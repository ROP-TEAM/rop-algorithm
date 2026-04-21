'use strict';

const Interaction = (() => {
  let svg, ctxMenu;
  let edgeDrawState  = null;   // { fromNode }
  let panState       = null;   // { startSX, startSY, origVB }
  let moveFollowState = null;  // { node } — node tracks cursor until click

  function init() {
    svg     = document.getElementById('canvas');
    ctxMenu = document.getElementById('ctx-menu');

    svg.addEventListener('mousedown',   onMouseDown);
    svg.addEventListener('mousemove',   onMouseMove);
    svg.addEventListener('mouseup',     onMouseUp);
    svg.addEventListener('wheel',       onWheel, { passive: false });
    svg.addEventListener('auxclick',    e => e.preventDefault());
    svg.addEventListener('contextmenu', e => e.preventDefault());
    svg.addEventListener('selectstart', e => e.preventDefault());
    svg.addEventListener('dragstart',   e => e.preventDefault());
    document.addEventListener('keydown', onKeyDown);
    document.addEventListener('mousedown', e => {
      if (!ctxMenu.contains(e.target)) hideCtxMenu();
    });
  }

  function svgPoint(e) {
    const r = svg.getBoundingClientRect();
    return [e.clientX - r.left, e.clientY - r.top];
  }

  function findNodeAt(sx, sy) {
    for (const node of AppState.nodes) {
      const [cx, cy] = Renderer.worldToScreen(node.x, node.y);
      if (Math.sqrt((sx - cx) ** 2 + (sy - cy) ** 2) <= Renderer.getHandleR()) return node;
    }
    return null;
  }

  // ── Mouse handlers ────────────────────────────────────────────────

  function onMouseDown(e) {
    // Middle → pan
    if (e.button === 1) {
      e.preventDefault();
      const [sx, sy] = svgPoint(e);
      panState = { startSX: sx, startSY: sy, origVB: { ...Renderer.getViewBox() } };
      return;
    }

    // Right → context menu
    if (e.button === 2) {
      e.preventDefault();
      e.stopPropagation();
      const [sx, sy] = svgPoint(e);
      const node = findNodeAt(sx, sy);
      if (node) { showNodeMenu(e.clientX, e.clientY, node); return; }
      const edgeEl = e.target.closest('.edge');
      if (edgeEl) showEdgeMenu(e.clientX, e.clientY, parseInt(edgeEl.getAttribute('data-edge-id')));
      return;
    }

    // Left
    if (e.button === 0) {
      hideCtxMenu();
      const [sx, sy] = svgPoint(e);

      // Place node if in move-follow mode
      if (moveFollowState) {
        const [wx, wy] = Renderer.screenToWorld(sx, sy);
        moveFollowState.node.x = Math.round(wx);
        moveFollowState.node.y = Math.round(wy);
        exitMoveFollow();
        Renderer.render();
        UI.renderPanel();
        return;
      }

      const node = findNodeAt(sx, sy);
      if (node) {
        edgeDrawState = { fromNode: node };
        return;
      }
      panState = { startSX: sx, startSY: sy, origVB: { ...Renderer.getViewBox() } };
    }
  }

  function onMouseMove(e) {
    const [sx, sy] = svgPoint(e);

    if (moveFollowState) {
      const [wx, wy] = Renderer.screenToWorld(sx, sy);
      moveFollowState.node.x = Math.round(wx);
      moveFollowState.node.y = Math.round(wy);
      Renderer.render();
      UI.renderPanel();
      return;
    }
    if (edgeDrawState) {
      const [nx, ny] = Renderer.worldToScreen(edgeDrawState.fromNode.x, edgeDrawState.fromNode.y);
      Renderer.renderDraftEdge(nx, ny, sx, sy);
      highlightTarget(sx, sy);
      return;
    }
    if (panState) {
      const vb = Renderer.getViewBox();
      const dx = (sx - panState.startSX) / vb.scale;
      const dy = (sy - panState.startSY) / vb.scale;
      Renderer.setViewBox({ ...panState.origVB, x: panState.origVB.x + dx, y: panState.origVB.y + dy });
      Renderer.render();
      return;
    }
    showHandleOnHover(sx, sy);
  }

  function onMouseUp(e) {
    if (edgeDrawState) {
      Renderer.clearDraft();
      const [sx, sy] = svgPoint(e);
      const target = findNodeAt(sx, sy);
      if (target && target.id !== edgeDrawState.fromNode.id) {
        const edge = createEdge(edgeDrawState.fromNode.id, target.id);
        if (edge) {
          const override = prompt(`Edge weight (default ${edge.weight}):`, edge.weight);
          if (override !== null && override !== '') {
            const val = parseFloat(override);
            if (!isNaN(val) && val >= 0) edge.weight = val;
          }
          Renderer.render();
          UI.renderPanel();
        }
      }
      edgeDrawState = null;
      return;
    }
    panState = null;
  }

  function onWheel(e) {
    e.preventDefault();
    const factor = e.deltaY < 0 ? 1.1 : 0.9;
    const vb = Renderer.getViewBox();
    Renderer.setViewBox({ ...vb, scale: Math.min(100, Math.max(0.1, vb.scale * factor)) });
    Renderer.render();
  }

  function onKeyDown(e) {
    if (e.key === 'Escape' && moveFollowState) exitMoveFollow();
  }

  // ── Move-follow mode ──────────────────────────────────────────────

  function enterMoveFollow(node) {
    moveFollowState = { node };
    svg.style.cursor = 'move';
  }

  function exitMoveFollow() {
    moveFollowState = null;
    svg.style.cursor = '';
  }

  // ── Hover helpers ────────────────────────────────────────────────

  function showHandleOnHover(sx, sy) {
    const node = findNodeAt(sx, sy);
    document.querySelectorAll('.node-handle').forEach(h => {
      const id = parseInt(h.closest('[data-node-id]')?.getAttribute('data-node-id'));
      h.setAttribute('opacity', (node && node.id === id) ? '1' : '0');
    });
  }

  function highlightTarget(sx, sy) {
    const node = findNodeAt(sx, sy);
    document.querySelectorAll('.node-handle').forEach(h => {
      const id = parseInt(h.closest('[data-node-id]')?.getAttribute('data-node-id'));
      const isTarget = node && node.id === id && node.id !== edgeDrawState?.fromNode?.id;
      h.setAttribute('opacity', isTarget ? '1' : '0');
    });
  }

  // ── Context menus ────────────────────────────────────────────────

  function showNodeMenu(clientX, clientY, node) {
    hideCtxMenu();
    const items = [];
    if (!node.isDepot) {
      items.push({ label: 'Assign to group ▶', sub: AppState.groups.map(g => ({
        label: `<span class="color-dot" style="background:${g.color}"></span> ${g.name}`,
        action: () => { assignNodeToGroup(node.id, g.id); Renderer.render(); UI.renderPanel(); },
      })) });
      items.push({ label: 'Unassign group', action: () => {
        assignNodeToGroup(node.id, null); Renderer.render(); UI.renderPanel();
      }});
    }
    items.push({ label: 'Move', action: () => enterMoveFollow(node) });
    if (!node.isDepot) {
      items.push({ label: 'Edit demand…', action: () => {
        const v = prompt('Node demand:', node.demand);
        if (v !== null) { const n = parseInt(v); if (!isNaN(n) && n >= 0) { node.demand = n; Renderer.render(); UI.renderPanel(); } }
      }});
    }
    items.push({ label: 'Delete node', action: () => { removeNode(node.id); Renderer.render(); UI.renderPanel(); }, danger: true });
    renderCtxMenu(clientX, clientY, items);
  }

  function showEdgeMenu(clientX, clientY, edgeId) {
    hideCtxMenu();
    const edge = AppState.edges.find(ed => ed.id === edgeId);
    if (!edge) return;
    renderCtxMenu(clientX, clientY, [
      { label: `Edit weight (${edge.weight})…`, action: () => {
        const v = prompt('Edge weight:', edge.weight);
        if (v !== null) { const n = parseFloat(v); if (!isNaN(n) && n >= 0) { edge.weight = n; Renderer.render(); UI.renderPanel(); } }
      }},
      { label: 'Delete edge', action: () => { removeEdge(edge.id); Renderer.render(); UI.renderPanel(); }, danger: true },
    ]);
  }

  function renderCtxMenu(x, y, items) {
    ctxMenu.innerHTML = '';
    items.forEach(item => {
      if (item.sub) {
        const wrap = document.createElement('div');
        wrap.className = 'ctx-item ctx-has-sub';
        wrap.innerHTML = item.label;
        const sub = document.createElement('div');
        sub.className = 'ctx-submenu';
        item.sub.forEach(s => {
          const si = document.createElement('div');
          si.className = 'ctx-item';
          si.innerHTML = s.label;
          si.addEventListener('click', ev => { ev.stopPropagation(); s.action(); hideCtxMenu(); });
          sub.appendChild(si);
        });
        wrap.appendChild(sub);
        ctxMenu.appendChild(wrap);
      } else {
        const div = document.createElement('div');
        div.className = 'ctx-item' + (item.danger ? ' danger' : '');
        div.textContent = item.label;
        div.addEventListener('click', ev => { ev.stopPropagation(); item.action(); hideCtxMenu(); });
        ctxMenu.appendChild(div);
      }
    });
    ctxMenu.style.left = x + 'px';
    ctxMenu.style.top = y + 'px';
    ctxMenu.classList.remove('hidden');
  }

  function hideCtxMenu() { ctxMenu.classList.add('hidden'); }

  return { init };
})();
