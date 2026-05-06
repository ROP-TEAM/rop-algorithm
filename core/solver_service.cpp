#include "solver_service.h"
#include "priority_shape_clustering/route_optimizer.h"
#include "priority_shape_clustering/slender_solver.h"
#include "priority_shape_clustering/types.h"
#include "priority/sorter.h"
#include "priority_shape_clustering/slender_utils.h"
#include "feasibility/capacity.h"
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
        nodes[i].tw_start     = pn.tw_start();
        nodes[i].tw_end       = (pn.tw_end() > 0) ? pn.tw_end() : 1440;
        nodes[i].service_time = pn.service_time();
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

// Compute route distance depot→seq[0]→seq[1]→…→seq[n-1]→depot.
static double nnTspDistance(const std::vector<int>&              node_ids,
                            const std::vector<std::vector<double>>& dist) {
    if (node_ids.empty()) return 0.0;
    std::vector<bool> visited(dist.size(), false);
    double total   = 0.0;
    int    current = 0;
    for (size_t step = 0; step < node_ids.size(); ++step) {
        double min_d = 1e18;
        int    best  = -1;
        for (int id : node_ids) {
            if (!visited[id] && dist[current][id] < min_d) {
                min_d = dist[current][id];
                best  = id;
            }
        }
        if (best != -1) { total += min_d; current = best; visited[best] = true; }
    }
    total += dist[current][0];
    return total;
}

static double simulateTripReturnTime(const Cluster&                          cluster,
                                      const std::vector<Node>&                nodes,
                                      const std::vector<std::vector<double>>& dur,
                                      double                                  ready_time) {
    if (cluster.node_ids.empty()) return ready_time;
    double t   = ready_time;
    int    cur = 0;
    for (int id : cluster.node_ids) {
        double arr = t + dur[cur][id];
        if (arr < nodes[id].tw_start) arr = nodes[id].tw_start;
        t   = arr + nodes[id].service_time;
        cur = id;
    }
    return t + dur[cur][0];
}

static std::pair<double, std::vector<std::vector<Cluster>>> evaluateSubset(
    const std::vector<int>&              veh_indices,
    const SolveContext&                  ctx,
    const solver::SolveRequest*          req,
    double                               fixed_cost_per_veh,
    double                               cost_per_km,
    uint32_t                             /*seed — unused; SlenderSolver uses fixed internal seed*/)
{
    int K = veh_indices.size();
    if (K == 0) return {INF, {}};

    // Per-vehicle weight capacity (constant across trips — no daily order cap)
    std::vector<double> caps(K);
    for (int i = 0; i < K; ++i)
        caps[i] = req->vehicles(veh_indices[i]).capacity();

    std::vector<double> ready_times(K);
    for (int i = 0; i < K; ++i)
        ready_times[i] = static_cast<double>(req->vehicles(veh_indices[i]).shift_start());

    std::vector<int>                  unassigned = ctx.sortedIndices;
    std::vector<std::vector<Cluster>> trips(K);

    bool feasible = true;
    while (!unassigned.empty()) {
        size_t size_before = unassigned.size();

        auto clusters = SlenderSolver::runOneRound(
            ctx.nodes, ctx.delta, ctx.dur, caps, unassigned, ready_times);

        if (unassigned.size() == size_before) { feasible = false; break; }

        for (int ci = 0; ci < (int)clusters.size() && ci < K; ++ci) {
            if (clusters[ci].node_ids.empty()) continue;
            // Capacity safety check (clustering already enforces, but verify)
            if (!feasibility::fitsCapacity(clusters[ci].total_weight, 0.0, caps[ci]))
                continue;

            trips[ci].push_back(clusters[ci]);
            ready_times[ci] = simulateTripReturnTime(
                clusters[ci], ctx.nodes, ctx.dur, ready_times[ci]);
            if (ready_times[ci] > req->vehicles(veh_indices[ci]).shift_end())
                caps[ci] = 0.0;
        }
    }

    if (!unassigned.empty() || !feasible) return {INF, {}};

    // Use insertion order as route — it is time-feasible by construction.
    // TSP re-ordering is skipped to preserve hard time-window feasibility.
    double                  total_dist = 0.0;
    std::unordered_set<int> assigned_set;

    for (int i = 0; i < K; ++i) {
        for (auto& trip : trips[i]) {
            trip.route    = trip.node_ids;  // time-feasible insertion order
            trip.distance = nnTspDistance(trip.route, ctx.dist);

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

    for (int attempt = 0; attempt < cfg.randomTrials; ++attempt) {
        int K = std::uniform_int_distribution<int>(1, V)(rng);

        std::vector<int> veh_indices(V);
        std::iota(veh_indices.begin(), veh_indices.end(), 0);
        std::shuffle(veh_indices.begin(), veh_indices.end(), rng);
        std::vector<int> subset(veh_indices.begin(), veh_indices.begin() + K);

        auto [cost, trips] = evaluateSubset(subset, ctx, req,
                                            cfg.fixedCostPerVehicle, cfg.costPerKm, cfg.seed);

        if (cost < best.cost) {
            best.cost   = cost;
            best.K      = K;
            best.subset = subset;
            best.trips  = trips;
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
        for (const auto& trip : plan.trips[i]) {
            if (cur != 0) {  // return to depot between trips
                v_dist += ctx.dist[cur][0];
                v_time += (int)ctx.dur[cur][0];
                cur     = 0;
            }
            route->add_trip_sizes((int)trip.route.size());
            for (int node_id : trip.route) {
                int travel    = (int)ctx.dur[cur][node_id];
                int arr       = v_time + travel;

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
                                                cfg.fixedCostPerVehicle, cfg.costPerKm, cfg.seed);
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
