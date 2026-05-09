#include "slender_solver.h"
#include "slender_utils.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <unordered_set>

// ---------------------------------------------------------------------------
// Heuristic tuning constants
// ---------------------------------------------------------------------------

static constexpr double BETA         = 0.01;   // waiting-time penalty coefficient
static constexpr double ALPHA        = 0.10;   // flexibility-loss penalty coefficient
static constexpr double LAMBDA       = 0.02;   // priority penalty coefficient
// static constexpr double BETA         = 0.80;   // waiting-time penalty coefficient
// static constexpr double ALPHA        = 0.10;   // flexibility-loss penalty coefficient
// static constexpr double LAMBDA       = 0.02;   // priority penalty coefficient
static constexpr double TIME_SCALE   = 480.0;  // normalise minutes to [0,1] over 8-hour day
static constexpr int    MAX_ITER     = 50;     // k-medoids convergence limit per restart
static constexpr int    NUM_RESTARTS = 10;     // independent random restarts

// ---------------------------------------------------------------------------
// Internal: Macro Node time state for one cluster
// ---------------------------------------------------------------------------

struct MacroState {
    double E;       // earliest feasible departure-start of the sequence
    double L;       // latest  feasible departure-start of the sequence
    double S;       // accumulated service + travel time from sequence start
    int    last_id; // id of the last node appended (for dur[last_id][next] lookup)
};

// Initialise a cluster's Macro Node state from its lone medoid.
static MacroState initFromMedoid(const Node& m, double ready_time,
                                  double travel_depot_to_medoid) {
    const double earliest = ready_time + travel_depot_to_medoid;
    return {
        std::max(static_cast<double>(m.tw_start), earliest),
        static_cast<double>(m.tw_end),
        static_cast<double>(m.service_time),
        m.id
    };
}

// ---------------------------------------------------------------------------
// Macro Node merge — attempt to append node i after the current sequence.
//
// Theory (all times in minutes from midnight):
//   If the vehicle starts the sequence at time τ ∈ [E, L], it arrives at i at
//   τ + S + t, where S is accumulated time and t = dur[last_id][i.id].
//
//   Case 1 (hard reject): E + S + t > l_i
//     Cannot reach i before its TW closes even from the earliest departure.
//
//   Case 2 (mandatory wait): L + S + t < e_i
//     Even the latest departure leaves i before its TW opens; the vehicle must
//     depart exactly at L, travel, then wait.  Window collapses to a point.
//
//   Case 3 (overlap): intersection of [E,L] and [e_i-S-t, l_i-S-t] is non-empty.
//
// Returns feasible=false for Case 1.
// ---------------------------------------------------------------------------

struct MergeResult {
    MacroState next;
    double     w_plus;    // mandatory waiting time added to S
    double     flex_loss; // reduction in [E,L] window width
    bool       feasible;
};

static MergeResult tryMerge(const MacroState& ms, const Node& node,
                             const std::vector<std::vector<double>>& dur) {
    const double t   = dur[ms.last_id][node.id];
    const double e_i = static_cast<double>(node.tw_start);
    const double l_i = static_cast<double>(node.tw_end);
    const double s_i = static_cast<double>(node.service_time);

    // Case 1: infeasible — earliest arrival already exceeds TW close
    if (ms.E + ms.S + t > l_i) {
        return {{}, 0.0, 0.0, false};
    }

    // Waiting time incurred when departing at the LATEST start time
    const double w_plus = std::max(0.0, e_i - (ms.L + ms.S + t));

    MacroState next;
    next.last_id = node.id;

    if (ms.L + ms.S + t < e_i) {
        // Case 2: mandatory wait — window collapses to single point at L
        next.E = ms.L;
        next.L = ms.L;
        next.S = ms.S + s_i + t + w_plus;
    } else {
        // Case 3: intersect the feasible departure windows
        next.E = std::min(ms.L, std::max(ms.E, e_i - (ms.S + t)));
        next.L = std::min(ms.L, l_i - (ms.S + t));
        next.S = ms.S + s_i + t + w_plus;

        if (next.E > next.L) {
            return {{}, 0.0, 0.0, false};
        }
    }

    // Flexibility loss = how much the window [E,L] shrinks due to node i
    const double flex_loss =
        std::max(0.0, (e_i - ms.S - t) - ms.E) +
        std::max(0.0, ms.L - (l_i - ms.S - t));

    return {next, w_plus, flex_loss, true};
}

// ---------------------------------------------------------------------------
// Heuristic assignment score for placing node i into cluster k.
//
//   score = delta[i][medoid_k]
//         + BETA   * (w_plus    / TIME_SCALE)   -- penalise waiting
//         + ALPHA  * (flex_loss / TIME_SCALE)   -- penalise flexibility loss
//         + LAMBDA * (4 - priority)             -- penalise lower-priority nodes
//
// Lower is better.  delta already ∈ [0,1]; time terms are normalised.
// ---------------------------------------------------------------------------

static double assignScore(double delta_val, double w_plus, double flex_loss,
                          int priority) {
    return delta_val
         + BETA   * (w_plus    / TIME_SCALE)
         + ALPHA  * (flex_loss / TIME_SCALE)
         + LAMBDA * static_cast<double>(4 - priority);
}

// ---------------------------------------------------------------------------
// One convergence run of K-medoids with time-aware greedy assignment.
//
// centers: in/out — updated to the converged spatial medoids.
// candidates: full unassigned set for this round (not modified here).
// Returns K Cluster objects; node_ids is the time-feasible visit order.
// ---------------------------------------------------------------------------

static std::vector<Cluster> kMedoidsIterate(
    const std::vector<Node>&                all_nodes,
    std::vector<int>&                       centers,
    const std::vector<std::vector<double>>& delta,
    const std::vector<std::vector<double>>& dur,
    const std::vector<double>&              caps,
    const std::vector<int>&                 candidates,
    const std::vector<double>&              ready_times,
    std::mt19937&                           rng)
{
    const int K = static_cast<int>(centers.size());
    std::vector<Cluster>    clusters(K);
    std::vector<MacroState> states(K);

    bool changed = true;
    for (int iter = 0; iter < MAX_ITER && changed; ++iter) {
        // ---- reset each cluster to its lone medoid ----------------------------
        for (int k = 0; k < K; ++k) {
            const Node& m = all_nodes[centers[k]];
            clusters[k]   = {};
            clusters[k].center_id = m.id;

            MacroState ms = initFromMedoid(m, ready_times[k], dur[0][m.id]);
            states[k]     = ms;

            if (ms.E <= ms.L) {
                clusters[k].node_ids     = {m.id};
                clusters[k].total_weight = m.weight;
            }
        }

        // Medoid set for O(1) exclusion during remaining-node iteration
        std::unordered_set<int> medoid_set(centers.begin(), centers.end());

        // ---- build the non-medoid candidate list and shuffle for fairness ----
        std::vector<int> remaining;
        remaining.reserve(candidates.size());
        for (int id : candidates) {
            if (!medoid_set.count(id)) remaining.push_back(id);
        }
        std::shuffle(remaining.begin(), remaining.end(), rng);

        // ---- greedy assignment: each node → best feasible cluster ------------
        for (int node_id : remaining) {
            const Node& node = all_nodes[node_id];

            double      best_score = std::numeric_limits<double>::max();
            int         best_k     = -1;
            MergeResult best_mr{};

            for (int k = 0; k < K; ++k) {
                // Capacity guard
                if (clusters[k].total_weight + node.weight > caps[k]) continue;

                MergeResult mr = tryMerge(states[k], node, dur);
                if (!mr.feasible) continue;

                const double s = assignScore(delta[node_id][centers[k]],
                                             mr.w_plus, mr.flex_loss, node.priority);
                if (s < best_score) {
                    best_score = s;
                    best_k     = k;
                    best_mr    = mr;
                }
            }

            if (best_k != -1) {
                clusters[best_k].node_ids.push_back(node_id);
                clusters[best_k].total_weight += node.weight;
                states[best_k] = best_mr.next;
            }
        }

        // ---- update medoids: node minimising sum-delta to cluster members ----
        // Spatial delta only; does NOT alter the time-state of the sequence.
        changed = false;
        for (int k = 0; k < K; ++k) {
            if (clusters[k].node_ids.empty()) continue;

            int    best_c  = centers[k];
            double min_sum = std::numeric_limits<double>::max();
            for (int cand : clusters[k].node_ids) {
                double sum = 0.0;
                for (int mem : clusters[k].node_ids) sum += delta[cand][mem];
                if (sum < min_sum) { min_sum = sum; best_c = cand; }
            }
            if (best_c != centers[k]) {
                centers[k] = best_c;
                changed    = true;
            }
        }
    }

    // ---- store converged Macro Node state into each cluster -----------------
    for (int k = 0; k < K; ++k) {
        clusters[k].E            = states[k].E;
        clusters[k].L            = states[k].L;
        clusters[k].S            = states[k].S;
        clusters[k].last_node_id = states[k].last_id;
    }

    return clusters;
}

// ---------------------------------------------------------------------------
// Public: multi-restart time-aware K-medoids
// ---------------------------------------------------------------------------

std::vector<Cluster> SlenderSolver::runOneRound(
    const std::vector<Node>&                all_nodes,
    const std::vector<std::vector<double>>& delta,
    const std::vector<std::vector<double>>& dur,
    const std::vector<double>&              active_capacities,
    std::vector<int>&                       unassigned_nodes,
    const std::vector<double>&              vehicle_ready_times)
{
    const int K         = static_cast<int>(active_capacities.size());
    const int pool_size = static_cast<int>(unassigned_nodes.size());

    if (K == 0 || pool_size == 0) return {};

    // Fixed seed → deterministic across calls (non-determinism was a known bug)
    std::mt19937 rng(std::random_device{}());

    std::vector<int>     best_centers;
    std::vector<Cluster> best_clusters;
    int    best_assigned = 0;
    double best_score    = std::numeric_limits<double>::max();

    for (int restart = 0; restart < NUM_RESTARTS; ++restart) {
        // Pick K initial medoids via random shuffle (with wrap-around if K > pool)
        std::vector<int> shuffled = unassigned_nodes;
        std::shuffle(shuffled.begin(), shuffled.end(), rng);

        std::vector<int> centers(K);
        for (int k = 0; k < K; ++k)
            centers[k] = shuffled[k % pool_size];

        std::vector<Cluster> current = kMedoidsIterate(
            all_nodes, centers, delta, dur,
            active_capacities, unassigned_nodes, vehicle_ready_times, rng);

        // Evaluate: prefer max assigned nodes, then min total delta-to-medoid
        int    assigned = 0;
        double score    = 0.0;
        for (const auto& c : current) {
            for (int id : c.node_ids) {
                score += delta[id][c.center_id];
                ++assigned;
            }
        }

        const bool is_better =
            (assigned > best_assigned) ||
            (assigned == best_assigned && score < best_score);

        if (is_better) {
            best_assigned = assigned;
            best_score    = score;
            best_centers  = centers;   // converged medoids for this restart
            best_clusters = current;
        }
    }

    if (best_clusters.empty()) return {};

    // Final deterministic pass with the best-found medoids
    {
        std::mt19937 final_rng(std::random_device{}());
        best_clusters = kMedoidsIterate(
            all_nodes, best_centers, delta, dur,
            active_capacities, unassigned_nodes, vehicle_ready_times, final_rng);
    }

    // Remove newly assigned nodes from the caller's unassigned list
    std::unordered_set<int> assigned_set;
    for (const auto& c : best_clusters)
        for (int id : c.node_ids) assigned_set.insert(id);

    std::vector<int> still_unassigned;
    still_unassigned.reserve(unassigned_nodes.size());
    for (int id : unassigned_nodes)
        if (!assigned_set.count(id)) still_unassigned.push_back(id);
    unassigned_nodes = std::move(still_unassigned);

    return best_clusters;
}

// ---------------------------------------------------------------------------
// Public: plan() — full pipeline: slender matrix → multi-trip K-medoids
// ---------------------------------------------------------------------------

SlenderPlan SlenderSolver::plan(
    const std::vector<Node>&                nodes,
    const std::vector<Vehicle>&             vehicles,
    const std::vector<std::vector<double>>& dur)
{
    const int N = static_cast<int>(nodes.size());
    const int K = static_cast<int>(vehicles.size());

    // 1. Compute theta (bearing from depot) and rho (distance from depot)
    std::vector<double> theta(N, 0.0);
    std::vector<double> rho(N, 0.0);
    double max_rho = 0.0;
    for (int i = 1; i < N; ++i) {
        theta[i] = calculateAngle(nodes[0].lat, nodes[0].lon,
                                  nodes[i].lat, nodes[i].lon);
        rho[i]   = dur[0][i];  // use travel time as radial proxy
        if (rho[i] > max_rho) max_rho = rho[i];
    }

    // 2. Build N×N slender similarity matrix
    std::vector<std::vector<double>> delta(N, std::vector<double>(N, 0.0));
    computeSlenderMatrix(N, theta, rho, delta, max_rho);

    // 3. Initial state: all order indices unassigned, sorted deadline ASC → priority DESC
    std::vector<int> unassigned(N - 1);
    std::iota(unassigned.begin(), unassigned.end(), 1);
    std::stable_sort(unassigned.begin(), unassigned.end(), [&](int a, int b) {
        int da = nodes[a].deadline_min == 0 ? 999999 : nodes[a].deadline_min;
        int db = nodes[b].deadline_min == 0 ? 999999 : nodes[b].deadline_min;
        if (da != db) return da < db;
        return nodes[a].priority > nodes[b].priority;
    });

    // 4. Per-vehicle state
    std::vector<double> caps(K);
    std::vector<double> ready_times(K);
    for (int v = 0; v < K; ++v) {
        caps[v]        = vehicles[v].capacity;
        ready_times[v] = static_cast<double>(vehicles[v].shift_start);
    }

    SlenderPlan result;
    result.trips_per_vehicle.resize(K);

    // 5. Multi-trip loop: repeat until all assigned or no progress
    while (!unassigned.empty()) {
        const size_t before = unassigned.size();

        std::vector<Cluster> round = runOneRound(
            nodes, delta, dur, caps, unassigned, ready_times);

        if (unassigned.size() == before) break;  // no progress — remaining unassignable

        for (int v = 0; v < K && v < static_cast<int>(round.size()); ++v) {
            if (round[v].node_ids.empty()) continue;

            result.trips_per_vehicle[v].push_back(round[v]);

            // Advance vehicle clock past this trip's return to depot
            double t   = ready_times[v];
            int    cur = 0;
            for (int id : round[v].node_ids) {
                double arr = t + dur[cur][id];
                if (arr < nodes[id].tw_start) arr = nodes[id].tw_start;
                t   = arr + nodes[id].service_time;
                cur = id;
            }
            ready_times[v] = t + dur[cur][0];

            // Block vehicle if shift is exhausted
            if (ready_times[v] > vehicles[v].shift_end)
                caps[v] = 0.0;
        }
    }

    result.unassigned = std::move(unassigned);
    return result;
}
