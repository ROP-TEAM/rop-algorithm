#include "solver_service.h"
#include "priority_shape_clustering/route_optimizer.h"
#include "priority_shape_clustering/slender_solver.h"
#include "priority_shape_clustering/types.h"
#include "priority/sorter.h"
#include "priority_shape_clustering/slender_utils.h"
#include <numeric>
#include <unordered_set>
#include <climits>
#include <random>
#include <chrono>
#include <algorithm>
#include <iostream>

const double FIXED_COST_PER_VEHICLE = 450.0;
const double COST_PER_KM            = 40.02;
const double INF                    = 1e18;

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

/*
   Evaluate one subset of vehicles (their indices in full fleet)
   Returns {total_cost, plan}, where plan is a vector of trips per SELECTED vehicle (size = K)
   If infeasible, cost = INF.
*/
std::pair<double, std::vector<std::vector<Cluster>>> evaluateSubset(
    const std::vector<int>& veh_indices,
    const std::vector<Node>& nodes,
    const std::vector<std::vector<double>>& delta,
    const std::vector<std::vector<double>>& dist,
    const solver::SolveRequest* req,
    const std::vector<int>& sorted_unassigned,
    double fixed_cost_per_veh,
    double cost_per_km)
{
  int K = veh_indices.size();
  if (K == 0) return {INF, {}};

  // Capacity and daily order limit for each selected vehicle
  std::vector<double> caps(K);
  std::vector<int> max_orders(K);
  for (int i = 0; i < K; ++i) {
    const auto& v = req->vehicles(veh_indices[i]);
    caps[i] = v.capacity();
    // max_tasks == 0 → unlimited // temporary
    max_orders[i] = (v.max_tasks() == 0) ? INT_MAX : v.max_tasks();
  }

  // Multi‑round assignment
  std::vector<int> unassigned = sorted_unassigned;   // copy
  std::vector<int> orders_served(K, 0);
  std::vector<std::vector<Cluster>> trips(K);         // per selected vehicle (local index)

  bool feasible = true;
  while (!unassigned.empty()) {
    // Determine active vehicles (still have daily capacity)
    std::vector<int> active_idx;                   // local indices 0..K-1
    std::vector<double> active_caps;
    std::vector<int> active_rem_orders;
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
        nodes, delta, active_caps, active_rem_orders, unassigned);

    // No progress → infeasible
    if (unassigned.size() == size_before) { feasible = false; break; }

    // Attach clusters to the right vehicle
    for (size_t ci = 0; ci < clusters.size(); ++ci) {
      if (clusters[ci].node_ids.empty()) continue;
      int veh_local = active_idx[ci];
      if (clusters[ci].total_weight > caps[veh_local] + 1e-9) continue;
      trips[veh_local].push_back(clusters[ci]);
      orders_served[veh_local] += (int)clusters[ci].node_ids.size();
    }
  }

  if (!unassigned.empty() || !feasible) return {INF, {}};

  // Solve TSP for each trip and compute total distance
  double total_dist = 0.0;
  std::unordered_set<int> assigned_set;

  for (int i = 0; i < K; ++i) {
    for (auto& trip : trips[i]) {

      Adj sub = buildSubAdj(trip.node_ids, dist);
      RouteResult res = optimizeRoute(sub, (int)sub.size());

      trip.distance = res.distance;

      // convert route back to global node IDs
      trip.route.clear();
      for (int idx : res.route) {
          if (idx == 0) continue;           // ข้าม depot (index 0)
          trip.route.push_back(trip.node_ids[idx - 1]);
      }

      total_dist += trip.distance;
      for (int nid : trip.node_ids) assigned_set.insert(nid);

    }
  }

  if (assigned_set.size() != (nodes.size() - 1)) return {INF, {}};

  double total_cost = K * fixed_cost_per_veh + (total_dist/1000.0) * cost_per_km;
  return {total_cost, trips};
}

grpc::Status SolverServiceImpl::Solve(grpc::ServerContext*,
                                       const solver::SolveRequest* req,
                                       solver::SolveResponse* resp)
{

    auto t0 = std::chrono::high_resolution_clock::now();
    int N = req->matrix_size();
    int V = req->vehicles_size();

    if (N < 2) {
        resp->set_status("OK");
        return grpc::Status::OK;
    }

    // 1. build data structures
    std::vector<Node> all_nodes   = buildAllNodes(req);
    auto              dist = unflatten(req->distances(), N);
    auto              dur  = unflatten(req->durations(), N);

    // Pre-compute Slender Matrix
    std::vector<double> theta(N), rho(N);
    double max_rho = 0;
    for (int i = 1; i < N; ++i) {
      theta[i] = calculateAngle(all_nodes[0].lat, all_nodes[0].lon, all_nodes[i].lat, all_nodes[i].lon);
      rho[i] = dist[0][i];
      if (rho[i] > max_rho) max_rho = rho[i];
    }
    std::vector<std::vector<double>> delta(N, std::vector<double>(N));
    computeSlenderMatrix(N, theta, rho, delta, max_rho);

    // Priority sorted order
    std::vector<int> indices(N - 1);
    std::iota(indices.begin(), indices.end(), 1);
    auto sorted_unassigned = sortNodeIndices(all_nodes, indices);

    // ---------- 2. Try every possible K and random subsets ----------
    std::mt19937 rng(std::random_device{}());
    const int RANDOM_TRIALS = 30;   // per K

    double       best_total_cost = INF;
    double       best_total_dist = 0;
    int          best_K          = -1;
    std::vector<int> best_subset;                     // indices into full fleet
    std::vector<std::vector<Cluster>> best_plan;      // plan[selected_index] = trips

    for (int K = 1; K <= V; ++K) {
      std::cout << "[Solve] trying K=" << K << std::endl;
      double best_K_cost = INF;
      std::vector<int> best_K_subset;
      std::vector<std::vector<Cluster>> best_K_plan;

      for (int trial = 0; trial < RANDOM_TRIALS; ++trial) {
        // Random subset of size K
        std::vector<int> veh_indices(V);
        std::iota(veh_indices.begin(), veh_indices.end(), 0);
        std::shuffle(veh_indices.begin(), veh_indices.end(), rng);
        std::vector<int> subset(veh_indices.begin(), veh_indices.begin() + K);

        auto [cost, plan] = evaluateSubset(subset, all_nodes, delta, dist, req,
            sorted_unassigned,
            FIXED_COST_PER_VEHICLE, COST_PER_KM);
        if (cost < best_K_cost) {
          best_K_cost = cost;
          best_K_subset = subset;
          best_K_plan = plan;
        }
      }

      if (best_K_cost < INF) {
        std::cout << "  best cost=" << best_K_cost << " Baht\n";
        if (best_K_cost < best_total_cost) {
          best_total_cost = best_K_cost;
          best_K = K;
          best_subset = best_K_subset;
          best_plan = best_K_plan;
        }
      }
    }

    // ---------- 3. Build response from best plan ----------
    if (best_K == -1) {
      resp->set_status("INFEASIBLE");
      return grpc::Status::OK;
    }

    // For each selected vehicle, output its trips with time windows
    for (int i = 0; i < best_K; ++i) {
      int global_idx = best_subset[i];
      const auto& veh_info = req->vehicles(global_idx);
      if (best_plan[i].empty()) continue;

      auto* route = resp->add_routes();
      route->set_vehicle_id(veh_info.id());

      double v_dist = 0.0;
      int v_time = veh_info.shift_start();   // initial time from depot
      int cur = 0;   // start at depot

      for (const auto& trip : best_plan[i]) {
        for (int node_id : trip.route) {
          // Travel time + waiting time logic
          int travel = (int)dur[cur][node_id];
          int arr = v_time + travel;
          const auto& pn = req->nodes(node_id - 1);
          if (pn.tw_start() > 0 && arr < pn.tw_start())
            arr = pn.tw_start();
          int depart = arr + pn.service_time();

          auto* stop = route->add_stops();
          stop->set_node_id(pn.id());
          stop->set_arrival_min(arr);
          stop->set_depart_min(depart);

          v_dist += dist[cur][node_id];
          v_time = depart;
          cur = node_id;
        }
      }
      v_dist += dist[cur][0];   // return to depot
      route->set_total_distance(v_dist);
      route->set_total_duration(v_time - veh_info.shift_start());
      best_total_dist += v_dist;
    }

    resp->set_objective(best_total_dist);
    resp->set_status("OK");

    auto t1 = std::chrono::high_resolution_clock::now();
    std::cout << "[Solve] finished in "
      << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
      << " ms, best K=" << best_K
      << ", vehicles used=" << best_subset.size()
      << ", total distance=" << best_total_dist << " m"
      << ", total cost=" << best_total_cost << " Baht\n";

    return grpc::Status::OK;
}
