#pragma once
#include <vector>
#include "solver.pb.h"

namespace feasibility {

// VRPB (L/B): True if no delivery node follows any pickup node in the route.
// route_node_ids: internal solver IDs (1..N-1); proto node at index id-1.
bool isLBPrecedenceFeasible(
    const std::vector<int>& route_node_ids,
    const google::protobuf::RepeatedPtrField<solver::Node>& all_nodes);

// PD pair: True if for every pair_id, pickup appears before delivery in the route.
bool isPDPairFeasible(
    const std::vector<int>& route_node_ids,
    const google::protobuf::RepeatedPtrField<solver::Node>& all_nodes);

} // namespace feasibility
