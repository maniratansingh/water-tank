/* ================================================================
   Standalone ESP8266 TankSync Dashboard
   ================================================================ */

'use strict';

// ── State ────────────────────────────────────────────────────────
let ws = null;
let tankData = null;
let reconnectTimer = null;
let reconnectAttempts = 0;
const MAX_RECONNECT = 8;

// ── SVG gradient defs ─────────────────────────────────────────────
const SVG_DEFS = `
<svg id="svg-defs" aria-hidden="true" style="position: absolute; width: 0; height: 0;">
  <defs>
    <linearGradient id="waterCritical" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#f97316"/>
      <stop offset="100%" stop-color="#ef4444"/>
    </linearGradient>
    <linearGradient id="waterLow" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#f59e0b"/>
      <stop offset="100%" stop-color="#f97316"/>
    </linearGradient>
    <linearGradient id="waterMid" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#22c55e"/>
      <stop offset="100%" stop-color="#f59e0b"/>
    </linearGradient>
    <linearGradient id="waterGood" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#00d9ff"/>
      <stop offset="100%" stop-color="#22c55e"/>
    </linearGradient>
    <linearGradient id="waterFull" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#60c8ff"/>
      <stop offset="100%" stop-color="#0070f3"/>
    </linearGradient>
  </defs>
</svg>`;

// ── Helpers ───────────────────────────────────────────────────────
const $ = id => document.getElementById(id);
const fmt = n => (n === null || n === undefined || isNaN(n)) ? '—' : Number(n).toLocaleString('en-IN');
const fmtPct = n => (n === null || n === undefined) ? '—' : `${Math.round(n)}`;
const fmtRssi = r => r ? `${r} dBm` : '—';
const fmtUptime = s => {
  if (!s && s !== 0) return '—';
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  return h > 0 ? `${h}h ${m}m` : `${m}m`;
};
const now = () => new Date().toLocaleTimeString('en-IN', { hour12: false });

function levelClass(pct) {
  if (pct === null || pct === undefined) return '';
  if (pct <= 10) return 'level-critical';
  if (pct <= 25) return 'level-low';
  if (pct <= 50) return 'level-mid';
  if (pct <= 85) return 'level-good';
  return 'level-full';
}

function levelColor(pct) {
  if (pct === null) return '#4a5568';
  if (pct <= 10) return 'url(#waterCritical)';
  if (pct <= 25) return 'url(#waterLow)';
  if (pct <= 50) return 'url(#waterMid)';
  if (pct <= 85) return 'url(#waterGood)';
  return 'url(#waterFull)';
}

// ── Logging ────────────────────────────────────────────────────────
function log(type, msg) {
  const logEl = $('event-log');
  if (!logEl) return;
  const entry = document.createElement('div');
  entry.className = `log-entry log-${type}`;
  const typeLabels = { info: 'INFO', warn: 'WARN', error: 'ERR!', success: 'OK  ', data: 'DATA' };
  entry.innerHTML = `
    <span class="log-time">${now()}</span>
    <span class="log-type">${typeLabels[type] || type.toUpperCase()}</span>
    <span class="log-msg">${escHtml(msg)}</span>`;
  logEl.prepend(entry);
  while (logEl.children.length > 50) logEl.removeChild(logEl.lastChild);
}

function clearLog() {
  const el = $('event-log');
  if (el) el.innerHTML = '';
}

function escHtml(s) {
  return String(s)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;')
    .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

// ── Toasts ─────────────────────────────────────────────────────────
function toast(msg, type = 'info', duration = 3500) {
  const c = $('toast-container');
  if (!c) return;
  const t = document.createElement('div');
  t.className = `toast toast-${type}`;
  t.textContent = msg;
  c.appendChild(t);
  setTimeout(() => {
    t.style.animation = 'toastOut 0.3s ease forwards';
    setTimeout(() => t.remove(), 320);
  }, duration);
}

// ── WS status indicator ────────────────────────────────────────────
function setWsStatus(state, label) {
  const badge = $('ws-status');
  const lbl = $('ws-label');
  if (badge) badge.className = `ws-badge ws-${state}`;
  if (lbl) lbl.textContent = label;
}

// ── Settings Modal ─────────────────────────────────────────────────
function toggleSettings(show) {
  if (show) {
    $('settings-modal').classList.remove('hidden');
  } else {
    $('settings-modal').classList.add('hidden');
  }
}

function saveSettings() {
  const empty = parseInt($('set-empty').value);
  const full = parseInt($('set-full').value);
  const cap = parseInt($('set-capacity').value);

  if (isNaN(empty) || isNaN(full) || isNaN(cap)) {
    toast('Please enter valid numbers', 'warn');
    return;
  }

  if (empty <= full) {
    toast('Empty distance must be greater than full distance', 'error');
    return;
  }

  const payload = {
    type: 'update_settings',
    empty_distance_cm: empty,
    full_distance_cm: full,
    capacity_l: cap
  };

  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(payload));
    toast('Settings saved to ESP8266', 'success');
    toggleSettings(false);
  } else {
    toast('Not connected to device', 'error');
  }
}

// ── Connection flow ────────────────────────────────────────────────
function autoConnect() {
  // Use the host IP that served the file
  const ip = window.location.host;
  // Fallback for local testing if opened as file://
  const url = ip ? `ws://${ip}/ws` : `ws://192.168.1.100/ws`;
  log('info', `Auto-connecting to ${url}…`);
  openWebSocket(url);
}

function openWebSocket(url) {
  if (ws) ws.close();
  setWsStatus('connecting', 'Connecting…');

  try {
    ws = new WebSocket(url);
  } catch (e) {
    log('error', `WebSocket creation failed: ${e.message}`);
    setWsStatus('error', 'Error');
    scheduleReconnect(url);
    return;
  }

  ws.addEventListener('open', () => {
    reconnectAttempts = 0;
    setWsStatus('connected', 'Live');
    log('success', `WebSocket connected to ${url}`);
    toast('Connected to Sensor 🎉', 'success');
  });

  ws.addEventListener('message', e => {
    try {
      const msg = JSON.parse(e.data);
      handleMessage(msg);
    } catch (err) {
      log('warn', `Unparse-able frame: ${e.data.slice(0, 80)}`);
    }
  });

  ws.addEventListener('close', evt => {
    setWsStatus('disconnected', 'Disconnected');
    log('warn', `Connection closed (code ${evt.code})`);
    toast('Sensor disconnected — reconnecting…', 'warn');
    scheduleReconnect(url);
  });

  ws.addEventListener('error', () => {
    setWsStatus('error', 'Error');
    log('error', 'WebSocket error');
  });
}

function scheduleReconnect(url) {
  if (reconnectAttempts >= MAX_RECONNECT) {
    log('error', 'Max reconnect attempts reached. Please refresh page.');
    toast('Could not reconnect. Refresh the page.', 'error', 6000);
    return;
  }
  const delay = Math.min(2000 * 2 ** reconnectAttempts, 30000);
  reconnectAttempts++;
  log('info', `Retry ${reconnectAttempts}/${MAX_RECONNECT} in ${(delay / 1000).toFixed(0)}s…`);
  setWsStatus('connecting', `Retry ${reconnectAttempts}…`);
  reconnectTimer = setTimeout(() => openWebSocket(url), delay);
}

// ── Message handler ────────────────────────────────────────────────
function handleMessage(msg) {
  if (msg.type === 'state') {
    // Update settings form if provided (skip if user is currently editing)
    if (msg.settings && $('settings-modal').classList.contains('hidden')) {
      $('set-empty').value = msg.settings.empty_distance_cm;
      $('set-full').value = msg.settings.full_distance_cm;
    }

    const devs = msg.devices || [];
    if (devs.length > 0) {
      if (msg.settings) {
        $('set-capacity').value = devs[0].capacity_l;
      }
      updateTank(devs[0]);
    }
  } else {
    log('info', `msg type="${msg.type}"`);
  }
}

// ── Tank update ────────────────────────────────────────────────────
function updateTank(d) {
  const state = d.state || {};
  const exists = (tankData !== null);

  tankData = {
    id: d.tank,
    name: d.name || `Tank ${d.tank}`,
    capacity_l: d.capacity_l || 0,
    level_pct: state.level_pct ?? null,
    rssi: state.rssi ?? null,
    conn_state: state.conn_state || 'waiting',
    sensor_error: state.sensor_error || false,
    sensor_stuck: state.sensor_stuck || false,
    consumed_l: state.consumed_l ?? null,
    updatedAt: Date.now(),
  };

  tankData.volume_l = (tankData.capacity_l && tankData.level_pct !== null)
    ? Math.round(tankData.capacity_l * tankData.level_pct / 100)
    : null;

  renderTankCard(tankData, !exists);
}

// ── Tank card DOM ──────────────────────────────────────────────────
function renderTankCard(tank, isNew) {
  const container = $('tank-card-container');
  if (!container) return;

  if (!container.firstElementChild) {
    container.innerHTML = `<div class="tank-card status-${tank.conn_state}" id="main-tank-card"></div>`;
    isNew = true;
  }

  const card = $('main-tank-card');
  if (!card) return;

  card.className = `tank-card status-${tank.conn_state}`;

  const pct = tank.level_pct ?? 0;
  const lvlClass = levelClass(tank.level_pct);
  const statusBadge = statusBadgeHTML(tank.conn_state);
  const sensorAlert = (tank.sensor_error || tank.sensor_stuck)
    ? `<div class="sensor-alert">⚠ ${tank.sensor_error ? 'Too near (<20cm) or offline. Place at appropriate position.' : 'Sensor reading stuck'}</div>` : '';

  const SVG_H = 90;
  const FILL_H = Math.max(0, Math.min(SVG_H, Math.round(SVG_H * pct / 100)));
  const FILL_Y = SVG_H - FILL_H + 5;

  if (isNew) {
    card.innerHTML = `
    <div class="tank-header">
      <div class="tank-name-row">
        <span class="tank-name">${escHtml(tank.name)}</span>
        <span class="tank-id">${fmt(tank.capacity_l)} Litre Capacity</span>
      </div>
      <div id="status-badge-container">${statusBadge}</div>
    </div>

    <div class="tank-visual ${lvlClass}" id="tank-visual-container">
      <div class="tank-svg-wrap">
        <svg viewBox="0 0 60 100" fill="none" xmlns="http://www.w3.org/2000/svg" id="tank-svg">
          <rect x="4" y="5" width="52" height="90" rx="6" fill="rgba(255,255,255,0.04)" stroke="rgba(255,255,255,0.12)" stroke-width="1.5"/>
          <g clip-path="url(#tankClip)">
            <rect class="tank-water"
              x="5.5" y="${FILL_Y}" width="49" height="${FILL_H}"
              rx="${FILL_H > 5 ? 4 : 2}"
              fill="${levelColor(tank.level_pct)}"
              id="water-rect"/>
            <path class="tank-wave" fill="${levelColor(tank.level_pct)}" opacity="0.6"
              d="M 0 ${FILL_Y} Q 15 ${FILL_Y-3} 30 ${FILL_Y} T 60 ${FILL_Y} T 90 ${FILL_Y} T 120 ${FILL_Y} L 120 100 L 0 100 Z"
              id="water-wave"/>
          </g>
          <rect x="5.5" y="5.75" width="49" height="89" rx="5"
            fill="url(#sheen)" opacity="0.3"/>
          <defs>
            <clipPath id="tankClip">
              <rect x="5.5" y="5.75" width="49" height="89" rx="5" />
            </clipPath>
            <linearGradient id="sheen" x1="0" y1="0" x2="1" y2="0">
              <stop offset="0%" stop-color="white" stop-opacity="0.08"/>
              <stop offset="40%" stop-color="white" stop-opacity="0"/>
              <stop offset="100%" stop-color="white" stop-opacity="0"/>
            </linearGradient>
          </defs>
          <line x1="4" y1="${5 + SVG_H * 0.25}" x2="8"  y2="${5 + SVG_H * 0.25}" stroke="rgba(255,255,255,0.15)" stroke-width="1"/>
          <line x1="4" y1="${5 + SVG_H * 0.5}"  x2="8"  y2="${5 + SVG_H * 0.5}"  stroke="rgba(255,255,255,0.15)" stroke-width="1"/>
          <line x1="4" y1="${5 + SVG_H * 0.75}" x2="8"  y2="${5 + SVG_H * 0.75}" stroke="rgba(255,255,255,0.15)" stroke-width="1"/>
        </svg>
        <div class="tank-pct-overlay" id="overlay-pct">${tank.level_pct !== null ? pct + '%' : '—'}</div>
      </div>

      <div class="tank-level-info ${lvlClass}" id="level-info-container">
        <div>
          <span class="level-pct" id="info-pct">${fmtPct(tank.level_pct)}</span>
          <span class="level-unit">%</span>
        </div>
        <div class="level-volume">
          <strong id="info-vol">${fmt(tank.volume_l)} L</strong> available
        </div>
      </div>
    </div>

    <div class="tank-progress ${lvlClass}" id="progress-container">
      <div class="progress-track">
        <div class="progress-fill" id="progress-fill" style="width:${tank.level_pct ?? 0}%"></div>
      </div>
    </div>

    <div class="tank-stats">
      <div class="stat-item">
        <div class="stat-label">📡 Wi-Fi Signal</div>
        <div class="stat-val" id="stat-rssi">${fmtRssi(tank.rssi)}</div>
      </div>
    </div>
    <div id="sensor-alert-container">${sensorAlert}</div>
    `;

    // Animate fill on first render
    setTimeout(() => {
      const rect = document.getElementById('water-rect');
      const wave = document.getElementById('water-wave');
      if (rect) {
        rect.style.transition = 'none';
        wave.style.transition = 'none';
        rect.setAttribute('height', '0');
        rect.setAttribute('y', '95');
        wave.setAttribute('d', `M 0 95 Q 15 92 30 95 T 60 95 T 90 95 T 120 95 L 120 100 L 0 100 Z`);
        requestAnimationFrame(() => requestAnimationFrame(() => {
          rect.style.transition = '';
          wave.style.transition = '';
          rect.setAttribute('height', FILL_H);
          rect.setAttribute('y', FILL_Y);
          wave.setAttribute('d', `M 0 ${FILL_Y} Q 15 ${FILL_Y-3} 30 ${FILL_Y} T 60 ${FILL_Y} T 90 ${FILL_Y} T 120 ${FILL_Y} L 120 100 L 0 100 Z`);
        }));
      }
    }, 50);

  } else {
    // Soft update: only touch changed DOM nodes so CSS transitions play
    const visual = document.getElementById('tank-visual-container');
    const levelInfo = document.getElementById('level-info-container');
    const progCont = document.getElementById('progress-container');

    if (visual)    visual.className    = `tank-visual ${lvlClass}`;
    if (levelInfo) levelInfo.className = `tank-level-info ${lvlClass}`;
    if (progCont)  progCont.className  = `tank-progress ${lvlClass}`;

    document.getElementById('status-badge-container').innerHTML = statusBadge;
    document.getElementById('overlay-pct').innerText  = tank.level_pct !== null ? pct + '%' : '—';
    document.getElementById('info-pct').innerText     = fmtPct(tank.level_pct);
    document.getElementById('info-vol').innerText     = `${fmt(tank.volume_l)} L`;
    document.getElementById('progress-fill').style.width = `${tank.level_pct ?? 0}%`;
    document.getElementById('stat-rssi').innerText    = fmtRssi(tank.rssi);
    document.getElementById('sensor-alert-container').innerHTML = sensorAlert;

    const rect = document.getElementById('water-rect');
    if (rect) {
      rect.setAttribute('height', FILL_H);
      rect.setAttribute('y', FILL_Y);
      rect.setAttribute('fill', levelColor(tank.level_pct));
    }
    const wave = document.getElementById('water-wave');
    if (wave) {
      wave.setAttribute('d', `M 0 ${FILL_Y} Q 15 ${FILL_Y-3} 30 ${FILL_Y} T 60 ${FILL_Y} T 90 ${FILL_Y} T 120 ${FILL_Y} L 120 100 L 0 100 Z`);
      wave.setAttribute('fill', levelColor(tank.level_pct));
    }
  }
}

function statusBadgeHTML(state) {
  const map = {
    online:  ['badge-online',  'Online'],
    stale:   ['badge-stale',   'Stale'],
    lost:    ['badge-lost',    'Lost'],
    waiting: ['badge-waiting', 'Waiting'],
  };
  const [cls, label] = map[state] || ['badge-waiting', state || 'Unknown'];
  return `<span class="tank-status-badge ${cls}">${label}</span>`;
}

// ── Init ───────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {
  document.body.insertAdjacentHTML('afterbegin', SVG_DEFS);
  log('info', 'Standalone ESP8266 App ready');
  autoConnect();
});
