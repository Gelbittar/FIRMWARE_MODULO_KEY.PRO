const AppAdmin = (function() {
  'use strict';

  let currentModule = null;
  let currentRole = 'admin';
  let currentBackup = null;

  async function init() {
    setupTabs();
    setupBottomNav();
    setupEventListeners();
    await BiometricAuth.init();
    loadSavedModules();
    renderFamilies();
    renderFamilySelects();
    const modules = JSON.parse(localStorage.getItem('geylca_modules') || '[]');
    if (modules.length === 0) {
      showModuleSelector();
    } else {
      showLogin();
    }
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
    if (tabId === 'tabBlocked') loadBlocked();
    if (tabId === 'tabFamilies') renderFamilies();
    if (tabId === 'tabConfig') loadBackupList();
    if (tabId === 'tabRoles') loadRoleKeys();
  }

  function switchSubTab(subId) {
    document.querySelectorAll('.sub-tab-btn').forEach(b => b.classList.toggle('active', b.dataset.sub === subId));
    document.querySelectorAll('.subview').forEach(v => v.style.display = v.id === subId ? 'block' : 'none');
  }

  function setupEventListeners() {
    document.getElementById('loginForm').addEventListener('submit', handleLogin);
    document.getElementById('btnDisconnect').addEventListener('click', disconnect);
    document.getElementById('btnSwitchModule').addEventListener('click', () => showModuleSelector());
    document.getElementById('btnSwitchModuleTop').addEventListener('click', () => showModuleSelector());
    document.getElementById('btnOpenDoor').addEventListener('click', openDoor);
    document.getElementById('btnRefreshStatus').addEventListener('click', refreshStatus);
    document.getElementById('btnQuerySlot').addEventListener('click', loadSlots);
    document.getElementById('btnSetKey').addEventListener('click', saveKey);
    document.getElementById('btnLearnKey').addEventListener('click', startLearnKey);
    document.getElementById('btnFindFreeSlot').addEventListener('click', findFreeSlot);
    document.getElementById('btnProgRead').addEventListener('click', readKey);
    document.getElementById('slotFilter').addEventListener('input', GEYLCA.debounce(filterSlots, 300));
    document.getElementById('slotOrder').addEventListener('change', sortSlots);

    document.getElementById('btnRefreshBlocked').addEventListener('click', loadBlocked);
    document.getElementById('btnCreateFamily').addEventListener('click', createFamily);
    document.getElementById('btnCreateBackup').addEventListener('click', createBackup);
    document.getElementById('btnImportBackup').addEventListener('click', () => document.getElementById('backupFileInput').click());
    document.getElementById('backupFileInput').addEventListener('change', importBackup);

    document.getElementById('copyFamilySelect').addEventListener('change', updateCopySourceSelect);
    document.getElementById('btnCopyToFamily').addEventListener('click', copyConfigToFamily);

    document.getElementById('btnSetRelay').addEventListener('click', () => setConfig('relay'));
    document.getElementById('btnSetSecMode').addEventListener('click', () => setConfig('secmode'));
    document.getElementById('btnSetInstallerKey').addEventListener('click', () => setConfig('installerkey'));
    document.getElementById('btnSetAdminKey').addEventListener('click', () => setConfig('adminkey'));
    document.getElementById('btnGenInstallerKey').addEventListener('click', () => document.getElementById('installerKeyInput').value = genPin());
    document.getElementById('btnGenAdminKey').addEventListener('click', () => document.getElementById('adminKeyInput').value = genPin());
    document.getElementById('btnGetRoleKeys').addEventListener('click', loadRoleKeys);
    document.getElementById('btnResetInstaller').addEventListener('click', () => resetKey('installer'));
    document.getElementById('btnResetAdminKey').addEventListener('click', () => resetKey('admin'));
    document.getElementById('btnSetRewriteProbe').addEventListener('click', () => setConfig('rewriteprobe'));
    document.getElementById('btnSetFamFilter').addEventListener('click', () => setConfig('famfilter'));
    document.getElementById('btnSetDoorConfig').addEventListener('click', () => setConfig('door'));
    document.getElementById('btnSetNtfy').addEventListener('click', () => setConfig('ntfy'));
    document.getElementById('btnTestNtfy').addEventListener('click', testNtfy);
    document.getElementById('btnResetWifi').addEventListener('click', () => maintAction('reset_wifi'));
    document.getElementById('btnWipeSlots').addEventListener('click', () => maintAction('wipe_slots'));
    document.getElementById('btnFactoryReset').addEventListener('click', () => maintAction('factory_reset'));

    document.getElementById('btnSetRoleAdmin').addEventListener('click', () => setRoleKey('admin'));
    document.getElementById('btnSetRoleInstaller').addEventListener('click', () => setRoleKey('installer'));
    document.getElementById('btnGetRoleKeys').addEventListener('click', loadRoleKeys);

    GEYLCA.on('connected', onConnected);
    GEYLCA.on('event', onEvent);
    GEYLCA.on('statusResp', onStatusResp);
    GEYLCA.on('backup', onBackup);
  }

  function loadSavedModules() {}

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
      GEYLCA.showToast('Primero agrega un módulo', 'warning');
      showModuleSelector();
      return;
    }

    const module = savedModules[0];
    try {
      const token = await generateToken(module, 'admin', pin);
      if (useBio) {
        try { await BiometricAuth.enableBiometric(module.id, { role: 'admin', pin }); }
        catch (bioErr) { GEYLCA.showToast('Biometría: ' + bioErr.message, 'warning'); }
      }
      await loginModule(module, 'admin', pin, token);
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
    GEYLCA.showToast(`Conectado a ${module.name || module.id} (Admin)`, 'success');
  }

  function onEvent(event) {}

  function onStatusResp(data) {
    if (data.max_slots !== undefined) updateDashboard(data);
  }

  function onBackup(data) {
    currentBackup = data;
    renderBackup(data);
  }

  function disconnect() {
    if (GEYLCA.mqttClient) GEYLCA.mqttClient.end(true);
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
    document.getElementById('btnRefreshBlocked').disabled = false;
    document.getElementById('btnCreateBackup').disabled = false;

    document.getElementById('relayTime').value = data.relay_time || 3;
    document.getElementById('secModeSelect').value = data.security_mode || 3;
    document.getElementById('rewriteProbeCheck').checked = data.rewrite_probe || false;
    document.getElementById('famFilter').value = data.family_filter || 0;
    document.getElementById('doorEnabledCheck').checked = data.door_enabled || false;
    document.getElementById('buzzerCheck').checked = data.buzzer_enabled !== false;
    document.getElementById('doorTimeout').value = data.door_timeout || 60;
    document.getElementById('doorPreBuzzer').value = data.buzzer_pre || 10;
    document.getElementById('doorNCSelect').value = data.door_nc ? '1' : '0';
    document.getElementById('ntfyUrl').value = data.ntfy_url || 'https://ntfy.sh';
    document.getElementById('ntfyTopic').value = data.ntfy_topic || '';
    document.getElementById('btnSetRelay').disabled = false;
    document.getElementById('btnSetSecMode').disabled = false;
    document.getElementById('btnSetInstallerKey').disabled = false;
    document.getElementById('btnSetAdminKey').disabled = false;
    document.getElementById('btnSetRewriteProbe').disabled = false;
    document.getElementById('btnSetFamFilter').disabled = false;
    document.getElementById('btnSetDoorConfig').disabled = false;
    document.getElementById('btnSetNtfy').disabled = false;
    document.getElementById('btnTestNtfy').disabled = false;
    document.getElementById('btnResetWifi').disabled = false;
    document.getElementById('btnWipeSlots').disabled = false;
    document.getElementById('btnFactoryReset').disabled = false;
  }

  async function openDoor() {
    try { await GEYLCA.sendCommand('open'); GEYLCA.showToast('Puerta abierta', 'success'); }
    catch (e) { GEYLCA.showToast('Error: ' + e.message, 'error'); }
  }

  async function loadSlots() {
    if (!GEYLCA.connected) return;
    document.getElementById('btnQuerySlot').disabled = true;
    document.getElementById('slotTableBody').innerHTML = '<tr><td colspan="5" class="empty-msg">Cargando...</td></tr>';
    try { await GEYLCA.sendCommand('get_slots'); }
    catch (e) { document.getElementById('slotTableBody').innerHTML = '<tr><td colspan="5" class="empty-msg">Error: ' + e.message + '</td></tr>'; }
    finally { document.getElementById('btnQuerySlot').disabled = false; }
  }

  function renderSlots(slots) {
    const tbody = document.getElementById('slotTableBody');
    if (!slots || slots.length === 0) { tbody.innerHTML = '<tr><td colspan="5" class="empty-msg">No hay slots programados</td></tr>'; return; }
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
    try { await GEYLCA.sendCommand('clear_slot', { slot }); GEYLCA.showToast('Slot borrado', 'success'); loadSlots(); }
    catch (e) { GEYLCA.showToast('Error: ' + e.message, 'error'); }
  };

  async function saveKey() {
    const slot = parseInt(document.getElementById('slotNum').value) || 0;
    const key = document.getElementById('slotKey').value.trim().toUpperCase();
    const apto = document.getElementById('slotApto').value.trim();
    if (!key) { GEYLCA.showToast('Key requerida', 'error'); return; }
    try { await GEYLCA.sendCommand('set_key', { slot, key, apto }); GEYLCA.showToast('Key guardada', 'success'); document.getElementById('slotNum').value = ''; document.getElementById('slotKey').value = ''; document.getElementById('slotApto').value = ''; loadSlots(); }
    catch (e) { GEYLCA.showToast('Error: ' + e.message, 'error'); }
  }

  async function startLearnKey() {
    try { await GEYLCA.sendCommand('learn_key', { apto: document.getElementById('slotApto').value || 'Desconocido' }); document.getElementById('learnWaitBox').style.display = 'block'; GEYLCA.showToast('Presente la llave', 'info'); }
    catch (e) { GEYLCA.showToast('Error: ' + e.message, 'error'); }
  }

  async function findFreeSlot() {
    try { const resp = await GEYLCA.sendCommand('get_first_free_slot'); if (resp.slot > 0) { document.getElementById('slotNum').value = resp.slot; GEYLCA.showToast('Slot libre: ' + resp.slot, 'success'); } else { GEYLCA.showToast('No hay slots libres', 'warning'); } }
    catch (e) { GEYLCA.showToast('Error: ' + e.message, 'error'); }
  }

  async function readKey() {
    document.getElementById('progReadStatus').textContent = 'Leyendo...';
    try { await GEYLCA.sendCommand('learn_key', { slot: 0 }); document.getElementById('progReadStatus').textContent = 'Presente la llave...'; }
    catch (e) { document.getElementById('progReadStatus').textContent = 'Error: ' + e.message; }
  }

  function filterSlots() {
    const filter = document.getElementById('slotFilter').value.toLowerCase();
    document.querySelectorAll('#slotTableBody tr').forEach(tr => { tr.style.display = tr.textContent.toLowerCase().includes(filter) ? '' : 'none'; });
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

  async function loadBlocked() {
    if (!GEYLCA.connected) return;
    document.getElementById('btnRefreshBlocked').disabled = true;
    document.getElementById('blockedTableBody').innerHTML = '<tr><td colspan="5" class="empty-msg">Cargando...</td></tr>';
    try { await GEYLCA.sendCommand('get_slots'); }
    catch (e) { document.getElementById('blockedTableBody').innerHTML = '<tr><td colspan="5" class="empty-msg">Error: ' + e.message + '</td></tr>'; }
    finally { document.getElementById('btnRefreshBlocked').disabled = false; }
  }

  function renderBlocked(slots) {
    const blocked = (slots || []).filter(s => s.suspended || s.blk);
    const tbody = document.getElementById('blockedTableBody');
    if (!blocked.length) { tbody.innerHTML = '<tr><td colspan="5" class="empty-msg">No hay slots bloqueados</td></tr>'; document.getElementById('blockedInfo').textContent = ''; return; }
    document.getElementById('blockedInfo').textContent = `${blocked.length} slot(s) bloqueado(s)`;
    tbody.innerHTML = blocked.map(s => `
      <tr>
        <td>${s.slot}</td>
        <td>${s.apto || '-'}</td>
        <td>${s.blk || (s.suspended ? 'suspendido' : 'desconocido')}</td>
        <td>${s.blkts ? new Date(s.blkts * 1000).toLocaleString() : '-'}</td>
        <td>
          <button class="btn btn-primary btn-sm" onclick="unblockSlot(${s.slot})">Desbloquear</button>
        </td>
      </tr>
    `).join('');
  }

  window.unblockSlot = async (slot) => {
    try { await GEYLCA.sendCommand('unblock_slot', { slot }); GEYLCA.showToast('Slot desbloqueado', 'success'); loadBlocked(); }
    catch (e) { GEYLCA.showToast('Error: ' + e.message, 'error'); }
  };

  function genPin() { return Math.floor(100000 + Math.random() * 900000).toString(); }

  async function setConfig(type) {
    try {
      let payload = {};
      switch (type) {
        case 'relay': payload = { action: 'set_relay', relay_time: parseInt(document.getElementById('relayTime').value) }; break;
        case 'secmode': payload = { action: 'set_security_mode', mode: parseInt(document.getElementById('secModeSelect').value) }; break;
        case 'installerkey': payload = { action: 'set_role_key', target_role: 'installer', key: document.getElementById('installerKeyInput').value }; break;
        case 'adminkey': payload = { action: 'set_role_key', target_role: 'admin', key: document.getElementById('adminKeyInput').value }; break;
        case 'rewriteprobe': payload = { action: 'set_rewrite_probe', enabled: document.getElementById('rewriteProbeCheck').checked }; break;
        case 'famfilter': payload = { action: 'set_family_filter', family: parseInt(document.getElementById('famFilter').value) }; break;
        case 'door': payload = { action: 'set_door_config', enabled: document.getElementById('doorEnabledCheck').checked, timeout: parseInt(document.getElementById('doorTimeout').value), nc: document.getElementById('doorNCSelect').value === '1', buzzer: document.getElementById('buzzerCheck').checked, buzzer_pre: parseInt(document.getElementById('doorPreBuzzer').value) }; break;
        case 'ntfy': payload = { action: 'set_ntfy', url: document.getElementById('ntfyUrl').value, topic: document.getElementById('ntfyTopic').value }; break;
      }
      await GEYLCA.sendCommand(payload.action, payload);
      GEYLCA.showToast('Configuración guardada', 'success');
      refreshStatus();
    } catch (e) { GEYLCA.showToast('Error: ' + e.message, 'error'); }
  }

  async function resetKey(role) {
    if (!await GEYLCA.showConfirm('Reset PIN', `¿Restablecer PIN ${role} a valor de fábrica?`, 'Resetear', 'danger')) return;
    try { await GEYLCA.sendCommand('reset_' + role + '_key'); GEYLCA.showToast('PIN restablecido', 'success'); loadRoleKeys(); }
    catch (e) { GEYLCA.showToast('Error: ' + e.message, 'error'); }
  }

  async function loadRoleKeys() {
    try { const resp = await GEYLCA.sendCommand('get_role_keys'); document.getElementById('roleKeysDisplay').textContent = JSON.stringify(resp, null, 2); document.getElementById('roleKeysDisplay2').textContent = JSON.stringify(resp, null, 2); }
    catch (e) { GEYLCA.showToast('Error: ' + e.message, 'error'); }
  }

  async function setRoleKey(role) {
    const pin = document.getElementById('role' + role.charAt(0).toUpperCase() + role.slice(1) + 'Pin').value;
    if (!GEYLCA.isValidPIN(pin)) { GEYLCA.showToast('PIN debe ser 6 dígitos', 'error'); return; }
    try { await GEYLCA.sendCommand('set_role_key', { target_role: role, key: pin }); GEYLCA.showToast('PIN ' + role + ' guardado', 'success'); loadRoleKeys(); }
    catch (e) { GEYLCA.showToast('Error: ' + e.message, 'error'); }
  }

  async function testNtfy() {
    try { await GEYLCA.sendCommand('set_ntfy', { url: document.getElementById('ntfyUrl').value, topic: document.getElementById('ntfyTopic').value }); GEYLCA.showToast('Prueba enviada', 'success'); }
    catch (e) { GEYLCA.showToast('Error: ' + e.message, 'error'); }
  }

  async function maintAction(action) {
    const labels = { reset_wifi: 'Reset WiFi', wipe_slots: 'Borrar TODAS las casillas', factory_reset: 'FACTORY RESET' };
    if (!await GEYLCA.showConfirm(labels[action], `¿Ejecutar ${labels[action]}? Esta acción no se puede deshacer.`, 'Ejecutar', 'danger')) return;
    try { await GEYLCA.sendCommand(action); GEYLCA.showToast(action + ' ejecutado', 'success'); setTimeout(refreshStatus, 2000); }
    catch (e) { GEYLCA.showToast('Error: ' + e.message, 'error'); }
  }

  function renderFamilies() {
    const families = FamilyManager.getFamilies();
    const activeId = FamilyManager.getActiveFamilyId();
    const container = document.getElementById('familiesList');
    if (!families.length) { container.innerHTML = '<div class="empty-state"><svg class="empty-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M23 21v-2a4 4 0 0 0-3-3.87"/><path d="M16 3.13a4 4 0 0 1 0 7.75"/></svg><h3 class="empty-title">No hay familias</h3><p class="empty-desc">Crea una familia para agrupar módulos y copiar configuraciones</p></div>'; return; }
    container.innerHTML = families.map(f => {
      const modules = FamilyManager.getFamilyModules(f.id);
      return `
        <div class="card" style="margin-bottom:var(--space-3);border-left:4px solid ${f.color}">
          <div class="card-body">
            <div style="display:flex;align-items:flex-start;justify-content:space-between;gap:var(--space-3)">
              <div>
                <div style="display:flex;align-items:center;gap:var(--space-2);margin-bottom:var(--space-1)">
                  <strong>${f.name}</strong>
                  <span class="badge" style="background:${f.color}20;color:${f.color}">${modules.length} módulos</span>
                  ${f.id === activeId ? '<span class="badge badge-primary">ACTIVA</span>' : ''}
                </div>
                <p style="color:var(--color-text-muted);font-size:var(--text-sm)">${f.description || 'Sin descripción'}</p>
                <p style="font-size:var(--text-xs);color:var(--color-text-dim)">Creada: ${new Date(f.createdAt).toLocaleDateString()}</p>
              </div>
              <div style="display:flex;gap:var(--space-2);flex-wrap:wrap">
                <button class="btn btn-outline btn-sm" onclick="editFamily('${f.id}')">Editar</button>
                <button class="btn btn-${f.id === activeId ? 'ghost' : 'primary'} btn-sm" onclick="setActiveFamily('${f.id}')">${f.id === activeId ? 'Activa' : 'Activar'}</button>
                <button class="btn btn-danger btn-sm" onclick="deleteFamily('${f.id}')">Eliminar</button>
              </div>
            </div>
            ${modules.length ? `
              <div style="margin-top:var(--space-3);padding-top:var(--space-3);border-top:1px solid var(--color-border)">
                <strong style="font-size:var(--text-sm)">Módulos:</strong>
                <div style="display:flex;flex-wrap:wrap;gap:var(--space-2);margin-top:var(--space-2)">
                  ${modules.map(m => `<span class="chip" style="background:${f.color}20;border-color:${f.color};color:${f.color}">${m.name || m.id} <button class="btn btn-ghost btn-icon btn-sm" onclick="removeModuleFromFamily('${f.id}', '${m.id}')" style="padding:0;margin-left:var(--space-1)"><svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg></button></span>`).join('')}
                </div>
              </div>
            ` : ''}
          </div>
        </div>
      `;
    }).join('');
  }

  window.editFamily = (id) => {
    const family = FamilyManager.getFamilies().find(f => f.id === id);
    if (!family) return;
    GEYLCA.showModal('Editar Familia', `
      <div class="form-group"><label class="form-label">Nombre</label><input id="editFamName" class="form-input" value="${family.name}"></div>
      <div class="form-group"><label class="form-label">Descripción</label><textarea id="editFamDesc" class="form-input" rows="3">${family.description || ''}</textarea></div>
      <div class="form-group"><label class="form-label">Color</label><input id="editFamColor" type="color" class="form-input" value="${family.color}" style="height:44px"></div>
    `, [
      { label: 'Cancelar', variant: 'ghost', value: false },
      { label: 'Guardar', variant: 'primary', value: true },
    ]).then(result => {
      if (result) {
        FamilyManager.updateFamily(id, { name: document.getElementById('editFamName').value, description: document.getElementById('editFamDesc').value, color: document.getElementById('editFamColor').value });
        renderFamilies();
        renderFamilySelects();
        GEYLCA.showToast('Familia actualizada', 'success');
      }
    });
  };

  window.setActiveFamily = (id) => { FamilyManager.setActiveFamily(id); renderFamilies(); renderFamilySelects(); GEYLCA.showToast('Familia activada', 'success'); };

  window.deleteFamily = async (id) => {
    if (!await GEYLCA.showConfirm('Eliminar familia', '¿Eliminar esta familia? Se perderán las asociaciones de módulos.', 'Eliminar', 'danger')) return;
    FamilyManager.deleteFamily(id); renderFamilies(); renderFamilySelects(); GEYLCA.showToast('Familia eliminada', 'success');
  };

  function createFamily() {
    GEYLCA.showModal('Nueva Familia', `
      <div class="form-group"><label class="form-label">Nombre</label><input id="newFamName" class="form-input" placeholder="Ej: Residencias Caracas" required></div>
      <div class="form-group"><label class="form-label">Descripción</label><textarea id="newFamDesc" class="form-input" rows="3" placeholder="Ej: Conjunto residencial en Caracas"></textarea></div>
      <div class="form-group"><label class="form-label">Color</label><input id="newFamColor" type="color" class="form-input" value="#00d4ff" style="height:44px"></div>
    `, [
      { label: 'Cancelar', variant: 'ghost', value: false },
      { label: 'Crear', variant: 'primary', value: true },
    ]).then(result => {
      if (result) {
        const name = document.getElementById('newFamName').value.trim();
        if (!name) { GEYLCA.showToast('Nombre requerido', 'error'); return; }
        FamilyManager.createFamily(name, document.getElementById('newFamDesc').value, document.getElementById('newFamColor').value);
        renderFamilies(); renderFamilySelects();
        GEYLCA.showToast('Familia creada', 'success');
      }
    });
  }

  function renderFamilySelects() {
    const families = FamilyManager.getFamilies();
    const activeId = FamilyManager.getActiveFamilyId();
    const copySelect = document.getElementById('copyFamilySelect');
    copySelect.innerHTML = '<option value="">Seleccionar familia...</option>' + families.map(f => `<option value="${f.id}" ${f.id === activeId ? 'selected' : ''}>${f.name}</option>`).join('');
    updateCopySourceSelect();
  }

  function updateCopySourceSelect() {
    const familyId = document.getElementById('copyFamilySelect').value;
    const sourceSelect = document.getElementById('copySourceSelect');
    if (!familyId) { sourceSelect.innerHTML = '<option value="">Primero seleccione familia</option>'; document.getElementById('btnCopyToFamily').disabled = true; return; }
    const modules = FamilyManager.getFamilyModules(familyId);
    sourceSelect.innerHTML = '<option value="">Seleccionar módulo origen...</option>' + modules.map(m => `<option value="${m.id}">${m.name || m.id}</option>`).join('');
    document.getElementById('btnCopyToFamily').disabled = modules.length < 2;
  }

  async function copyConfigToFamily() {
    const familyId = document.getElementById('copyFamilySelect').value;
    const sourceId = document.getElementById('copySourceSelect').value;
    const copyConfig = document.getElementById('copyConfigCheck').checked;
    const copyKeys = document.getElementById('copyKeysCheck').checked;
    if (!familyId || !sourceId) { GEYLCA.showToast('Seleccione familia y módulo origen', 'error'); return; }
    if (!await GEYLCA.showConfirm('Copiar configuración', `¿Copiar de ${sourceId} a todos los demás módulos de la familia?`, 'Copiar', 'primary')) return;

    document.getElementById('btnCopyToFamily').disabled = true;
    document.getElementById('btnCopyToFamily').textContent = 'Copiando...';
    try {
      await FamilyManager.copyConfigToFamily(familyId, sourceId, { config: copyConfig, keys: copyKeys });
      GEYLCA.showToast('Configuración copiada a la familia', 'success');
    } catch (e) {
      GEYLCA.showToast('Error: ' + e.message, 'error');
    } finally {
      document.getElementById('btnCopyToFamily').disabled = false;
      document.getElementById('btnCopyToFamily').textContent = 'Copiar a toda la familia';
    }
  }

  async function createBackup() {
    if (!GEYLCA.connected) { GEYLCA.showToast('No conectado', 'error'); return; }
    try { const backup = await BackupManager.createBackup(currentModule.id, currentModule.name || currentModule.id, true); loadBackupList(); }
    catch (e) { GEYLCA.showToast('Error: ' + e.message, 'error'); }
  }

  function loadBackupList() {
    const backups = BackupManager.getBackupsForModule(currentModule?.id);
    const tbody = document.getElementById('backupTableBody');
    if (!backups.length) { tbody.innerHTML = '<tr><td colspan="4" class="empty-msg">Sin backups guardados</td></tr>'; return; }
    tbody.innerHTML = backups.map(b => `
      <tr>
        <td>${b.moduleName}</td>
        <td>${new Date(b.timestamp).toLocaleString()}</td>
        <td>${b.slotCount}</td>
        <td>
          <button class="btn btn-primary btn-sm" onclick="restoreBackup('${b.id}')">Restaurar</button>
          <button class="btn btn-outline btn-sm" onclick="exportBackup('${b.id}')">Exportar</button>
          <button class="btn btn-danger btn-sm" onclick="deleteBackup('${b.id}')">Borrar</button>
        </td>
      </tr>
    `).join('');
  }

  window.restoreBackup = async (id) => {
    const backups = BackupManager.getBackupsForModule(currentModule.id);
    const backup = backups.find(b => b.id === id);
    if (!backup) return;
    if (!await GEYLCA.showConfirm('Restaurar backup', `¿Restaurar backup de ${backup.moduleName} (${backup.slotCount} slots)? Esto sobrescribirá la configuración actual.`, 'Restaurar', 'primary')) return;
    try { await BackupManager.restoreBackup(currentModule.id, backup, { restoreConfig: true, restoreKeys: true }); GEYLCA.showToast('Backup restaurado', 'success'); refreshStatus(); loadSlots(); }
    catch (e) { GEYLCA.showToast('Error: ' + e.message, 'error'); }
  };

  window.exportBackup = (id) => { BackupManager.exportBackup(id); GEYLCA.showToast('Backup exportado', 'success'); };

  window.deleteBackup = (id) => { if (confirm('¿Eliminar este backup?')) { BackupManager.deleteBackup(id); loadBackupList(); GEYLCA.showToast('Backup eliminado', 'success'); } };

  async function importBackup(e) {
    const file = e.target.files[0];
    if (!file) return;
    try { await BackupManager.importBackup(file); loadBackupList(); }
    catch (e) {}
    e.target.value = '';
  }

  function renderBackup(data) {
    const html = `
      <div class="card" style="margin-top:var(--space-3)">
        <div class="card-header"><h3 class="card-title">Backup Recibido: ${data.moduleName || data.device_id}</h3></div>
        <div class="card-body">
          <p><strong>Firmware:</strong> ${data.version} | <strong>Modo:</strong> ${data.security_mode === 3 ? 'SÓVICA' : data.security_mode + ' bytes'} | <strong>Slots:</strong> ${data.slotCount || (data.slots?.length || 0)}</p>
          <div class="btn-row">
            <button class="btn btn-primary" onclick="restoreCurrentBackup(true, true)">Restaurar Config + Keys</button>
            <button class="btn btn-outline" onclick="restoreCurrentBackup(true, false)">Solo Config</button>
            <button class="btn btn-outline" onclick="restoreCurrentBackup(false, true)">Solo Keys</button>
            <button class="btn btn-secondary" onclick="saveCurrentBackup()">Guardar Local</button>
          </div>
        </div>
      </div>
    `;
    document.getElementById('backupList').insertAdjacentHTML('afterbegin', html);
  }

  window.restoreCurrentBackup = async (config, keys) => {
    if (!currentBackup) return;
    try { await BackupManager.restoreBackup(currentModule.id, currentBackup, { restoreConfig: config, restoreKeys: keys }); GEYLCA.showToast('Restaurado', 'success'); }
    catch (e) { GEYLCA.showToast('Error: ' + e.message, 'error'); }
  };

  window.saveCurrentBackup = () => { if (currentBackup) { BackupManager.createBackup(currentModule.id, currentModule.name || currentModule.id, true); GEYLCA.showToast('Backup guardado localmente', 'success'); } };

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
      currentModule = module;
      GEYLCA.showToast('Módulo seleccionado: ' + module.name, 'info');
      showLogin();
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
        currentModule = { id, name };
        showLogin();
      }
    });
  };

  document.addEventListener('DOMContentLoaded', init);
  return { init };
})();