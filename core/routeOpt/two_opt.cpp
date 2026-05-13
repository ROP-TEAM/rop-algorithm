#include "routeOpt/two_opt.h"
#include "validator/route_state.h"
#include <algorithm>
#include <cmath>

namespace hfvrptwb {

RouteState twoOptTrip(
    const solver::SolveRequest& req,
    const solver::Vehicle& vehicle,
    RouteState trip,
    double default_fixed_cost,
    double default_cost_per_km)
{
    if (trip.nodes.size() < 3) return trip;
    bool improved = true;
    while (improved) {
        improved = false;
        int n = (int)trip.nodes.size();
        for (int i = 0; i < n - 1 && !improved; ++i) {
            for (int j = i + 2; j < n && !improved; ++j) {
                auto candidate = trip.nodes;
                std::reverse(candidate.begin() + i + 1, candidate.begin() + j + 1);
                auto eval = evaluateRouteState(
                    req, vehicle, trip, std::move(candidate),
                    default_fixed_cost, default_cost_per_km);
                if (eval.feasible && std::isfinite(eval.next.cost) &&
                        eval.next.cost < trip.cost - 1e-6) {
                    trip = std::move(eval.next);
                    improved = true;
                }
            }
        }
    }
    return trip;
}

} // namespace hfvrptwb
