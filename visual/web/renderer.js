'use strict';

const Renderer = (() => {
  const NS = 'http://www.w3.org/2000/svg';
  let svg, root, layerEdges, layerDraft, layerNodes, gridBg;
  let viewBox = { x: 0, y: 0, scale: 1 };
  let gridSize = 20;

  const NODE_R_BASE  = 1.8;
  const DEPOT_R_BASE = 2.2;

  function nodeR()   { return Math.max(6,  Math.min(18, NODE_R_BASE  * viewBox.scale)); }
  function depotR()  { return Math.max(8,  Math.min(22, DEPOT_R_BASE * viewBox.scale)); }
  function handleR() { return nodeR() + 10; }

  function init() {
    svg       = document.getElementById('canvas');
    root      = document.getElementById('canvas-root');
    layerEdges = document.getElementById('layer-edges');
    layerDraft = document.getElementById('layer-draft');
    layerNodes = document.getElementById('layer-nodes');
    gridBg    = document.getElementById('grid-bg');
    resizeGrid();
    window.addEventListener('resize', resizeGrid);
  }

  function resizeGrid() {
    const w = svg.clientWidth || window.innerWidth - 260;
    const h = svg.clientHeight || window.innerHeight;
    gridBg.setAttribute('width', w);
    gridBg.setAttribute('height', h);
  }

  function setGridSize(L) { gridSize = L; }

  function worldToScreen(wx, wy) {
    const cx = svg.clientWidth / 2;
    const cy = svg.clientHeight / 2;
    return [
      cx + (wx + viewBox.x) * viewBox.scale,
      cy - (wy - viewBox.y) * viewBox.scale,
    ];
  }

  function screenToWorld(sx, sy) {
    const cx = svg.clientWidth / 2;
    const cy = svg.clientHeight / 2;
    return [
      (sx - cx) / viewBox.scale - viewBox.x,
      -((sy - cy) / viewBox.scale - viewBox.y),
    ];
  }

  function el(tag, attrs) {
    const e = document.createElementNS(NS, tag);
    for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
    return e;
  }

  function ensureArrowMarker(color, id) {
    const defs = svg.querySelector('defs');
    if (defs.querySelector('#' + id)) return;
    const marker = el('marker', { id, markerWidth: '8', markerHeight: '6',
      refX: '6', refY: '3', orient: 'auto' });
    const poly = el('polygon', { points: '0 0, 8 3, 0 6', fill: color });
    marker.appendChild(poly);
    defs.appendChild(marker);
  }

  function groupColor(groupId) {
    if (groupId === null || groupId === undefined) return '#9E9E9E';
    const g = AppState.groups.find(g => g.id === groupId);
    return g ? g.color : '#9E9E9E';
  }

  function render() {
    layerEdges.innerHTML = '';
    layerNodes.innerHTML = '';
    renderGridTransform();
    renderAxes();
    renderEdges();
    renderNodes();
  }

  function renderGridTransform() {
    const period = Math.max(4, 5 * viewBox.scale);
    const [ox, oy] = worldToScreen(0, 0);
    const offX = ((ox % period) + period) % period;
    const offY = ((oy % period) + period) % period;
    const pattern = svg.querySelector('#grid-pattern');
    pattern.setAttribute('width', period);
    pattern.setAttribute('height', period);
    pattern.setAttribute('patternTransform', `translate(${offX}, ${offY})`);
    const path = pattern.querySelector('path');
    path.setAttribute('d', `M ${period} 0 L 0 0 0 ${period}`);
  }

  function renderAxes() {
    const existing = root.querySelectorAll('.axis');
    existing.forEach(e => e.remove());
    const L = gridSize;
    const step = 5;
    for (let v = -L; v <= L; v += step) {
      const [sx, sy] = worldToScreen(v, 0);
      const [sx2, sy2] = worldToScreen(0, v);
      addAxisTick(sx, sy, String(v), 'x');
      if (v !== 0) addAxisTick(sx2, sy2, String(v), 'y');
    }
    const [ox, oy] = worldToScreen(0, 0);
    const originTxt = el('text', { x: ox - 14, y: oy + 14, class: 'axis-label', fill: '#888', 'font-size': '10' });
    originTxt.textContent = '0';
    originTxt.classList.add('axis');
    root.appendChild(originTxt);
  }

  function addAxisTick(sx, sy, label, axis) {
    const t = el('text', {
      x: axis === 'x' ? sx      : sx - 18,
      y: axis === 'x' ? sy + 14 : sy + 4,
      class: 'axis-label axis', fill: '#aaa', 'font-size': '9', 'text-anchor': 'middle',
    });
    t.textContent = label;
    root.appendChild(t);
  }

  function renderEdges() {
    AppState.edges.forEach(edge => {
      const a = AppState.nodes.find(n => n.id === edge.from);
      const b = AppState.nodes.find(n => n.id === edge.to);
      if (!a || !b) return;
      const color = edgeColor(edge);
      const markerId = 'arrow-' + color.replace('#', '');
      ensureArrowMarker(color, markerId);

      const [x1, y1] = worldToScreen(a.x, a.y);
      const [x2, y2] = worldToScreen(b.x, b.y);
      const dx = x2 - x1, dy = y2 - y1;
      const len = Math.sqrt(dx * dx + dy * dy) || 1;
      const r = (b.isDepot ? depotR() : nodeR()) + 2;
      const ex = x2 - dx / len * r;
      const ey = y2 - dy / len * r;

      const line = el('line', {
        x1, y1, x2: ex, y2: ey,
        stroke: color, 'stroke-width': '2',
        'marker-end': `url(#${markerId})`,
        'data-edge-id': edge.id,
        class: 'edge',
      });
      line.addEventListener('contextmenu', e => { e.preventDefault(); showEdgeMenu(e, edge.id); });

      const mx = (x1 + x2) / 2;
      const my = (y1 + y2) / 2;
      const wLabel = el('text', {
        x: mx, y: my - 6, 'text-anchor': 'middle',
        fill: color, 'font-size': '10', 'font-weight': 'bold', class: 'edge-label',
      });
      wLabel.textContent = edge.weight;

      layerEdges.appendChild(line);
      layerEdges.appendChild(wLabel);
    });
  }

  function edgeColor(edge) {
    const a = AppState.nodes.find(n => n.id === edge.from);
    const b = AppState.nodes.find(n => n.id === edge.to);
    if (a && b && a.groupId !== null && a.groupId === b.groupId) return groupColor(a.groupId);
    return '#BDBDBD';
  }

  function renderNodes() {
    AppState.nodes.forEach(node => {
      const [cx, cy] = worldToScreen(node.x, node.y);
      const r = node.isDepot ? depotR() : nodeR();
      const color = node.isDepot ? '#000' : groupColor(node.groupId);
      const fillColor = node.isDepot ? '#fff' : (node.groupId !== null ? color + '22' : '#fff');

      const g = el('g', { class: 'node-group', 'data-node-id': node.id });

      if (!node.isDepot) {
        const handle = el('circle', {
          cx, cy, r: handleR(),
          fill: 'transparent', stroke: color, 'stroke-width': '1.5',
          'stroke-dasharray': '3 3', class: 'node-handle', opacity: '0',
        });
        g.appendChild(handle);
      }

      const circle = el('circle', {
        cx, cy, r,
        fill: fillColor, stroke: color,
        'stroke-width': node.isDepot ? '3' : '2',
        class: 'node-body',
      });
      g.appendChild(circle);

      const label = el('text', {
        x: cx, y: cy + 1,
        'text-anchor': 'middle', 'dominant-baseline': 'middle',
        fill: node.isDepot ? '#000' : '#333',
        'font-size': node.isDepot ? '13' : '11',
        'font-weight': node.isDepot ? 'bold' : 'normal',
        class: 'node-label',
      });
      label.textContent = node.isDepot ? '0' : node.label;
      g.appendChild(label);

      const demandLabel = el('text', {
        x: cx + r - 2, y: cy - r + 2,
        'text-anchor': 'middle', 'dominant-baseline': 'middle',
        fill: color, 'font-size': '8', 'font-weight': 'bold', class: 'demand-badge',
      });
      demandLabel.textContent = node.demand;
      g.appendChild(demandLabel);

      if (AppState.showCoords) {
        const coordTxt = el('text', {
          x: cx, y: cy + r + 12,
          'text-anchor': 'middle', fill: '#888', 'font-size': '8',
        });
        coordTxt.textContent = `(${node.x},${node.y})`;
        g.appendChild(coordTxt);
      }

      layerNodes.appendChild(g);
    });
  }

  function renderDraftEdge(x1, y1, x2, y2) {
    layerDraft.innerHTML = '';
    const line = el('line', {
      x1, y1, x2, y2,
      stroke: '#999', 'stroke-width': '2', 'stroke-dasharray': '5 4',
    });
    layerDraft.appendChild(line);
  }

  function clearDraft() { layerDraft.innerHTML = ''; }

  function getViewBox() { return viewBox; }
  function setViewBox(v) { viewBox = v; }
  function getNodeR() { return nodeR(); }
  function getHandleR() { return handleR(); }

  return { init, render, worldToScreen, screenToWorld, setGridSize,
           renderDraftEdge, clearDraft, getViewBox, setViewBox,
           getNodeR, getHandleR };
})();
