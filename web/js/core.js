const GEYLCA = (function() {
  'use strict';

  const MQTT_BROKER = 'wss://broker.hivemq.com:8884/mqtt';
  const RECONNECT_DELAY = 5000;
  const HEARTBEAT_INTERVAL = 30000;

  let mqttClient = null;
  let currentModule = null;
  let currentRole = 'user';
  let deviceCredentials = {};
  let eventHandlers = new Map();
  let connectionState = 'disconnected';
  let pendingRequests = new Map();
  let requestId = 0;

  function generateRequestId() {
    return ++requestId;
  }

  function setConnectionState(state) {
    connectionState = state;
    emit('connectionChange', state);
    updateConnectionUI(state);
  }

  function updateConnectionUI(state) {
    const dot = document.getElementById('connStatus');
    const badge = document.getElementById('moduleBadge');
    if (dot) {
      dot.className = 'conn-dot ' + (state === 'connected' ? 'on' : state === 'connecting' ? 'connecting' : 'off');
      dot.title = state === 'connected' ? 'Conectado' : state === 'connecting' ? 'Conectando...' : 'Desconectado';
    }
    if (badge && currentModule) {
      badge.textContent = state === 'connected' ? currentModule.name : 'VERIFICANDO';
      badge.className = 'status-tag ' + (state === 'connected' ? 'ok' : 'wait');
    }
  }

  function connectMQTT(module, role = 'user', token = '') {
    currentModule = module;
    currentRole = role;
    deviceCredentials = { module, role, token };
    setConnectionState('connecting');

    if (mqttClient) {
      mqttClient.end(true);
    }

    mqttClient = mqtt.connect(MQTT_BROKER, {
      clientId: 'GEYLCA_APP_' + module.id + '_' + Date.now(),
      clean: true,
      reconnectPeriod: 0,
      connectTimeout: 10000,
    });

    mqttClient.on('connect', () => {
      console.log('[MQTT] Conectado al broker');
      setConnectionState('connected');
      subscribeToModule(module.id);
      sendHeartbeat();
      setInterval(sendHeartbeat, HEARTBEAT_INTERVAL);
      emit('connected', module);
    });

    mqttClient.on('message', (topic, message) => {
      try {
        const data = JSON.parse(message.toString());
        handleMessage(topic, data);
      } catch (e) {
        console.error('[MQTT] Error parseando mensaje:', e);
      }
    });

    mqttClient.on('error', (err) => {
      console.error('[MQTT] Error:', err.message);
    });

    mqttClient.on('close', () => {
      console.log('[MQTT] Conexión cerrada');
      setConnectionState('disconnected');
      setTimeout(() => connectMQTT(module, role, token), RECONNECT_DELAY);
    });

    mqttClient.on('offline', () => {
      setConnectionState('disconnected');
    });
  }

  function subscribeToModule(deviceId) {
    const topics = [
      `geylca/${deviceId}/events`,
      `geylca/${deviceId}/status_resp`,
      `geylca/${deviceId}/logs`,
      `geylca/${deviceId}/backup_resp`,
    ];
    topics.forEach(t => mqttClient.subscribe(t, { qos: 1 }));
  }

  function sendHeartbeat() {
    if (mqttClient?.connected && currentModule) {
      mqttClient.publish(`geylca/${currentModule.id}/status`, JSON.stringify({ status: 'ONLINE' }), { qos: 1, retain: true });
    }
  }

  function sendCommand(action, payload = {}, role = currentRole, token = deviceCredentials.token) {
    return new Promise((resolve, reject) => {
      if (!mqttClient?.connected) {
        reject(new Error('MQTT no conectado'));
        return;
      }
      if (!currentModule) {
        reject(new Error('Módulo no seleccionado'));
        return;
      }

      const id = generateRequestId();
      const timestamp = Math.floor(Date.now() / 1000);
      const message = {
        action,
        role,
        token,
        timestamp,
        ...payload,
      };

      const timeout = setTimeout(() => {
        pendingRequests.delete(id);
        reject(new Error('Timeout: el módulo no respondió'));
      }, 15000);

      pendingRequests.set(id, { resolve, reject, timeout, action });

      const topic = `geylca/${currentModule.id}/cmd`;
      mqttClient.publish(topic, JSON.stringify(message), { qos: 1 });
    });
  }

  function handleMessage(topic, data) {
    if (topic.endsWith('/events') || topic.endsWith('/status_resp') || topic.endsWith('/logs') || topic.endsWith('/backup_resp')) {
      const action = data.action || data.status;
      if (action && pendingRequests.has(action)) {
        const req = pendingRequests.get(action);
        clearTimeout(req.timeout);
        pendingRequests.delete(action);
        if (data.status === 'DENIED' || data.status === 'ERROR') {
          req.reject(new Error(data.message || 'Comando denegado'));
        } else {
          req.resolve(data);
        }
        return;
      }

      if (topic.endsWith('/events')) {
        emit('event', data);
        addEventToLog(data);
      } else if (topic.endsWith('/status_resp')) {
        emit('statusResp', data);
      } else if (topic.endsWith('/logs')) {
        emit('logs', data);
      } else if (topic.endsWith('/backup_resp')) {
        emit('backup', data);
      }
    }
  }

  function emit(event, data) {
    const handlers = eventHandlers.get(event) || [];
    handlers.forEach(fn => {
      try { fn(data); } catch (e) { console.error(`[Event:${event}]`, e); }
    });
  }

  function on(event, handler) {
    if (!eventHandlers.has(event)) eventHandlers.set(event, []);
    eventHandlers.get(event).push(handler);
    return () => off(event, handler);
  }

  function off(event, handler) {
    const handlers = eventHandlers.get(event) || [];
    const idx = handlers.indexOf(handler);
    if (idx > -1) handlers.splice(idx, 1);
  }

  function addEventToLog(event) {
    const tbody = document.getElementById('eventTableBody');
    if (!tbody) return;
    const empty = tbody.querySelector('.empty-msg');
    if (empty) empty.parentElement.remove();

    const time = new Date((event.ts || event.timestamp || Date.now()) * (event.ts ? 1000 : 1)).toLocaleTimeString();
    const action = event.action || event.status || 'EVENT';
    const detail = event.apto ? `Slot ${event.slot} - ${event.apto}` : event.key ? `Key: ${event.key.substring(0, 16)}...` : JSON.stringify(event).substring(0, 80);

    const tr = document.createElement('tr');
    tr.innerHTML = `<td>${time}</td><td><span class="badge ${getBadgeClass(action)}">${formatAction(action)}</span></td><td>${detail}</td>`;
    tbody.insertBefore(tr, tbody.firstChild);
    while (tbody.children.length > 50) tbody.removeChild(tbody.lastChild);
  }

  function getBadgeClass(action) {
    const a = String(action).toLowerCase();
    if (a.includes('grant') || a.includes('ok') || a.includes('open')) return 'badge-success';
    if (a.includes('deni') || a.includes('error') || a.includes('fail') || a.includes('block')) return 'badge-danger';
    if (a.includes('warn') || a.includes('suspend') || a.includes('timeout')) return 'badge-warning';
    if (a.includes('learn') || a.includes('key')) return 'badge-info';
    return 'badge-primary';
  }

  function formatAction(action) {
    const map = {
      'GRANTED': 'Acceso', 'DENIED': 'Denegado', 'OPEN': 'Apertura',
      'SET_KEY': 'Key guardada', 'CLEAR_SLOT': 'Slot borrado',
      'SUSPEND': 'Suspendido', 'BLOCKED': 'Bloqueado', 'UNBLOCKED': 'Desbloqueado',
      'LEARN': 'Aprendizaje', 'FACTORY_RESET': 'Factory Reset', 'WIPE': 'Borrado total',
      'DOOR_TIMEOUT': 'Puerta abierta', 'KEY_SCAN': 'Key escaneada',
    };
    return map[action] || action;
  }

  function showToast(message, type = 'info') {
    const container = getOrCreateToastContainer();
    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    const icons = {
      success: '<svg class="toast-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"/><polyline points="22 4 12 14.01 9 11.01"/></svg>',
      error: '<svg class="toast-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><line x1="15" y1="9" x2="9" y2="15"/><line x1="9" y1="9" x2="15" y2="15"/></svg>',
      warning: '<svg class="toast-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>',
      info: '<svg class="toast-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>',
    };
    toast.innerHTML = `${icons[type] || icons.info}<span class="toast-message">${message}</span><button class="toast-close" aria-label="Cerrar"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg></button>`;
    toast.querySelector('.toast-close').onclick = () => toast.remove();
    container.appendChild(toast);
    setTimeout(() => toast.remove(), 5000);
  }

  function getOrCreateToastContainer() {
    let container = document.getElementById('toastContainer');
    if (!container) {
      container = document.createElement('div');
      container.id = 'toastContainer';
      container.className = 'toast-container';
      document.body.appendChild(container);
    }
    return container;
  }

  function showModal(title, body, actions = []) {
    return new Promise(resolve => {
      const overlay = document.createElement('div');
      overlay.className = 'modal-overlay';
      overlay.innerHTML = `
        <div class="modal" role="dialog" aria-modal="true" aria-labelledby="modalTitle">
          <div class="modal-header">
            <h3 id="modalTitle" class="modal-title">${title}</h3>
            <button class="modal-close" aria-label="Cerrar"><svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg></button>
          </div>
          <div class="modal-body">${body}</div>
          <div class="modal-footer"></div>
        </div>
      `;
      const footer = overlay.querySelector('.modal-footer');
      actions.forEach(({ label, variant = 'primary', value }) => {
        const btn = document.createElement('button');
        btn.className = `btn btn-${variant}`;
        btn.textContent = label;
        btn.onclick = () => { overlay.remove(); resolve(value); };
        footer.appendChild(btn);
      });
      overlay.querySelector('.modal-close').onclick = () => { overlay.remove(); resolve(null); };
      overlay.onclick = e => { if (e.target === overlay) { overlay.remove(); resolve(null); } };
      document.body.appendChild(overlay);
    });
  }

  function showConfirm(title, message, confirmText = 'Confirmar', variant = 'danger') {
    return showModal(title, `<p style="color:var(--color-text-muted);margin-bottom:var(--space-4)">${message}</p>`, [
      { label: 'Cancelar', variant: 'ghost', value: false },
      { label: confirmText, variant, value: true },
    ]);
  }

  function formatTimestamp(ts) {
    if (!ts) return '--';
    const date = new Date(ts * (ts > 1e12 ? 1 : 1000));
    return date.toLocaleString();
  }

  function bytesToHex(bytes) {
    return Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('').toUpperCase();
  }

  function hexToBytes(hex) {
    const bytes = new Uint8Array(hex.length / 2);
    for (let i = 0; i < bytes.length; i++) {
      bytes[i] = parseInt(hex.substr(i * 2, 2), 16);
    }
    return bytes;
  }

  function isValidPIN(pin) {
    return /^\d{6}$/.test(pin);
  }

  function debounce(fn, ms) {
    let timer;
    return (...args) => {
      clearTimeout(timer);
      timer = setTimeout(() => fn(...args), ms);
    };
  }

  function throttle(fn, ms) {
    let last = 0;
    return (...args) => {
      const now = Date.now();
      if (now - last >= ms) {
        last = now;
        fn(...args);
      }
    };
  }

  return {
    connectMQTT,
    sendCommand,
    on,
    off,
    showToast,
    showModal,
    showConfirm,
    formatTimestamp,
    bytesToHex,
    hexToBytes,
    isValidPIN,
    debounce,
    throttle,
    get currentModule() { return currentModule; },
    get currentRole() { return currentRole; },
    get connected() { return connectionState === 'connected'; },
  };
})();