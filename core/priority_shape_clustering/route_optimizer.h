#pragma once
#include <vector>
#include <queue>
#include <algorithm>
#include <cmath>
#include <limits>
#include <climits>

using Adj  = std::vector<std::vector<double>>;
using Edge = std::pair<int, int>;

struct RouteResult {
    std::vector<int> route;
    double distance;
};

inline std::vector<Edge> buildMST(Adj& adj, int n) {
    std::priority_queue<std::pair<double, int>,
                        std::vector<std::pair<double, int>>,
                        std::greater<>> pq;
    std::vector<bool>   visited(n, false);
    std::vector<int>    parent(n, -1);
    std::vector<double> edgeW(n, std::numeric_limits<double>::max());
    std::vector<Edge>   result;

    edgeW[0] = 0.0;
    pq.push({0.0, 0});

    while (!pq.empty()) {
        int u = pq.top().second;
        pq.pop();
        if (visited[u]) continue;
        visited[u] = true;
        if (parent[u] != -1) result.push_back({parent[u], u});
        for (int v = 0; v < n; ++v) {
            if (!visited[v] && adj[u][v] < edgeW[v]) {
                parent[v] = u;
                edgeW[v]  = adj[u][v];
                pq.push({adj[u][v], v});
            }
        }
    }
    return result;
}

inline void dfs(int u, std::vector<std::vector<int>>& tree,
                std::vector<bool>& visited, std::vector<int>& order) {
    visited[u] = true;
    order.push_back(u);
    for (int v : tree[u])
        if (!visited[v]) dfs(v, tree, visited, order);
}

inline std::vector<int> getRoute(std::vector<Edge>& mst, int n) {
    std::vector<std::vector<int>> tree(n);
    for (auto [u, v] : mst) {
        tree[u].push_back(v);
        tree[v].push_back(u);
    }
    std::vector<bool> visited(n, false);
    std::vector<int>  order;
    dfs(0, tree, visited, order);
    return order;
}

inline void applySwap(std::vector<int>& route, int i, int j) {
    std::reverse(route.begin() + i + 1, route.begin() + j + 1);
}

inline bool twoOptPass(std::vector<int>& route, Adj& adj, int& swapCount) {
    int n = (int)route.size();
    for (int i = 0; i < n - 1; ++i) {
        for (int j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) continue;
            int a = route[i], b = route[i + 1];
            int c = route[j], d = route[(j + 1) % n];
            if (adj[a][c] + adj[b][d] < adj[a][b] + adj[c][d] - 1e-9) {
                applySwap(route, i, j);
                ++swapCount;
                return true;
            }
        }
    }
    return false;
}

inline std::vector<int> twoOpt(std::vector<int> route, Adj& adj) {
    int  swaps    = 0;
    bool improved = true;
    while (improved) improved = twoOptPass(route, adj, swaps);
    return route;
}

inline double getRouteDistance(std::vector<int>& route, Adj& adj) {
    double dist = 0.0;
    for (int i = 0; i + 1 < (int)route.size(); ++i)
        dist += adj[route[i]][route[i + 1]];
    return dist;
}

// Runs MST (Prim) + DFS traversal + 2-opt on a pre-built n×n distance matrix.
// Returns the optimised tour starting and ending at node 0.
inline RouteResult optimizeRoute(Adj& adj, int n) {
    std::vector<Edge> mst   = buildMST(adj, n);
    std::vector<int>  route = getRoute(mst, n);
    std::vector<int>  opt   = twoOpt(route, adj);
    opt.push_back(0);
    return {opt, getRouteDistance(opt, adj)};
}
