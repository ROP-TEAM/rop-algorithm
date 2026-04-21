'use strict';

const UI = (() => {
  function init() {
    setupInitDialog();
    setupToolbar();
    renderPanel();
  }

  function setupInitDialog() {
    const edgeRadios = document.querySelectorAll('input[name="edges"]');
    edgeRadios.forEach(r => r.addEventListener('change', () => {
      const isSparse = document.querySelector('input[name="edges"]:checked').value === 'sparse';
      document.getElementById('noise-label').classList.toggle('hidden', !isSparse);
      document.getElementById('noise-row').classList.toggle('hidden', !isSparse);
    }));

    const noiseSlider = document.getElementById('cfg-noise');
    noiseSlider.addEventListener('input', () => {
      document.getElementById('noise-val').textContent = parseFloat(noiseSlider.value).toFixed(2);
    });

    document.getElementById('btn-generate').addEventListener('click', () => {
      const seedInput = document.getElementById('cfg-seed').value;
      const cfg = {
        gridSize:       parseInt(document.getElementById('cfg-grid').value) || 20,
        numNodes:       parseInt(document.getElementById('cfg-nodes').value) || 10,
        depotCenter:    document.querySelector('input[name="depot"]:checked').value === 'center',
        demandMin:      parseInt(document.getElementById('cfg-demand-min').value) || 1,
        demandMax:      parseInt(document.getElementById('cfg-demand-max').value) || 10,
        numGroups:      parseInt(document.getElementById('cfg-groups').value) || 3,
        vehicleCapacity:parseInt(document.getElementById('cfg-vehicle-cap').value) || 30,
        edgeMode:       document.querySelector('input[name="edges"]:checked').value,
        noiseFactor:    parseFloat(document.getElementById('cfg-noise').value) || 0.3,
        clustered:      document.querySelector('input[name="cluster"]:checked').value === 'clustered',
        seed:           seedInput !== '' ? parseInt(seedInput) : null,
        showCoords:     document.getElementById('cfg-coords').checked,
        autoAssign:     document.getElementById('cfg-autoassign').checked,
      };
      generateGraph(cfg);
      Renderer.setGridSize(cfg.gridSize);
      Renderer.setViewBox({ x: 0, y: 0, scale: Math.min(14, Math.floor(380 / cfg.gridSize)) });
      document.getElementById('init-overlay').classList.add('hidden');
      Renderer.render();
      renderPanel();
    });
  }

  function setupToolbar() {
    document.getElementById('btn-new-graph').addEventListener('click', () => {
      document.getElementById('init-overlay').classList.remove('hidden');
    });
    document.getElementById('btn-add-node').addEventListener('click', () => {
      const L = 20;
      const x = Math.round((Math.random() * 2 - 1) * L * 0.85);
      const y = Math.round((Math.random() * 2 - 1) * L * 0.85);
      createNode(x, y, false, 1, 10);
      Renderer.render();
      renderPanel();
    });
    document.getElementById('btn-add-depot').addEventListener('click', () => {
      if (AppState.nodes.some(n => n.isDepot)) { alert('A depot already exists.'); return; }
      createNode(0, 0, true, 0, 0);
      Renderer.render();
      renderPanel();
    });
    document.getElementById('btn-export').addEventListener('click', exportJSON);
  }

  function renderPanel() {
    const body = document.getElementById('panel-body');
    body.innerHTML = '';

    const groupsSection = document.createElement('div');
    groupsSection.className = 'section';

    const groupHeader = document.createElement('div');
    groupHeader.className = 'section-header';
    groupHeader.innerHTML = '<span>Route Groups</span>';
    const addGroupBtn = document.createElement('button');
    addGroupBtn.textContent = '+';
    addGroupBtn.className = 'btn-small';
    addGroupBtn.title = 'Add group';
    addGroupBtn.addEventListener('click', () => {
      const name = prompt('Group name:', 'Route ' + (AppState.groups.length + 1));
      if (!name) return;
      createGroup(name, GROUP_COLORS[AppState.groups.length % GROUP_COLORS.length]);
      renderPanel();
    });
    groupHeader.appendChild(addGroupBtn);
    groupsSection.appendChild(groupHeader);

    AppState.groups.forEach(g => {
      const stats = getGroupStats(g.id);
      const row = document.createElement('div');
      row.className = 'group-row';
      row.innerHTML = `
        <span class="color-dot" style="background:${g.color}"></span>
        <span class="group-name">${g.name}</span>
        <span class="group-stats">${stats.totalDemand}/${g.vehicleCapacity} cap · ${stats.totalWeight} dist</span>
      `;

      const bar = document.createElement('div');
      bar.className = 'cap-bar';
      const fill = document.createElement('div');
      fill.className = 'cap-fill';
      const ratio = Math.min(1, stats.totalDemand / (g.vehicleCapacity || 1));
      fill.style.width = (ratio * 100) + '%';
      fill.style.background = ratio >= 1 ? '#F44336' : g.color;
      bar.appendChild(fill);
      row.appendChild(bar);

      const nodeList = document.createElement('div');
      nodeList.className = 'node-list';
      AppState.nodes.filter(n => n.groupId === g.id && !n.isDepot).forEach(n => {
        const nrow = document.createElement('div');
        nrow.className = 'node-row';
        nrow.textContent = `#${n.label}  demand:${n.demand}  (${n.x},${n.y})`;
        nodeList.appendChild(nrow);
      });
      row.appendChild(nodeList);
      groupsSection.appendChild(row);
    });

    body.appendChild(groupsSection);

    const unassigned = AppState.nodes.filter(n => n.groupId === null && !n.isDepot);
    if (unassigned.length > 0) {
      const sec = document.createElement('div');
      sec.className = 'section';
      sec.innerHTML = '<div class="section-header"><span>Unassigned nodes</span></div>';
      unassigned.forEach(n => {
        const nrow = document.createElement('div');
        nrow.className = 'node-row unassigned';
        nrow.textContent = `#${n.label}  demand:${n.demand}  (${n.x},${n.y})`;
        sec.appendChild(nrow);
      });
      body.appendChild(sec);
    }

    const depot = AppState.nodes.find(n => n.isDepot);
    if (depot) {
      const sec = document.createElement('div');
      sec.className = 'section';
      sec.innerHTML = `<div class="section-header"><span>Depot</span></div>
        <div class="node-row">#0  (${depot.x},${depot.y})</div>`;
      body.appendChild(sec);
    }
  }

  return { init, renderPanel };
})();

window.addEventListener('DOMContentLoaded', () => {
  Renderer.init();
  Interaction.init();
  UI.init();
});
