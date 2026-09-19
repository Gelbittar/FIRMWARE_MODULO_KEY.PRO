const AppOperator = (function() {
  'use strict';

  let currentModule = null;
  let currentRole = 'installer';

  async function init() {
    setupTabs();
    setupBottomNav();
    setupEventListeners();
    await BiometricAuth.init();
    loadSavedModules();
    showLogin();
  }

  function setupTabs() {
    document.querySelectorAll('.tab-btn').forEach(btn => {
      btn.addEventListener('click', () => switchTab(btn.dataset.tab));
    });
    document.querySelectorAll('.sub-tab-btn').forEach(btn => {
      btn.addEventListener('click', () => switchSubTab(btn.dataset.sub));
    });
  }

  function setupBottomNav() {
    document.querySelectorAll('.nav-item').forEach(btn => {
      btn.addEventListener('click', () => switchTab(btn.dataset.tab));
    });
  }

  function switchTab(tabId) {
    document.querySelectorAll('.tab-btn, .nav-item').forEach(b => {
      b.classList.toggle('active', b.dataset.tab === tabId);
      b.setAttribute('aria-selected', b.dataset.tab === tabId);
    });
    document.querySelectorAll('.tab').forEach(t => {
      t.classList.toggle('active', t.id === tabId);
    });
    if (tabId === 'tabSlots') loadSlots();
    if (tabId === 'tabDashboard') refreshStatus();
  }

  function switchSubTab(subId) {
    document.querySelectorAll('.sub-tab-btn').forEach(b => b.classList.toggle('active', b.dataset.sub === subId));
    document.querySelectorAll('.subview').forEach(v => v.style.display = v.id === subId ? 'block' : 'none');
  }

  function setupEventListeners() {
    document.getElementById('loginForm').addEventListener('submit', handleLogin);
    document.getElementById('btnDisconnect').addEventListener('click', disconnect);
    document.getElementById('btnSwitchModule').addEventListener('click', () => showModuleSelector());
    document.getElementById('btnOpenDoor').addEventListener('click', openDoor);
    document.getElementById('btnRefreshStatus').addEventListener('click', refreshStatus);
    document.getElementById('btnQuerySlot').addEventListener('click', loadSlots);
    document.getElementById('btnSetKey').addEventListener('click', saveKey);
    document.getElementById('btnLearnKey').addEventListener('click', startLearnKey);
    document.getElementById('btnFindFreeSlot').addEventListener('click', findFreeSlot);
    document.getElementById('btnProgRead').addEventListener('click', readKey);
    document.getElementById('slotFilter').addEventListener('input', GEYLCA.debounce(filterSlots, 300));
    document.getElementById('slotOrder').addEventListener('change', sortSlots);

    GEYLCA.on('connected', onConnected);
    GEYLCA.on('event', onEvent);
    GEYLCA.on('statusResp', onStatusResp);
  }

  function loadSavedModules() {
    const modules = JSON.parse(localStorage.getItem('geylca_modules') || '[]');
    const selector = document.getElementById('moduleSelector');
  }

  function showLogin() {
    document.getElementById('loginOverlay').style.display = 'flex';
    document.getElementById('appContainer').style.display = 'none';
    document.getElementById('pinInput').focus();
  }

  async function handleLogin(e) {
    e.preventDefault();
    const pin = document.getElementById('pinInput').value;
    const useBio = document.getElementById('bioLogin').checked;

    if (!GEYLCA.isValidPIN(pin)) {
      GEYLCA.showToast('PIN debe ser 6 dígitos', 'error');
      return;
    }

    const savedModules = JSON.parse(localStorage.getItem('geylca_modules') || '[]');
    if (savedModules.length === 0) {
      GEYLCA.showToast('Primero agrega un módulo en "Cambiar módulo"', 'warning');
      showModuleSelector();
      return;
    }

    const module = savedModules[0];
    try {
      const token = await generateToken(module, 'installer', pin);
      if (useBio) {
        try {
          await BiometricAuth.enableBiometric(module.id, { role: 'installer', pin });
        } catch (bioErr) {
          GEYLCA.showToast('Biometría no disponible: ' + bioErr.message, 'warning');
        }
      }
      await loginModule(module, 'installer', pin, token);
    } catch (err) {
      GEYLCA.showToast('Error: ' + err.message, 'error');
    }
  }

  async function generateToken(module, role, pin) {
    const timestamp = Math.floor(Date.now() / 1000);
    const action = 'verify_role';
    const rawData = action + timestamp;
    return GEYLCA.calcularHMAC(rawData, pin);
  }

  async function loginModule(module, role, pin, token) {
    GEYLCA.showToast('Conectando...', 'info');
    try {
      await GEYLCA.connectMQTT(module, role, token);
      currentModule = module;
      currentRole = role;
      document.getElementById('loginOverlay').style.display = 'none';
      document.getElementById('appContainer').style.display = 'flex';
      document.getElementById('headerDevice').textContent = module.name || module.id;
      refreshStatus();
      loadSlots();
    } catch (err) {
      throw new Error('No se pudo conectar: ' + err.message);
    }
  }

  function onConnected(module) {
    GEYLCA.showToast(`Conectado a ${module.name || module.id}`, 'success');
  }

  function onEvent(event) {
    // Events handled in core.js for table
  }

  function onStatusResp(data) {
    if (data.max_slots !== undefined) {
      updateDashboard(data);
    }
  }

  function disconnect() {
    if (GEYLCA.mqttClient) {
      GEYLCA.mqttClient.end(true);
    }
    currentModule = null;
    showLogin();
    GEYLCA.showToast('Desconectado', 'info');
  }

  async function refreshStatus() {
    if (!GEYLCA.connected) return;
    document.getElementById('btnRefreshStatus').disabled = true;
    try {
      await GEYLCA.sendCommand('get_device_info');
      await GEYLCA.sendCommand('get_free_slots');
    } catch (e) {
      GEYLCA.showToast('Error: ' + e.message, 'error');
    } finally {
      document.getElementById('btnRefreshStatus').disabled = false;
    }
  }

  function updateDashboard(data) {
    document.getElementById('statConn').innerHTML = '<span class="status-dot on"></span> ONLINE';
    document.getElementById('statTotal').textContent = data.max_slots || '--';
    document.getElementById('statFree').textContent = data.free_slots || '--';
    document.getElementById('statSecMode').textContent = data.security_mode === 3 ? 'SÓVICA' : data.security_mode + ' bytes';
    document.getElementById('btnOpenDoor').disabled = false;
    document.getElementById('btnRefreshStatus').disabled = false;
    document.getElementById('btnQuerySlot').disabled = false;
    document.getElementById('btnSetKey').disabled = false;
    document.getElementById('btnLearnKey').disabled = false;
    document.getElementById('btnFindFreeSlot').disabled = false;
    document.getElementById('btnProgRead').disabled = false;
  }

  async function openDoor() {
    try {
      await GEYLCA.sendCommand('open');
      GEYLCA.showToast('Puerta abierta', 'success');
    } catch (e) {
      GEYLCA.showToast('Error: ' + e.message, 'error');
    }
  }

  async function loadSlots() {
    if (!GEYLCA.connected) return;
    document.getElementById('btnQuerySlot').disabled = true;
    document.getElementById('slotTableBody').innerHTML = '<tr><td colspan="5" class="empty-msg">Cargando...</td></tr>';
    try {
      await GEYLCA.sendCommand('get_slots');
    } catch (e) {
      document.getElementById('slotTableBody').innerHTML = '<tr><td colspan="5" class="empty-msg">Error: ' + e.message + '</td></tr>';
    } finally {
      document.getElementById('btnQuerySlot').disabled = false;
    }
  }

  function renderSlots(slots) {
    const tbody = document.getElementById('slotTableBody');
    if (!slots || slots.length === 0) {
      tbody.innerHTML = '<tr><td colspan="5" class="empty-msg">No hay slots programados</td></tr>';
      return;
    }
    tbody.innerHTML = slots.map(s => `
      <tr>
        <td>${s.slot}</td>
        <td>${s.apto || '-'}</td>
        <td><code>${s.key.substring(0, 16)}${s.key.length > 16 ? '...' : ''}</code></td>
        <td><span class="badge ${s.suspended ? 'badge-warning' : s.blk ? 'badge-danger' : 'badge-success'}">${s.suspended ? 'Suspendido' : s.blk ? 'Bloqueado' : 'Activo'}</span></td>
        <td>
          <button class="btn btn-ghost btn-sm btn-icon" onclick="editSlot(${s.slot}, '${s.apto || ''}', '${s.key}')" aria-label="Editar"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"/></svg></button>
          <button class="btn btn-ghost btn-sm btn-icon" onclick="clearSlot(${s.slot})" aria-label="Borrar"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/></svg></button>
        </td>
      </tr>
    `).join('');
  }

  window.editSlot = (slot, apto, key) => {
    document.getElementById('slotNum').value = slot;
    document.getElementById('slotApto').value = apto;
    document.getElementById('slotKey').value = key;
    switchSubTab('slotsView');
    document.getElementById('slotApto').focus();
  };

  window.clearSlot = async (slot) => {
    if (!await GEYLCA.showConfirm('Borrar slot', `¿Eliminar la key del slot ${slot}?`, 'Borrar', 'danger')) return;
    try {
      await GEYLCA.sendCommand('clear_slot', { slot });
      GEYLCA.showToast('Slot borrado', 'success');
      loadSlots();
    } catch (e) {
      GEYLCA.showToast('Error: ' + e.message, 'error');
    }
  };

  async function saveKey() {
    const slot = parseInt(document.getElementById('slotNum').value) || 0;
    const key = document.getElementById('slotKey').value.trim().toUpperCase();
    const apto = document.getElementById('slotApto').value.trim();
    if (!key) { GEYLCA.showToast('Key requerida', 'error'); return; }
    try {
      await GEYLCA.sendCommand('set_key', { slot, key, apto });
      GEYLCA.showToast('Key guardada', 'success');
      document.getElementById('slotNum').value = '';
      document.getElementById('slotKey').value = '';
      document.getElementById('slotApto').value = '';
      loadSlots();
    } catch (e) {
      GEYLCA.showToast('Error: ' + e.message, 'error');
    }
  }

  async function startLearnKey() {
    try {
      await GEYLCA.sendCommand('learn_key', { apto: document.getElementById('slotApto').value || 'Desconocido' });
      document.getElementById('learnWaitBox').style.display = 'block';
      GEYLCA.showToast('Presente la llave en el lector', 'info');
    } catch (e) {
      GEYLCA.showToast('Error: ' + e.message, 'error');
    }
  }

  async function findFreeSlot() {
    try {
      const resp = await GEYLCA.sendCommand('get_first_free_slot');
      if (resp.slot > 0) {
        document.getElementById('slotNum').value = resp.slot;
        GEYLCA.showToast('Slot libre: ' + resp.slot, 'success');
      } else {
        GEYLCA.showToast('No hay slots libres', 'warning');
      }
    } catch (e) {
      GEYLCA.showToast('Error: ' + e.message, 'error');
    }
  }

  async function readKey() {
    document.getElementById('progReadStatus').textContent = 'Leyendo...';
    try {
      const resp = await GEYLCA.sendCommand('learn_key', { slot: 0 });
      document.getElementById('progReadStatus').textContent = 'Presente la llave...';
    } catch (e) {
      document.getElementById('progReadStatus').textContent = 'Error: ' + e.message;
    }
  }

  function filterSlots() {
    const filter = document.getElementById('slotFilter').value.toLowerCase();
    document.querySelectorAll('#slotTableBody tr').forEach(tr => {
      const text = tr.textContent.toLowerCase();
      tr.style.display = text.includes(filter) ? '' : 'none';
    });
  }

  function sortSlots() {
    const order = document.getElementById('slotOrder').value;
    const tbody = document.getElementById('slotTableBody');
    const rows = Array.from(tbody.querySelectorAll('tr'));
    rows.sort((a, b) => {
      const aSlot = parseInt(a.cells[0].textContent) || 0;
      const bSlot = parseInt(b.cells[0].textContent) || 0;
      const aApto = a.cells[1].textContent;
      const bApto = b.cells[1].textContent;
      if (order === 'slot-asc') return aSlot - bSlot;
      if (order === 'slot-desc') return bSlot - aSlot;
      if (order === 'apto-asc') return aApto.localeCompare(bApto);
      return bApto.localeCompare(aApto);
    });
    rows.forEach(r => tbody.appendChild(r));
  }

  function showModuleSelector() {
    const modules = JSON.parse(localStorage.getItem('geylca_modules') || '[]');
    const html = modules.length ? modules.map(m => `
      <div class="card" style="margin-bottom:var(--space-3);cursor:pointer" onclick="selectModule('${m.id}')">
        <div class="card-body">
          <div style="display:flex;align-items:center;justify-content:space-between">
            <div><strong>${m.name}</strong><br><small style="color:var(--color-text-muted)">${m.id}</small></div>
            <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="9 18 15 12 9 6"/></svg>
          </div>
        </div>
      </div>
    `).join('') : '<p style="text-align:center;color:var(--color-text-muted)">No hay módulos guardados</p>';
    GEYLCA.showModal('Seleccionar Módulo', html + '<div class="btn-row" style="margin-top:var(--space-4)"><button class="btn btn-primary" onclick="addNewModule()">+ Agregar Módulo</button></div>');
  }

  window.selectModule = (id) => {
    const modules = JSON.parse(localStorage.getItem('geylca_modules') || '[]');
    const module = modules.find(m => m.id === id);
    if (module) {
      document.getElementById('headerDevice').textContent = module.name;
      currentModule = module;
      GEYLCA.showToast('Módulo seleccionado: ' + module.name, 'info');
    }
  };

  window.addNewModule = () => {
    GEYLCA.showModal('Agregar Módulo', `
      <div class="form-group"><label class="form-label">Device ID</label><input id="newModuleId" class="form-input" placeholder="mod_xxxxxxxx"></div>
      <div class="form-group"><label class="form-label">Nombre</label><input id="newModuleName" class="form-input" placeholder="Ej: Entrada Principal"></div>
      <div class="form-group"><label class="form-label">Secret (opcional)</label><input id="newModuleSecret" class="form-input" placeholder="Se genera automáticamente"></div>
    `, [
      { label: 'Cancelar', variant: 'ghost', value: false },
      { label: 'Guardar', variant: 'primary', value: true },
    ]).then(result => {
      if (result) {
        const id = document.getElementById('newModuleId').value.trim();
        const name = document.getElementById('newModuleName').value.trim();
        if (!id) { GEYLCA.showToast('Device ID requerido', 'error'); return; }
        const modules = JSON.parse(localStorage.getItem('geylca_modules') || '[]');
        modules.push({ id, name, secret: document.getElementById('newModuleSecret').value.trim() });
        localStorage.setItem('geylca_modules', JSON.stringify(modules));
        GEYLCA.showToast('Módulo guardado', 'success');
      }
    });
  };

  document.addEventListener('DOMContentLoaded', init);
  return { init };
})();