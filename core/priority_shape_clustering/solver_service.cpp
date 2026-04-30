#include "solver_service.h"
#include "route_optimizer.h"
#include "slender_solver.h"
#include "types.h"
#include "../priority/sorter.h"
#include <numeric>
#include <unordered_set>
#include <climits>

static std::vector<Node> buildAllNodes(const solver::SolveRequest* req) {
    int N = req->matrix_size();
    std::vector<Node> nodes(N);

    nodes[0].id          = 0;
    nodes[0].name        = req->depot().id();
    nodes[0].lat         = req->depot().lat();
    nodes[0].lon         = req->depot().lng();
    nodes[0].weight      = 0.0;
    nodes[0].priority    = 0;
    nodes[0].deadline_min = 0;

    for (int i = 1; i < N; ++i) {
        const auto& pn = req->nodes(i - 1);
        nodes[i].id          = i;
        nodes[i].name        = pn.id();
        nodes[i].lat         = pn.lat();
        nodes[i].lon         = pn.lng();
        nodes[i].weight      = pn.demand();
        nodes[i].priority    = pn.priority();
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

// Builds (1 + cluster_size) x (1 + cluster_size) sub-matrix.
// Index 0 = depot, indices 1..k = cluster node_ids.
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

// Picks K center IDs evenly spaced across sorted candidate list.
static std::vector<int> pickCenters(const std::vector<int>& ids, int K) {
    int sz = (int)ids.size();
    std::vector<int> centers(K);
    for (int k = 0; k < K; ++k)
        centers[k] = ids[k * (sz - 1) / std::max(K - 1, 1) % sz];
    return centers;
}

grpc::Status SolverServiceImpl::Solve(grpc::ServerContext*,
                                       const solver::SolveRequest* req,
                                       solver::SolveResponse* resp)
{
    int N = req->matrix_size();
    if (N < 2) {
        resp->set_status("OK");
        return grpc::Status::OK;
    }

    std::vector<Node> all_nodes    = buildAllNodes(req);
    auto              dist_matrix  = unflatten(req->distances(), N);
    auto              dur_matrix   = unflatten(req->durations(), N);

    std::vector<int> all_indices(N - 1);
    std::iota(all_indices.begin(), all_indices.end(), 1);
    std::vector<int> sorted_unassigned = sortNodeIndices(all_nodes, all_indices);

    int V = req->vehicles_size();
    std::vector<double> caps(V);
    std::vector<int>    max_tasks(V);
    std::vector<int>    tasks_done(V, 0);
    for (int v = 0; v < V; ++v) {
        caps[v]      = req->vehicles(v).capacity();
        max_tasks[v] = req->vehicles(v).max_tasks();
    }

    std::vector<std::vector<Cluster>> vehicle_trips(V);

    while (!sorted_unassigned.empty()) {
        std::vector<double> active_caps;
        std::vector<int>    active_rem;
        std::vector<int>    active_v;
        for (int v = 0; v < V; ++v) {
            int left = max_tasks[v] == 0 ? INT_MAX : max_tasks[v] - tasks_done[v];
            if (left > 0) {
                active_caps.push_back(caps[v]);
                active_rem.push_back(left);
                active_v.push_back(v);
            }
        }
        if (active_v.empty()) break;

        int K = (int)active_v.size();

        std::vector<Node> candidates;
        candidates.reserve(sorted_unassigned.size());
        for (int idx : sorted_unassigned) candidates.push_back(all_nodes[idx]);

        std::vector<int> candidate_ids;
        candidate_ids.reserve(candidates.size());
        for (const auto& n : candidates) candidate_ids.push_back(n.id);

        std::vector<int> centers = pickCenters(candidate_ids, K);

        std::vector<Cluster> clusters = SlenderSolver::clusterWithCenters(
            candidates, all_nodes, centers, dist_matrix, active_caps, active_rem);

        std::unordered_set<int> assigned_ids;
        int assigned_total = 0;

        for (int i = 0; i < K; ++i) {
            if (i >= (int)clusters.size() || clusters[i].node_ids.empty()) continue;
            Cluster& c = clusters[i];

            Adj sub = buildSubAdj(c.node_ids, dist_matrix);
            RouteResult result = optimizeRoute(sub, (int)sub.size());

            c.route.resize(result.route.size());
            for (int j = 0; j < (int)result.route.size(); ++j)
                c.route[j] = result.route[j] == 0 ? 0 : c.node_ids[result.route[j] - 1];
            c.distance = result.distance;

            int v = active_v[i];
            vehicle_trips[v].push_back(c);
            tasks_done[v] += (int)c.node_ids.size();
            for (int id : c.node_ids) assigned_ids.insert(id);
            assigned_total++;
        }

        if (assigned_total == 0) break;

        sorted_unassigned.erase(
            std::remove_if(sorted_unassigned.begin(), sorted_unassigned.end(),
                [&](int idx) { return assigned_ids.count(idx) > 0; }),
            sorted_unassigned.end());
    }

    // Build SolveResponse
    std::unordered_set<int> all_assigned;
    double total_obj = 0.0;

    for (int v = 0; v < V; ++v) {
        if (vehicle_trips[v].empty()) continue;
        auto* route = resp->add_routes();
        route->set_vehicle_id(req->vehicles(v).id());

        double vdist = 0.0;
        int    vtime = req->vehicles(v).shift_start();
        int    cur   = 0;

        for (const auto& trip : vehicle_trips[v]) {
            for (int global_id : trip.route) {
                if (global_id == 0) continue;
                const auto& pn = req->nodes(global_id - 1);
                int travel = (int)dur_matrix[cur][global_id];
                int arr    = vtime + travel;
                if (pn.tw_start() > 0 && arr < pn.tw_start()) arr = pn.tw_start();
                int depart = arr + pn.service_time();

                auto* stop = route->add_stops();
                stop->set_node_id(pn.id());
                stop->set_arrival_min(arr);
                stop->set_depart_min(depart);

                vdist += dist_matrix[cur][global_id];
                vtime  = depart;
                cur    = global_id;
                all_assigned.insert(global_id);
            }
        }
        vdist += dist_matrix[cur][0]; // return to depot
        route->set_total_distance(vdist);
        route->set_total_duration(vtime - req->vehicles(v).shift_start());
        total_obj += vdist;
    }

    for (int i = 0; i < req->nodes_size(); ++i) {
        if (!all_assigned.count(i + 1))
            resp->add_unassigned(req->nodes(i).id());
    }

    resp->set_objective(total_obj);
    resp->set_status(resp->unassigned_size() == 0 ? "OK" : "INFEASIBLE");
    return grpc::Status::OK;
}
