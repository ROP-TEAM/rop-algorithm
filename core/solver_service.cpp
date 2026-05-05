#include "solver_service.h"
#include "priority_shape_clustering/route_optimizer.h"
#include "priority_shape_clustering/slender_solver.h"
#include "priority_shape_clustering/types.h"
#include "priority/sorter.h"
#include "priority_shape_clustering/slender_utils.h"
#include "feasibility/capacity.h"
#include "feasibility/tags.h"
#include "feasibility/max_distance.h"
#include "feasibility/precedence.h"
#include "feasibility/shift.h"
#include "feasibility/break.h"
#include <numeric>
#include <unordered_set>
#include <climits>
#include <random>
#include <chrono>
#include <algorithm>
#include <iostream>

static const double INF = 1e18;

// ---------------------------------------------------------------------------
// Data structures local to the solve pipeline
// ---------------------------------------------------------------------------

struct SolveContext {
    std::vector<Node>                    nodes;          // index 0 = depot
    std::vector<std::vector<double>>     dist;
    std::vector<std::vector<double>>     dur;
    std::vector<std::vector<double>>     delta;          // slender matrix
    std::vector<int>                     sortedIndices;  // priority-sorted node ids (1..N-1)
};

struct BestPlan {
    int                               K     = -1;
    double                            cost  = INF;
    double                            dist  = 0.0;
    std::vector<int>                  subset;
    std::vector<std::vector<Cluster>> trips; // trips[selected_index]
};

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::vector<Node> buildAllNodes(const solver::SolveRequest* req) {
    int N = req->matrix_size();
    std::vector<Node> nodes(N);

    nodes[0].id           = 0;
    nodes[0].name         = req->depot().id();
    nodes[0].lat          = req->depot().lat();
    nodes[0].lon          = req->depot().lng();
    nodes[0].weight       = 0.0;
    nodes[0].priority     = 0;
    nodes[0].deadline_min = 0;

    for (int i = 1; i < N; ++i) {
        const auto& pn = req->nodes(i - 1);
        nodes[i].id           = i;
        nodes[i].name         = pn.id();
        nodes[i].lat          = pn.lat();
        nodes[i].lon          = pn.lng();
        nodes[i].weight       = pn.demand();
        nodes[i].priority     = pn.priority();
        nodes[i].deadline_min = pn.deadline_min();
    }
    return nodes;
}

static std::vector<std::vector<double>> unflatten(
    const google::protobuf::RepeatedField<double>& flat, int n)
{
    std::vector<std::vector<double>> mat(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            mat[i][j] = flat[i * n + j];
    return mat;
}

static Adj buildSubAdj(const std::vector<int>& node_ids,
                       const std::vector<std::vector<double>>& dist)
{
    int k = node_ids.size();
    Adj sub(k + 1, std::vector<double>(k + 1, 0.0));
    for (int r = 1; r <= k; ++r) {
        sub[0][r] = sub[r][0] = dist[0][node_ids[r - 1]];
        for (int c = 1; c <= k; ++c)
            sub[r][c] = dist[node_ids[r - 1]][node_ids[c - 1]];
    }
    return sub;
}

// ---------------------------------------------------------------------------
// Stage 1: build context (nodes, matrices, slender delta, sorted order)
// ---------------------------------------------------------------------------

static SolveContext buildSolveContext(const solver::SolveRequest* req) {
    SolveContext ctx;
    int N = req->matrix_size();

    ctx.nodes = buildAllNodes(req);
    ctx.dist  = unflatten(req->distances(), N);
    ctx.dur   = unflatten(req->durations(), N);

    std::vector<double> theta(N), rho(N);
    double max_rho = 0;
    for (int i = 1; i < N; ++i) {
        theta[i] = calculateAngle(ctx.nodes[0].lat, ctx.nodes[0].lon,
                                  ctx.nodes[i].lat, ctx.nodes[i].lon);
        rho[i] = ctx.dist[0][i];
        if (rho[i] > max_rho) max_rho = rho[i];
    }
    ctx.delta.assign(N, std::vector<double>(N));
    computeSlenderMatrix(N, theta, rho, ctx.delta, max_rho);

    std::vector<int> indices(N - 1);
    std::iota(indices.begin(), indices.end(), 1);
    ctx.sortedIndices = sortNodeIndices(ctx.nodes, indices);

    return ctx;
}

// ---------------------------------------------------------------------------
// Stage 2: evaluate one vehicle subset (unchanged logic, now a named fn)
// ---------------------------------------------------------------------------

static std::pair<double, std::vector<std::vector<Cluster>>> evaluateSubset(
    const std::vector<int>&              veh_indices,
    const SolveContext&                  ctx,
    const solver::SolveRequest*          req,
    double                               fixed_cost_per_veh,
    double                               cost_per_km)
{
    int K = veh_indices.size();
    if (K == 0) return {INF, {}};

    std::vector<double> caps(K);
    std::vector<int>    max_orders(K);
    for (int i = 0; i < K; ++i) {
        const auto& v = req->vehicles(veh_indices[i]);
        caps[i]       = v.capacity();
        max_orders[i] = (v.max_tasks() == 0) ? INT_MAX : v.max_tasks();
    }

    std::vector<int>                     unassigned = ctx.sortedIndices;
    std::vector<int>                     orders_served(K, 0);
    std::vector<std::vector<Cluster>>    trips(K);

    bool feasible = true;
    while (!unassigned.empty()) {
        std::vector<int>    active_idx;
        std::vector<double> active_caps;
        std::vector<int>    active_rem_orders;
        for (int i = 0; i < K; ++i) {
            int left = max_orders[i] - orders_served[i];
            if (left > 0) {
                active_idx.push_back(i);
                active_caps.push_back(caps[i]);
                active_rem_orders.push_back(left);
            }
        }
        if (active_idx.empty()) { feasible = false; break; }

        size_t size_before = unassigned.size();
        auto clusters = SlenderSolver::runOneRound(
            ctx.nodes, ctx.delta, active_caps, active_rem_orders, unassigned);

        if (unassigned.size() == size_before) { feasible = false; break; }

        for (size_t ci = 0; ci < clusters.size(); ++ci) {
            if (clusters[ci].node_ids.empty()) continue;
            int veh_local = active_idx[ci];
            if (!feasibility::fitsCapacity(clusters[ci].total_weight, 0.0, caps[veh_local])) continue;

            const auto& v = req->vehicles(veh_indices[veh_local]);
            bool tags_ok = true;
            for (int node_id : clusters[ci].node_ids) {
                if (!feasibility::isTagCompatible(v, req->nodes(node_id - 1))) {
                    tags_ok = false;
                    break;
                }
            }
            if (!tags_ok) { feasible = false; break; }

            trips[veh_local].push_back(clusters[ci]);
            orders_served[veh_local] += (int)clusters[ci].node_ids.size();
        }
    }

    if (!unassigned.empty() || !feasible) return {INF, {}};

    double                  total_dist = 0.0;
    std::unordered_set<int> assigned_set;

    for (int i = 0; i < K; ++i) {
        const auto& v = req->vehicles(veh_indices[i]);
        for (auto& trip : trips[i]) {
            Adj sub = buildSubAdj(trip.node_ids, ctx.dist);
            RouteResult res = optimizeRoute(sub, (int)sub.size());

            trip.distance = res.distance;
            trip.route.clear();
            for (int idx : res.route) {
                if (idx == 0) continue;
                trip.route.push_back(trip.node_ids[idx - 1]);
            }

            if (!feasibility::isMaxDistanceFeasible(trip.distance, v.max_distance()))
                return {INF, {}};
            if (!feasibility::isLBPrecedenceFeasible(trip.route, req->nodes()))
                return {INF, {}};
            if (!feasibility::isPDPairFeasible(trip.route, req->nodes()))
                return {INF, {}};

            total_dist += trip.distance;
            for (int nid : trip.node_ids) assigned_set.insert(nid);
        }
    }

    if (assigned_set.size() != (ctx.nodes.size() - 1)) return {INF, {}};

    double total_cost = K * fixed_cost_per_veh + (total_dist / 1000.0) * cost_per_km;
    return {total_cost, trips};
}

// ---------------------------------------------------------------------------
// Stage 2: search over all K and random subsets to find the best plan
// ---------------------------------------------------------------------------

static BestPlan findBestPlan(const SolveContext& ctx,
                             const solver::SolveRequest* req,
                             const SolveConfig& cfg)
{
    int V = req->vehicles_size();
    std::mt19937 rng(cfg.seed);
    BestPlan best;

    for (int K = 1; K <= V; ++K) {
        std::cout << "[Solve] trying K=" << K << std::endl;
        double                            best_K_cost = INF;
        std::vector<int>                  best_K_subset;
        std::vector<std::vector<Cluster>> best_K_trips;

        for (int trial = 0; trial < cfg.randomTrials; ++trial) {
            std::vector<int> veh_indices(V);
            std::iota(veh_indices.begin(), veh_indices.end(), 0);
            std::shuffle(veh_indices.begin(), veh_indices.end(), rng);
            std::vector<int> subset(veh_indices.begin(), veh_indices.begin() + K);

            auto [cost, trips] = evaluateSubset(subset, ctx, req,
                                                cfg.fixedCostPerVehicle, cfg.costPerKm);
            if (cost < best_K_cost) {
                best_K_cost   = cost;
                best_K_subset = subset;
                best_K_trips  = trips;
            }
        }

        if (best_K_cost < INF) {
            std::cout << "  best cost=" << best_K_cost << " Baht\n";
            if (best_K_cost < best.cost) {
                best.K     = K;
                best.cost  = best_K_cost;
                best.subset = best_K_subset;
                best.trips  = best_K_trips;
            }
        }
    }

    return best;
}

// ---------------------------------------------------------------------------
// Stage 3: serialise best plan into the gRPC response
// ---------------------------------------------------------------------------

static bool buildResponse(const BestPlan& plan,
                          const SolveContext& ctx,
                          const solver::SolveRequest* req,
                          solver::SolveResponse* resp)
{
    double total_dist = 0.0;

    for (int i = 0; i < plan.K; ++i) {
        int global_idx = plan.subset[i];
        const auto& veh_info = req->vehicles(global_idx);
        if (plan.trips[i].empty()) continue;

        auto* route = resp->add_routes();
        route->set_vehicle_id(veh_info.id());

        double v_dist = 0.0;
        int    v_time = veh_info.shift_start();
        int    cur    = 0;
        std::vector<std::pair<int, int>> travel_gaps;

        for (const auto& trip : plan.trips[i]) {
            for (int node_id : trip.route) {
                int travel    = (int)ctx.dur[cur][node_id];
                int gap_start = v_time;
                int arr       = v_time + travel;
                travel_gaps.push_back({gap_start, arr});

                const auto& pn = req->nodes(node_id - 1);
                if (pn.tw_start() > 0 && arr < pn.tw_start())
                    arr = pn.tw_start();
                int depart = arr + pn.service_time();

                auto* stop = route->add_stops();
                stop->set_node_id(pn.id());
                stop->set_arrival_min(arr);
                stop->set_depart_min(depart);

                v_dist += ctx.dist[cur][node_id];
                v_time  = depart;
                cur     = node_id;
            }
        }

        if (!feasibility::isShiftWindowFeasible(v_time, veh_info.shift_end()))
            return false;
        if (!feasibility::isBreakWindowFeasible(travel_gaps, veh_info.break_start(), veh_info.break_end()))
            return false;

        v_dist += ctx.dist[cur][0];
        route->set_total_distance(v_dist);
        route->set_total_duration(v_time - veh_info.shift_start());
        total_dist += v_dist;
    }

    resp->set_objective(total_dist);
    resp->set_status("OK");
    return true;
}

// ---------------------------------------------------------------------------
// gRPC entry point
// ---------------------------------------------------------------------------

grpc::Status SolverServiceImpl::Solve(grpc::ServerContext*,
                                      const solver::SolveRequest* req,
                                      solver::SolveResponse* resp)
{
    auto t0 = std::chrono::high_resolution_clock::now();
    int N = req->matrix_size();

    if (N < 2) {
        resp->set_status("OK");
        return grpc::Status::OK;
    }

    SolveContext ctx  = buildSolveContext(req);
    BestPlan     best = findBestPlan(ctx, req, cfg_);

    if (best.K == -1) {
        resp->set_status("INFEASIBLE");
        return grpc::Status::OK;
    }

    if (!buildResponse(best, ctx, req, resp)) {
        resp->clear_routes();
        resp->set_status("INFEASIBLE");
        return grpc::Status::OK;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    std::cout << "[Solve] finished in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
              << " ms, best K=" << best.K
              << ", vehicles used=" << best.subset.size()
              << ", total distance=" << resp->objective() << " m"
              << ", total cost=" << best.cost << " Baht\n";

    return grpc::Status::OK;
}

// ---------------------------------------------------------------------------
// findBestPlanV2 — starting point for the new algorithm
// ---------------------------------------------------------------------------

static BestPlan findBestPlanV2(const SolveContext& ctx,
                               const solver::SolveRequest* req,
                               const SolveConfig& cfg)
{
    int V = req->vehicles_size();
    std::mt19937 rng(cfg.seed);
    BestPlan best;

    for (int K = 1; K <= V; ++K) {
        double                            best_K_cost = INF;
        std::vector<int>                  best_K_subset;
        std::vector<std::vector<Cluster>> best_K_trips;

        for (int trial = 0; trial < cfg.randomTrials; ++trial) {
            std::vector<int> veh_indices(V);
            std::iota(veh_indices.begin(), veh_indices.end(), 0);
            std::shuffle(veh_indices.begin(), veh_indices.end(), rng);
            std::vector<int> subset(veh_indices.begin(), veh_indices.begin() + K);

            auto [cost, trips] = evaluateSubset(subset, ctx, req,
                                                cfg.fixedCostPerVehicle, cfg.costPerKm);
            if (cost < best_K_cost) {
                best_K_cost   = cost;
                best_K_subset = subset;
                best_K_trips  = trips;
            }
        }

        if (best_K_cost < INF && best_K_cost < best.cost) {
            best.K      = K;
            best.cost   = best_K_cost;
            best.subset = best_K_subset;
            best.trips  = best_K_trips;
        }
    }

    return best;
}

// ---------------------------------------------------------------------------
// SolverV2 entry point
// ---------------------------------------------------------------------------

solver::SolveResponse SolverV2::Solve(const solver::SolveRequest& req) {
    solver::SolveResponse resp;
    if (req.matrix_size() < 2) {
        resp.set_status("OK");
        return resp;
    }

    SolveContext ctx  = buildSolveContext(&req);
    BestPlan     best = findBestPlanV2(ctx, &req, cfg_);

    if (best.K == -1) {
        resp.set_status("INFEASIBLE");
        return resp;
    }

    if (!buildResponse(best, ctx, &req, &resp)) {
        resp.clear_routes();
        resp.set_status("INFEASIBLE");
    }
    return resp;
}
