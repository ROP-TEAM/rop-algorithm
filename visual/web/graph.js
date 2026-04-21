'use strict';

const GROUP_COLORS = ['#2196F3','#4CAF50','#FF9800','#F44336','#9C27B0','#00BCD4','#795548','#607D8B'];

const AppState = {
  nodes: [],
  edges: [],
  groups: [],
  nextNodeId: 0,
  nextEdgeId: 0,
  nextGroupId: 0,
  showCoords: false,
};

function seededRandom(seed) {
  let s = seed >>> 0;
  return function () {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return ((s >>> 0) / 4294967296);
  };
}

function euclidean(a, b) {
  return Math.round(Math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2) * 10) / 10;
}

function createGroup(name, color) {
  const g = { id: AppState.nextGroupId++, name, color, vehicleCapacity: 30 };
  AppState.groups.push(g);
  return g;
}

function createNode(x, y, isDepot, demandMin, demandMax, rand) {
  const r = rand || Math.random;
  const demand = demandMin + Math.floor(r() * (demandMax - demandMin + 1));
  const node = {
    id: AppState.nextNodeId++,
    x, y,
    label: String(AppState.nextNodeId - 1),
    demand,
    isDepot: !!isDepot,
    groupId: null,
  };
  AppState.nodes.push(node);
  return node;
}

function createEdge(fromId, toId, weightOverride) {
  const a = AppState.nodes.find(n => n.id === fromId);
  const b = AppState.nodes.find(n => n.id === toId);
  if (!a || !b || fromId === toId) return null;
  const exists = AppState.edges.find(e => e.from === fromId && e.to === toId);
  if (exists) return null;
  const weight = weightOverride !== undefined ? weightOverride : euclidean(a, b);
  const edge = { id: AppState.nextEdgeId++, from: fromId, to: toId, weight };
  AppState.edges.push(edge);
  return edge;
}

function removeNode(id) {
  AppState.nodes = AppState.nodes.filter(n => n.id !== id);
  AppState.edges = AppState.edges.filter(e => e.from !== id && e.to !== id);
}

function removeEdge(id) {
  AppState.edges = AppState.edges.filter(e => e.id !== id);
}

function assignNodeToGroup(nodeId, groupId) {
  const node = AppState.nodes.find(n => n.id === nodeId);
  if (node) node.groupId = groupId;
}

function getGroupStats(groupId) {
  const members = AppState.nodes.filter(n => n.groupId === groupId && !n.isDepot);
  const totalDemand = members.reduce((s, n) => s + n.demand, 0);
  const groupEdges = AppState.edges.filter(e => {
    const a = AppState.nodes.find(n => n.id === e.from);
    const b = AppState.nodes.find(n => n.id === e.to);
    return a && b && a.groupId === groupId && b.groupId === groupId;
  });
  const totalWeight = groupEdges.reduce((s, e) => s + e.weight, 0);
  return { totalDemand, totalWeight: Math.round(totalWeight * 10) / 10, nodeCount: members.length };
}

function generateGraph(cfg) {
  AppState.nodes = [];
  AppState.edges = [];
  AppState.groups = [];
  AppState.nextNodeId = 0;
  AppState.nextEdgeId = 0;
  AppState.nextGroupId = 0;
  AppState.showCoords = cfg.showCoords;

  const rand = cfg.seed !== null ? seededRandom(cfg.seed) : Math.random.bind(Math);
  const L = cfg.gridSize;

  for (let i = 0; i < cfg.numGroups; i++) {
    const g = createGroup('Route ' + (i + 1), GROUP_COLORS[i % GROUP_COLORS.length]);
    g.vehicleCapacity = cfg.vehicleCapacity;
  }

  const depotX = cfg.depotCenter ? 0 : (rand() * 2 - 1) * L * 0.8;
  const depotY = cfg.depotCenter ? 0 : (rand() * 2 - 1) * L * 0.8;
  createNode(Math.round(depotX), Math.round(depotY), true, 0, 0, rand);

  const positions = generatePositions(cfg.numNodes, L, cfg.clustered, rand);
  positions.forEach(([x, y]) => createNode(x, y, false, cfg.demandMin, cfg.demandMax, rand));

  if (cfg.autoAssign) {
    const deliveryNodes = AppState.nodes.filter(n => !n.isDepot);
    deliveryNodes.forEach((n, i) => { n.groupId = AppState.groups[i % AppState.groups.length].id; });
  }

  if (cfg.edgeMode === 'full') {
    const ns = AppState.nodes;
    for (let i = 0; i < ns.length; i++)
      for (let j = i + 1; j < ns.length; j++)
        createEdge(ns[i].id, ns[j].id);
  } else if (cfg.edgeMode === 'sparse') {
    const ns = AppState.nodes;
    for (let i = 0; i < ns.length; i++) {
      for (let j = i + 1; j < ns.length; j++) {
        if (rand() < 0.4) {
          const dist = euclidean(ns[i], ns[j]);
          const noise = 1 + (rand() * 2 - 1) * cfg.noiseFactor;
          createEdge(ns[i].id, ns[j].id, Math.round(dist * noise * 10) / 10);
        }
      }
    }
  }
}

function generatePositions(count, L, clustered, rand) {
  if (!clustered) {
    return Array.from({ length: count }, () => [
      Math.round((rand() * 2 - 1) * L * 0.9),
      Math.round((rand() * 2 - 1) * L * 0.9),
    ]);
  }
  const numClusters = Math.max(2, Math.min(4, Math.floor(count / 3)));
  const centers = Array.from({ length: numClusters }, () => [
    (rand() * 2 - 1) * L * 0.6,
    (rand() * 2 - 1) * L * 0.6,
  ]);
  return Array.from({ length: count }, (_, i) => {
    const [cx, cy] = centers[i % numClusters];
    return [
      Math.round(cx + (rand() * 2 - 1) * L * 0.25),
      Math.round(cy + (rand() * 2 - 1) * L * 0.25),
    ];
  });
}

function exportJSON() {
  const data = JSON.stringify({ nodes: AppState.nodes, edges: AppState.edges, groups: AppState.groups }, null, 2);
  const blob = new Blob([data], { type: 'application/json' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'cvrp-graph.json';
  a.click();
}
