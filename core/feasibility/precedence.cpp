#include "precedence.h"
#include <unordered_set>
#include <string>

namespace feasibility {

bool isLBPrecedenceFeasible(
    const std::vector<int>& route_node_ids,
    const google::protobuf::RepeatedPtrField<solver::Node>& all_nodes)
{
    bool seen_pickup = false;
    for (int id : route_node_ids) {
        const auto& node = all_nodes[id - 1];
        if (node.type() == "pickup") {
            seen_pickup = true;
        } else if (node.type() == "delivery" && seen_pickup) {
            return false;
        }
    }
    return true;
}

bool isPDPairFeasible(
    const std::vector<int>& route_node_ids,
    const google::protobuf::RepeatedPtrField<solver::Node>& all_nodes)
{
    std::unordered_set<std::string> seen_deliveries;
    for (int id : route_node_ids) {
        const auto& node = all_nodes[id - 1];
        if (node.pair_id().empty()) continue;
        if (node.type() == "delivery") {
            seen_deliveries.insert(node.pair_id());
        } else if (node.type() == "pickup" && seen_deliveries.count(node.pair_id())) {
            return false;
        }
    }
    return true;
}

} // namespace feasibility
