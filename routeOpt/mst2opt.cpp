#include <bits/stdc++.h>

using namespace std;

using Edge  = std::pair<int,int>;
using Adj = vector<vector<double>>;

constexpr double DOUBLE_MAX = std::numeric_limits<double>::max();

struct Point {
  double x,y;
};

void separator(const std::string& title = "") {
  if (title.empty()) { std::cout << std::string(52, '-') << "\n"; return; }
  int pad = (50 - (int)title.size()) / 2;
  std::cout << std::string(pad, '-') << " " << title << " "
    << std::string(pad, '-') << "\n";
}

double getDistance(Point a, Point b) {
  double dx = a.x - b.x, dy = a.y - b.y;
  return sqrt(dx*dx + dy*dy);
}

vector<Edge> buildMST(Adj& adj, int n) {
  priority_queue<pair<double, int>, vector<pair<double, int>>, greater<>> pq;
  vector<bool> visit(n, false);
  vector<int>  parent(n, -1);   // track where each vertex came from
  vector<double>  edgeW(n, DOUBLE_MAX);     // track the edge weight used to reach it
  vector<Edge> ans;

  edgeW[0] = 0.0;
  pq.push({0,0}); // {weight, node}

while(!pq.empty()) {

  auto u = pq.top().second;
  pq.pop();

  if(visit[u]) continue;
  visit[u] = 1;

  if (parent[u] != -1) ans.push_back({parent[u], u});

  for(int v=0;v<n;v++) {
    double w = adj[u][v];
    if(!visit[v] && w < edgeW[v]) {
      parent[v] = u;
      edgeW[v]  = adj[u][v];
      pq.push({w,v});
    }
  }
}
return ans;
}

void dfs(int u, vector<vector<int>>& tree, vector<bool>& visit, vector<int>& ans) {
  visit[u] = 1;
  ans.push_back(u);
  for(auto v : tree[u]) {
    if(!visit[v]) dfs(v, tree, visit, ans);
  }
}

vector<int> getRoute(vector<Edge>& MST,int n) {
  vector<bool> visit(n,0);
  vector<int> ans;

  vector<vector<int>> tree(n);
  for(auto [u,v] : MST) {
    tree[u].push_back(v);
    tree[v].push_back(u);
  }

  dfs(0, tree, visit, ans);

  return ans;
}

void applySwap(vector<int>& route, int i, int j) {
  std::reverse(route.begin() + i + 1, route.begin() + j + 1);
}

bool twoOptPass(vector<int>& route, Adj& adj, int& swapCount) {
  int n = (int)route.size();
  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 2; j < n; j++) {
      if (i == 0 && j == n - 1) continue;  // would be a no-op

      int a = route[i],     b = route[i+1];
      int c = route[j],     d = route[(j+1) % n];

      double before = adj[a][b] + adj[c][d];
      double after = adj[a][c] + adj[b][d];

      if (after < before - 1e-9) {
        applySwap(route, i, j);
        swapCount++;
        return true;   // restart outer loop
      }
    }
  }
  return false;
}

vector<int> twoOpt(vector<int> route, Adj& adj) {
  int  swapCount = 0;
  bool improved  = true;

  while (improved)
    improved = twoOptPass(route, adj, swapCount);

  // std::cout << "  Total 2-opt swaps applied: " << swapCount << "\n";
  return route;
}

double getRouteDistance(vector<int>& route, Adj& adj) {
  double ans = 0.0;
  int n = route.size();
  for(int i=0;i<n-1;i++) ans += adj[route[i]][route[i+1]];
  return ans;
}

int main() {

  vector<Point> nodes = {
    {100, 100},   // 0  depot
    {300, 150},   // 1
    {500,  80},   // 2
    {450, 300},   // 3
    {600, 450},   // 4
    {350, 500},   // 5
    {150, 450},   // 6
    { 80, 300},   // 7
    {250, 350},   // 8
    {400, 220},   // 9
    {180, 200},   // 10
    {520, 350},   // 11
  };

  int n = nodes.size();
  Adj adj(n, vector<double>(n, 0.0));
  for(int i=0;i<n;i++) 
    for(int j=i+1;j<n;j++) {
      double dist = getDistance(nodes[i], nodes[j]);
      adj[i][j] = dist;
      adj[j][i] = dist;
    }

  // first step : building a MST
  // I'm using prim's algortihm
  vector<Edge> MST = buildMST(adj, n);

  separator("Prim's MST");
  for(auto [u,v] : MST) cout << u << " -> " << v << " / weight : " << adj[u][v] << '\n';
  separator();

  // second step : tranverse the tree and create a route
  // this step would not give an optimal result which is way we need 2-opt
  vector<int> route = getRoute(MST, n);
  cout << '\n';
  separator("create route");
  for(int i=0;i<n;i++) cout << route[i] << " -> ";
  cout << "0\n";
  separator();

  // third step : opt route
  vector<int> optRoute = twoOpt(route, adj);
  optRoute.push_back(0);
  cout << '\n';
  separator("2-opt route");
  cout << "route distance : " << getRouteDistance(optRoute, adj) << '\n';
  for(int i=0;i<n+1;i++) cout << optRoute[i] << (i!=n?" -> ":"\n");
  separator();

}
