#pragma once
#include <vector>
#include <algorithm>
#include <climits>
#include "../priority_shape_clustering/types.h"

// Sorts a copy of indices by: deadline_min ASC (0 = last), then priority DESC.
// Mirrors Go's core/priority.SortNodes.
inline std::vector<int> sortNodeIndices(const std::vector<Node>& nodes,
                                        const std::vector<int>& indices) {
    std::vector<int> sorted = indices;
    std::stable_sort(sorted.begin(), sorted.end(), [&](int a, int b) {
        int da = nodes[a].deadline_min == 0 ? INT_MAX : nodes[a].deadline_min;
        int db = nodes[b].deadline_min == 0 ? INT_MAX : nodes[b].deadline_min;
        if (da != db) return da < db;
        return nodes[a].priority > nodes[b].priority;
    });
    return sorted;
}
