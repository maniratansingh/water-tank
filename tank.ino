/**
 * ============================================================
 *  ESP8266 Water Tank Level Monitor
 *  Single-file Arduino sketch — open in Arduino IDE and upload.
 * ============================================================
 *
 *  Hardware
 *  --------
 *  Board  : ESP8266 (NodeMCU 1.0 / Wemos D1 Mini)
 *  Sensor : JSN-SR04M waterproof ultrasonic
 *  TRIG   : GPIO 12  (NodeMCU label D6)
 *  ECHO   : GPIO 13  (NodeMCU label D7)
 *
 *  Required libraries  (install via Arduino Library Manager)
 *  ---------------------------------------------------------
 *  • ArduinoJson        >= 6.x   (Benoit Blanchon)
 *  • ESPAsyncTCP                 (me-no-dev)
 *  • ESPAsyncWebServer           (me-no-dev)
 *
 *  Usage
 *  -----
 *  1. Set your Wi-Fi SSID and password below.
 *  2. Flash to the board (Tools → Board → NodeMCU 1.0).
 *  3. Open Serial Monitor at 115200 baud — note the IP address.
 *  4. Navigate to http://<ip>/ in any browser on the same network.
 * ============================================================
 */

// ── Library includes ─────────────────────────────────────────────
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <pgmspace.h>

// ── Wi-Fi credentials ─────────────────────────────────────────────
const char *ssid     = "YOUR_SSID";      // <-- change me
const char *password = "YOUR_PASSWORD";  // <-- change me

// ── Sensor pins ───────────────────────────────────────────────────
#define TRIG_PIN 12   // D6 on NodeMCU
#define ECHO_PIN 13   // D7 on NodeMCU

// ── Calibration defaults (adjustable from the web UI) ─────────────
float empty_distance_cm = 100.0;  // Sensor → tank bottom when empty
float full_distance_cm  =  20.0;  // Sensor → water surface when full
int   capacity_l        = 1000;   // Total tank capacity in litres

// ── Runtime state ─────────────────────────────────────────────────
float current_distance = -1.0;
int   current_pct      = 0;
int   current_vol      = 0;
bool  sensor_error     = false;

// ── Timing ────────────────────────────────────────────────────────
unsigned long lastReadTime      = 0;
unsigned long lastBroadcastTime = 0;
const unsigned long READ_INTERVAL      = 2000; // ms — sensor poll rate
const unsigned long BROADCAST_INTERVAL = 5000; // ms — WebSocket push rate

// ── Sensor tuning ─────────────────────────────────────────────────
#define SENSOR_SAMPLES         5
#define SENSOR_SAMPLE_DELAY_MS 50

// ── Servers ───────────────────────────────────────────────────────
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ── Web assets (stored in flash, served over HTTP) ────────────────

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Water Tank Monitor — ESP8266</title>
  <link rel="preconnect" href="https://fonts.googleapis.com" />
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin />
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700;800&family=JetBrains+Mono:wght@400;500&display=swap" rel="stylesheet" />
  <link rel="stylesheet" href="style.css" />
</head>
<body>
  <!-- Settings Modal -->
  <div id="settings-modal" class="modal-overlay hidden">
    <div class="modal-box" role="dialog" aria-modal="true">
      <div class="modal-header">
        <h2 class="modal-title">Tank Settings</h2>
        <button class="btn-icon-only" onclick="toggleSettings(false)">✕</button>
      </div>
      <p class="modal-sub">Configure sensor calibration directly on the device.</p>
      <div class="form-group">
        <label for="set-capacity">Total Capacity (Liters)</label>
        <input id="set-capacity" type="number" placeholder="1000" />
      </div>
      <div class="form-group">
        <label for="set-empty">Empty Distance (cm)</label>
        <p class="input-hint">Distance from sensor to tank bottom when empty</p>
        <input id="set-empty" type="number" placeholder="100" />
      </div>
      <div class="form-group">
        <label for="set-full">Full Distance (cm)</label>
        <p class="input-hint">Distance from sensor to water surface when full</p>
        <input id="set-full" type="number" placeholder="20" />
      </div>
      <button class="btn-primary" onclick="saveSettings()">
        <span class="btn-icon">💾</span> Save to ESP8266
      </button>
    </div>
  </div>

  <!-- Main App -->
  <div id="app" class="app">
    <header class="app-header">
      <div class="header-left">
        <div class="header-logo">
          <svg viewBox="0 0 32 32" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M16 2C16 2 6 10 6 19a10 10 0 0 0 20 0C26 10 16 2 16 2z" fill="url(#hDropGrad)"/>
            <defs>
              <linearGradient id="hDropGrad" x1="16" y1="2" x2="16" y2="29" gradientUnits="userSpaceOnUse">
                <stop offset="0%" stop-color="#60c8ff"/>
                <stop offset="100%" stop-color="#0070f3"/>
              </linearGradient>
            </defs>
          </svg>
        </div>
        <div>
          <h1 class="header-title">Water Monitor</h1>
          <p class="header-sub">ESP8266 Live Dashboard</p>
        </div>
      </div>
      <div class="header-right">
        <div id="ws-status" class="ws-badge ws-connecting">
          <span class="ws-dot"></span>
          <span id="ws-label">Connecting…</span>
        </div>
        <button id="settings-btn" class="btn-icon-only" title="Settings" onclick="toggleSettings(true)">⚙️</button>
      </div>
    </header>

    <main class="single-tank-layout" id="tank-card-container">
      <!-- Populated by app.js -->
    </main>

    <section class="log-section">
      <div class="log-header">
        <span class="log-title">📡 Event Log</span>
        <button class="btn-ghost-sm" onclick="clearLog()">Clear</button>
      </div>
      <div id="event-log" class="log-body"></div>
    </section>
  </div>

  <div id="toast-container"></div>
  <script src="app.js"></script>
</body>
</html>
)rawliteral";

// ─────────────────────────────────────────────────────────────────

const char style_css[] PROGMEM = R"rawliteral(
/* ============================================================
   Water Tank Monitor — CSS Design System
   ============================================================ */

:root {
  --bg-base:       #080c14;
  --bg-card:       rgba(255,255,255,0.04);
  --bg-card-hover: rgba(255,255,255,0.07);
  --bg-glass:      rgba(12,20,36,0.85);
  --border:        rgba(255,255,255,0.08);
  --border-strong: rgba(255,255,255,0.15);
  --blue:          #0070f3;
  --blue-light:    #60c8ff;
  --cyan:          #00d9ff;
  --green:         #22c55e;
  --yellow:        #f59e0b;
  --orange:        #f97316;
  --red:           #ef4444;
  --text-1:        #f0f4ff;
  --text-2:        #8892aa;
  --text-3:        #4a5568;
  --radius-sm:     8px;
  --radius-md:     14px;
  --radius-lg:     20px;
  --radius-xl:     28px;
  --shadow-card:   0 4px 32px rgba(0,0,0,0.4);
  --font-body:     'Inter', system-ui, sans-serif;
  --font-mono:     'JetBrains Mono', 'Fira Code', monospace;
  --transition:    all 0.2s cubic-bezier(0.4,0,0.2,1);
}

*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

body {
  font-family: var(--font-body);
  background-color: var(--bg-base);
  color: var(--text-1);
  min-height: 100vh;
  overflow-x: hidden;
  line-height: 1.6;
}

body::before {
  content: '';
  position: fixed;
  inset: 0;
  background:
    radial-gradient(ellipse 80% 60% at 20% 0%, rgba(0,112,243,0.12) 0%, transparent 60%),
    radial-gradient(ellipse 60% 50% at 80% 100%, rgba(0,217,255,0.08) 0%, transparent 60%);
  pointer-events: none;
  z-index: 0;
}

.hidden { display: none !important; }

/* Modal */
.modal-overlay {
  position: fixed; inset: 0;
  background: rgba(8,12,20,0.92);
  backdrop-filter: blur(12px);
  display: flex; align-items: center; justify-content: center;
  z-index: 1000;
  animation: fadeIn 0.3s ease;
}
.modal-box {
  background: rgba(14,22,38,0.95);
  border: 1px solid var(--border-strong);
  border-radius: var(--radius-xl);
  padding: 2.5rem 2.5rem 2rem;
  width: min(460px, 94vw);
  box-shadow: 0 0 80px rgba(0,112,243,0.15), var(--shadow-card);
  animation: slideUp 0.4s cubic-bezier(0.34,1.56,0.64,1);
}
.modal-header {
  display: flex; justify-content: space-between;
  align-items: center; margin-bottom: 0.5rem;
}
.modal-title {
  font-size: 1.75rem; font-weight: 800;
  background: linear-gradient(135deg, #f0f4ff 0%, var(--blue-light) 100%);
  -webkit-background-clip: text; -webkit-text-fill-color: transparent;
  background-clip: text; letter-spacing: -0.02em;
}
.modal-sub { color: var(--text-2); font-size: 0.875rem; margin-top: 0.4rem; margin-bottom: 2rem; }

/* Form */
.form-group { margin-bottom: 1.25rem; }
.form-group label {
  display: block; font-size: 0.8125rem; font-weight: 600;
  color: var(--text-2); text-transform: uppercase;
  letter-spacing: 0.06em; margin-bottom: 0.5rem;
}
.input-hint { font-size: 0.8rem; color: #a0aec0; margin-top: -0.25rem; margin-bottom: 0.5rem; }

input[type="number"] {
  width: 100%;
  background: rgba(255,255,255,0.04);
  border: 1px solid var(--border-strong);
  border-radius: var(--radius-sm);
  padding: 0.625rem 0.875rem;
  color: var(--text-1);
  font-family: var(--font-mono); font-size: 0.875rem;
  outline: none; transition: var(--transition);
}
input[type="number"]:focus {
  border-color: var(--blue);
  box-shadow: 0 0 0 3px rgba(0,112,243,0.2);
}
input::placeholder { color: var(--text-3); }

/* Buttons */
.btn-primary {
  width: 100%; padding: 0.875rem 1.5rem;
  background: linear-gradient(135deg, var(--blue) 0%, #0052cc 100%);
  color: #fff; border: none; border-radius: var(--radius-sm);
  font-family: var(--font-body); font-size: 0.9375rem; font-weight: 700;
  cursor: pointer; display: flex; align-items: center; justify-content: center;
  gap: 0.5rem; transition: var(--transition);
  box-shadow: 0 4px 16px rgba(0,112,243,0.4); margin-top: 0.5rem;
}
.btn-primary:hover { transform: translateY(-1px); box-shadow: 0 6px 24px rgba(0,112,243,0.5); }
.btn-ghost-sm {
  background: transparent; border: 1px solid var(--border); color: var(--text-2);
  font-family: var(--font-body); font-size: 0.75rem; cursor: pointer;
  padding: 0.25rem 0.625rem; border-radius: 4px; transition: var(--transition);
}
.btn-ghost-sm:hover { border-color: var(--border-strong); color: var(--text-1); }
.btn-icon-only {
  background: rgba(255,255,255,0.05); border: 1px solid var(--border);
  color: var(--text-2); width: 32px; height: 32px;
  border-radius: var(--radius-sm); cursor: pointer; font-size: 0.75rem;
  transition: var(--transition); display: flex; align-items: center; justify-content: center;
}
.btn-icon-only:hover { background: rgba(239,68,68,0.15); border-color: rgba(239,68,68,0.4); color: var(--red); }

/* App shell */
.app { position: relative; z-index: 1; min-height: 100vh; display: flex; flex-direction: column; }
.app-header {
  position: sticky; top: 0; z-index: 100;
  background: var(--bg-glass); backdrop-filter: blur(20px);
  border-bottom: 1px solid var(--border);
  padding: 0.875rem 1.5rem;
  display: flex; align-items: center; justify-content: space-between; gap: 1rem;
}
.header-left { display: flex; align-items: center; gap: 0.875rem; }
.header-logo {
  width: 36px; height: 36px;
  background: radial-gradient(circle, rgba(0,112,243,0.2), transparent);
  border-radius: 50%; border: 1px solid rgba(0,112,243,0.3);
  display: flex; align-items: center; justify-content: center;
  padding: 6px; flex-shrink: 0;
}
.header-logo svg { width: 100%; height: 100%; }
.header-title {
  font-size: 1rem; font-weight: 800; letter-spacing: -0.02em;
  background: linear-gradient(135deg, #f0f4ff, var(--blue-light));
  -webkit-background-clip: text; -webkit-text-fill-color: transparent; background-clip: text;
}
.header-sub { font-size: 0.7rem; color: var(--text-3); font-weight: 400; margin-top: -2px; }
.header-right { display: flex; align-items: center; gap: 0.75rem; flex-wrap: wrap; justify-content: flex-end; }

/* WS badge */
.ws-badge {
  display: flex; align-items: center; gap: 0.45rem;
  padding: 0.3rem 0.75rem; border-radius: 999px;
  font-size: 0.75rem; font-weight: 600;
  border: 1px solid transparent; transition: var(--transition);
}
.ws-dot { width: 7px; height: 7px; border-radius: 50%; display: inline-block; flex-shrink: 0; }
.ws-connecting { background: rgba(245,158,11,0.12); border-color: rgba(245,158,11,0.3); color: var(--yellow); }
.ws-connecting .ws-dot { background: var(--yellow); animation: pulse 1.2s ease-in-out infinite; }
.ws-connected { background: rgba(34,197,94,0.12); border-color: rgba(34,197,94,0.3); color: var(--green); }
.ws-connected .ws-dot { background: var(--green); animation: pulse 2s ease-in-out infinite; }
.ws-error, .ws-disconnected { background: rgba(239,68,68,0.12); border-color: rgba(239,68,68,0.3); color: var(--red); }
.ws-error .ws-dot, .ws-disconnected .ws-dot { background: var(--red); }

/* Tank layout */
.single-tank-layout {
  display: flex; align-items: center; justify-content: center;
  flex: 1; padding: 2rem 1.5rem;
  max-width: 520px; width: 100%; margin: 0 auto;
}

/* Tank card */
.tank-card {
  background: var(--bg-card); border: 1px solid var(--border);
  border-radius: var(--radius-lg); padding: 1.75rem;
  transition: var(--transition); position: relative;
  overflow: hidden; width: 100%; box-shadow: var(--shadow-card);
  animation: cardIn 0.4s cubic-bezier(0.34,1.56,0.64,1) both;
}
.tank-card::before {
  content: ''; position: absolute; inset: 0;
  border-radius: var(--radius-lg);
  background: linear-gradient(135deg, rgba(255,255,255,0.04) 0%, transparent 60%);
  pointer-events: none;
}
.tank-card:hover { background: var(--bg-card-hover); border-color: var(--border-strong); }
.tank-card.status-online { --card-glow: rgba(34,197,94,0.1);  border-color: rgba(34,197,94,0.25); }
.tank-card.status-stale  { --card-glow: rgba(245,158,11,0.1); border-color: rgba(245,158,11,0.25); }
.tank-card.status-lost   { --card-glow: rgba(239,68,68,0.1);  border-color: rgba(239,68,68,0.25); }
.tank-card.status-online::after, .tank-card.status-stale::after, .tank-card.status-lost::after {
  content: ''; position: absolute; inset: -1px;
  border-radius: var(--radius-lg); background: var(--card-glow); z-index: -1;
}

/* Card header */
.tank-header { display: flex; align-items: flex-start; justify-content: space-between; margin-bottom: 1.5rem; }
.tank-name-row { display: flex; flex-direction: column; gap: 0.25rem; }
.tank-name { font-size: 1.25rem; font-weight: 800; color: var(--text-1); letter-spacing: -0.01em; }
.tank-id   { font-size: 0.75rem; font-family: var(--font-mono); color: var(--text-2); }
.tank-status-badge {
  padding: 0.3rem 0.75rem; border-radius: 999px;
  font-size: 0.7rem; font-weight: 700;
  text-transform: uppercase; letter-spacing: 0.08em; flex-shrink: 0;
}
.badge-online  { background: rgba(34,197,94,0.15);   color: var(--green);  border: 1px solid rgba(34,197,94,0.3); }
.badge-stale   { background: rgba(245,158,11,0.15);  color: var(--yellow); border: 1px solid rgba(245,158,11,0.3); }
.badge-lost    { background: rgba(239,68,68,0.15);   color: var(--red);    border: 1px solid rgba(239,68,68,0.3); }
.badge-waiting { background: rgba(138,100,255,0.15); color: #a78bfa;       border: 1px solid rgba(138,100,255,0.3); }

/* Tank visual */
.tank-visual { display: flex; align-items: center; gap: 1.75rem; margin-bottom: 1.5rem; }
.tank-svg-wrap { position: relative; width: 100px; flex-shrink: 0; }
.tank-svg-wrap svg { width: 100px; height: 140px; display: block; }
.tank-water {
  transition: height 1s cubic-bezier(0.4,0,0.2,1),
              y      1s cubic-bezier(0.4,0,0.2,1), fill 0.5s;
}
.tank-wave { animation: waveScroll 3s linear infinite; transition: transform 1s cubic-bezier(0.4,0,0.2,1), fill 0.5s; }
@keyframes waveScroll { from { transform: translateX(0); } to { transform: translateX(-60px); } }
.tank-pct-overlay {
  position: absolute; bottom: 16px; left: 0; right: 0;
  text-align: center; font-size: 0.875rem; font-weight: 800;
  font-family: var(--font-mono); color: #fff;
  text-shadow: 0 1px 4px rgba(0,0,0,0.6); pointer-events: none;
}

/* Level info */
.tank-level-info { flex: 1; }
.level-pct { font-size: 3.5rem; font-weight: 800; font-family: var(--font-mono); line-height: 1; letter-spacing: -0.03em; }
.level-unit { font-size: 1.25rem; font-weight: 600; color: var(--text-2); margin-left: 2px; }
.level-volume { font-size: 0.95rem; color: var(--text-2); margin-top: 6px; }
.level-volume strong { color: var(--text-1); font-weight: 700; }

/* Progress bar */
.tank-progress { margin-bottom: 1.5rem; }
.progress-track {
  height: 8px; background: rgba(255,255,255,0.07);
  border-radius: 999px; overflow: hidden; position: relative;
}
.progress-fill {
  height: 100%; border-radius: 999px;
  transition: width 1s cubic-bezier(0.4,0,0.2,1); position: relative;
}
.progress-fill::after {
  content: ''; position: absolute; right: 0; top: 0; bottom: 0; width: 20px;
  background: linear-gradient(90deg, transparent, rgba(255,255,255,0.3));
  border-radius: 999px;
}

/* Level color classes */
.level-critical .level-pct { color: var(--red); }
.level-low      .level-pct { color: var(--orange); }
.level-mid      .level-pct { color: var(--yellow); }
.level-good     .level-pct { color: var(--green); }
.level-full     .level-pct { color: var(--cyan); }
.level-critical .progress-fill { background: linear-gradient(90deg, #ef4444, #f97316); }
.level-low      .progress-fill { background: linear-gradient(90deg, #f97316, #f59e0b); }
.level-mid      .progress-fill { background: linear-gradient(90deg, #f59e0b, #22c55e); }
.level-good     .progress-fill { background: linear-gradient(90deg, #22c55e, #00d9ff); }
.level-full     .progress-fill { background: linear-gradient(90deg, #00d9ff, #0070f3); }
.level-critical .tank-water, .level-critical .tank-wave { fill: url(#waterCritical); }
.level-low      .tank-water, .level-low      .tank-wave { fill: url(#waterLow); }
.level-mid      .tank-water, .level-mid      .tank-wave { fill: url(#waterMid); }
.level-good     .tank-water, .level-good     .tank-wave { fill: url(#waterGood); }
.level-full     .tank-water, .level-full     .tank-wave { fill: url(#waterFull); }

/* Stats grid */
.tank-stats { display: grid; grid-template-columns: repeat(2, 1fr); gap: 0.75rem; }
.stat-item {
  background: rgba(255,255,255,0.03); border: 1px solid var(--border);
  border-radius: var(--radius-sm); padding: 0.625rem;
  text-align: center; transition: var(--transition);
}
.stat-item:hover { background: rgba(255,255,255,0.06); border-color: var(--border-strong); }
.stat-label { font-size: 0.65rem; text-transform: uppercase; letter-spacing: 0.08em; color: var(--text-3); font-weight: 600; margin-bottom: 4px; }
.stat-val   { font-size: 0.9rem; font-weight: 700; font-family: var(--font-mono); color: var(--text-1); }

/* Sensor alert */
.sensor-alert {
  margin-top: 1rem; padding: 0.625rem 0.875rem;
  background: rgba(239,68,68,0.1); border: 1px solid rgba(239,68,68,0.25);
  border-radius: var(--radius-sm); font-size: 0.75rem; color: #fca5a5;
  display: flex; align-items: center; gap: 0.4rem;
}

/* Toasts */
#toast-container { position: fixed; bottom: 1.5rem; right: 1.5rem; display: flex; flex-direction: column; gap: 0.5rem; z-index: 9999; }
.toast {
  padding: 0.75rem 1.25rem; border-radius: var(--radius-md);
  font-size: 0.8125rem; font-weight: 500; backdrop-filter: blur(12px);
  animation: toastIn 0.3s cubic-bezier(0.34,1.56,0.64,1);
  box-shadow: var(--shadow-card); max-width: 320px;
}
.toast-info    { background: rgba(0,112,243,0.85);  border: 1px solid rgba(96,200,255,0.3); color: #fff; }
.toast-success { background: rgba(22,163,74,0.85);  border: 1px solid rgba(34,197,94,0.3);  color: #fff; }
.toast-warn    { background: rgba(180,83,9,0.85);   border: 1px solid rgba(245,158,11,0.3); color: #fff; }
.toast-error   { background: rgba(185,28,28,0.85);  border: 1px solid rgba(239,68,68,0.3);  color: #fff; }

/* Log panel */
.log-section {
  margin: 0 1.5rem 1.5rem;
  background: var(--bg-card); border: 1px solid var(--border);
  border-radius: var(--radius-md); overflow: hidden;
}
.log-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 0.625rem 1rem; background: rgba(255,255,255,0.02);
  border-bottom: 1px solid var(--border);
}
.log-title { font-size: 0.8125rem; font-weight: 600; color: var(--text-2); }
.log-body {
  height: 120px; overflow-y: auto; padding: 0.5rem;
  display: flex; flex-direction: column-reverse; gap: 2px;
}
.log-body::-webkit-scrollbar       { width: 4px; }
.log-body::-webkit-scrollbar-track { background: transparent; }
.log-body::-webkit-scrollbar-thumb { background: var(--border-strong); border-radius: 999px; }
.log-entry {
  padding: 0.3rem 0.625rem; border-radius: 4px;
  font-size: 0.72rem; font-family: var(--font-mono);
  display: flex; gap: 0.75rem; align-items: baseline;
  animation: logIn 0.2s ease;
}
.log-entry:hover { background: rgba(255,255,255,0.03); }
.log-time { color: var(--text-3); flex-shrink: 0; }
.log-type { flex-shrink: 0; font-weight: 600; }
.log-msg  { color: var(--text-2); }
.log-info    .log-type { color: var(--blue-light); }
.log-warn    .log-type { color: var(--yellow); }
.log-error   .log-type { color: var(--red); }
.log-success .log-type { color: var(--green); }
.log-data    .log-type { color: #a78bfa; }

/* Animations */
@keyframes fadeIn  { from { opacity: 0; } to { opacity: 1; } }
@keyframes slideUp { from { opacity: 0; transform: translateY(24px) scale(0.96); } to { opacity: 1; transform: translateY(0) scale(1); } }
@keyframes cardIn  { from { opacity: 0; transform: translateY(16px); } to { opacity: 1; transform: translateY(0); } }
@keyframes logIn   { from { opacity: 0; transform: translateX(-8px); } to { opacity: 1; transform: translateX(0); } }
@keyframes toastIn { from { opacity: 0; transform: translateX(16px) scale(0.95); } to { opacity: 1; transform: translateX(0) scale(1); } }
@keyframes toastOut { to { opacity: 0; transform: translateX(16px) scale(0.95); } }
@keyframes pulse { 0%, 100% { opacity: 1; transform: scale(1); } 50% { opacity: 0.5; transform: scale(0.85); } }

/* Responsive */
@media (max-width: 640px) {
  .app-header         { padding: 0.75rem 1rem; }
  .single-tank-layout { padding: 1.5rem 1rem; }
  .log-section        { margin: 0 1rem 1rem; }
  .modal-box          { padding: 2rem 1.5rem 1.5rem; }
}
)rawliteral";

// ─────────────────────────────────────────────────────────────────

const char app_js[] PROGMEM = R"rawliteral(
/* ================================================================
   Standalone ESP8266 TankSync Dashboard — WebSocket Client
   ================================================================ */
'use strict';

let ws = null;
let tankData = null;
let reconnectAttempts = 0;
const MAX_RECONNECT = 8;

// SVG gradient definitions injected into the DOM once at startup
const SVG_DEFS = `
<svg aria-hidden="true" style="position:absolute;width:0;height:0;">
  <defs>
    <linearGradient id="waterCritical" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%"   stop-color="#f97316"/>
      <stop offset="100%" stop-color="#ef4444"/>
    </linearGradient>
    <linearGradient id="waterLow" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%"   stop-color="#f59e0b"/>
      <stop offset="100%" stop-color="#f97316"/>
    </linearGradient>
    <linearGradient id="waterMid" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%"   stop-color="#22c55e"/>
      <stop offset="100%" stop-color="#f59e0b"/>
    </linearGradient>
    <linearGradient id="waterGood" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%"   stop-color="#00d9ff"/>
      <stop offset="100%" stop-color="#22c55e"/>
    </linearGradient>
    <linearGradient id="waterFull" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%"   stop-color="#60c8ff"/>
      <stop offset="100%" stop-color="#0070f3"/>
    </linearGradient>
  </defs>
</svg>`;

// ── Helpers ───────────────────────────────────────────────────────
const $       = id => document.getElementById(id);
const fmt     = n  => (n == null || isNaN(n)) ? '—' : Number(n).toLocaleString('en-IN');
const fmtPct  = n  => (n == null) ? '—' : `${Math.round(n)}`;
const fmtRssi = r  => r ? `${r} dBm` : '—';
const now     = () => new Date().toLocaleTimeString('en-IN', { hour12: false });

function levelClass(pct) {
  if (pct == null) return '';
  if (pct <= 10)   return 'level-critical';
  if (pct <= 25)   return 'level-low';
  if (pct <= 50)   return 'level-mid';
  if (pct <= 85)   return 'level-good';
  return 'level-full';
}

function levelColor(pct) {
  if (pct == null) return '#4a5568';
  if (pct <= 10)   return 'url(#waterCritical)';
  if (pct <= 25)   return 'url(#waterLow)';
  if (pct <= 50)   return 'url(#waterMid)';
  if (pct <= 85)   return 'url(#waterGood)';
  return 'url(#waterFull)';
}

function escHtml(s) {
  return String(s)
    .replace(/&/g,'&amp;').replace(/</g,'&lt;')
    .replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

// ── Event log ─────────────────────────────────────────────────────
function log(type, msg) {
  const el = $('event-log');
  if (!el) return;
  const labels = { info:'INFO', warn:'WARN', error:'ERR!', success:'OK  ', data:'DATA' };
  const div = document.createElement('div');
  div.className = `log-entry log-${type}`;
  div.innerHTML =
    `<span class="log-time">${now()}</span>` +
    `<span class="log-type">${labels[type] || type.toUpperCase()}</span>` +
    `<span class="log-msg">${escHtml(msg)}</span>`;
  el.prepend(div);
  while (el.children.length > 50) el.removeChild(el.lastChild);
}

function clearLog() { const el = $('event-log'); if (el) el.innerHTML = ''; }

// ── Toasts ────────────────────────────────────────────────────────
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

// ── WS status ─────────────────────────────────────────────────────
function setWsStatus(state, label) {
  const badge = $('ws-status'), lbl = $('ws-label');
  if (badge) badge.className = `ws-badge ws-${state}`;
  if (lbl)   lbl.textContent  = label;
}

// ── Settings modal ────────────────────────────────────────────────
function toggleSettings(show) {
  $('settings-modal').classList.toggle('hidden', !show);
}

function saveSettings() {
  const empty = parseInt($('set-empty').value);
  const full  = parseInt($('set-full').value);
  const cap   = parseInt($('set-capacity').value);
  if (isNaN(empty) || isNaN(full) || isNaN(cap)) { toast('Please enter valid numbers', 'warn'); return; }
  if (empty <= full) { toast('Empty distance must be greater than full distance', 'error'); return; }
  if (!ws || ws.readyState !== WebSocket.OPEN) { toast('Not connected to device', 'error'); return; }
  ws.send(JSON.stringify({ type:'update_settings', empty_distance_cm:empty, full_distance_cm:full, capacity_l:cap }));
  toast('Settings saved to ESP8266', 'success');
  toggleSettings(false);
}

// ── WebSocket connection ───────────────────────────────────────────
function autoConnect() {
  const ip  = window.location.host;
  const url = ip ? `ws://${ip}/ws` : 'ws://192.168.1.100/ws';
  log('info', `Connecting to ${url}…`);
  openWebSocket(url);
}

function openWebSocket(url) {
  if (ws) ws.close();
  setWsStatus('connecting', 'Connecting…');
  try { ws = new WebSocket(url); } catch (e) {
    log('error', `WebSocket failed: ${e.message}`);
    setWsStatus('error', 'Error');
    scheduleReconnect(url); return;
  }
  ws.addEventListener('open', () => {
    reconnectAttempts = 0;
    setWsStatus('connected', 'Live');
    log('success', `Connected to ${url}`);
    toast('Connected to Sensor 🎉', 'success');
  });
  ws.addEventListener('message', e => {
    try { handleMessage(JSON.parse(e.data)); }
    catch { log('warn', `Bad frame: ${e.data.slice(0,60)}`); }
  });
  ws.addEventListener('close', evt => {
    setWsStatus('disconnected', 'Disconnected');
    log('warn', `Closed (code ${evt.code})`);
    toast('Disconnected — reconnecting…', 'warn');
    scheduleReconnect(url);
  });
  ws.addEventListener('error', () => { setWsStatus('error', 'Error'); log('error', 'WebSocket error'); });
}

function scheduleReconnect(url) {
  if (reconnectAttempts >= MAX_RECONNECT) {
    log('error', 'Max retries reached — please refresh the page.');
    toast('Could not reconnect. Refresh the page.', 'error', 6000); return;
  }
  const delay = Math.min(2000 * 2 ** reconnectAttempts, 30000);
  reconnectAttempts++;
  log('info', `Retry ${reconnectAttempts}/${MAX_RECONNECT} in ${(delay/1000).toFixed(0)}s…`);
  setWsStatus('connecting', `Retry ${reconnectAttempts}…`);
  setTimeout(() => openWebSocket(url), delay);
}

// ── Message handler ────────────────────────────────────────────────
function handleMessage(msg) {
  if (msg.type !== 'state') { log('info', `Unknown msg type: "${msg.type}"`); return; }

  // Sync settings fields only when the modal is closed (don't overwrite mid-edit)
  if (msg.settings && $('settings-modal').classList.contains('hidden')) {
    $('set-empty').value = msg.settings.empty_distance_cm;
    $('set-full').value  = msg.settings.full_distance_cm;
  }

  const devs = msg.devices || [];
  if (!devs.length) return;
  if (msg.settings) $('set-capacity').value = devs[0].capacity_l;
  updateTank(devs[0]);
}

// ── Tank state update ──────────────────────────────────────────────
function updateTank(d) {
  const state  = d.state || {};
  const isNew  = (tankData === null);

  tankData = {
    id:           d.tank,
    name:         d.name || `Tank ${d.tank}`,
    capacity_l:   d.capacity_l || 0,
    level_pct:    state.level_pct    ?? null,
    rssi:         state.rssi         ?? null,
    conn_state:   state.conn_state   || 'waiting',
    sensor_error: state.sensor_error || false,
    sensor_stuck: state.sensor_stuck || false,
  };
  tankData.volume_l = (tankData.capacity_l && tankData.level_pct !== null)
    ? Math.round(tankData.capacity_l * tankData.level_pct / 100) : null;

  renderTankCard(tankData, isNew);
}

// ── Tank card renderer ─────────────────────────────────────────────
function renderTankCard(tank, isNew) {
  const container = $('tank-card-container');
  if (!container) return;

  if (!container.firstElementChild) {
    container.innerHTML = `<div class="tank-card" id="main-tank-card"></div>`;
    isNew = true;
  }
  const card = $('main-tank-card');
  if (!card) return;

  card.className = `tank-card status-${tank.conn_state}`;

  const pct         = tank.level_pct ?? 0;
  const lvlClass    = levelClass(tank.level_pct);
  const statusBadge = statusBadgeHTML(tank.conn_state);
  const sensorAlert = (tank.sensor_error || tank.sensor_stuck)
    ? `<div class="sensor-alert">⚠ ${tank.sensor_error
        ? 'Sensor too close or offline — check position.'
        : 'Sensor reading is stuck.'}</div>` : '';

  const SVG_H  = 90;
  const FILL_H = Math.max(0, Math.min(SVG_H, Math.round(SVG_H * pct / 100)));
  const FILL_Y = SVG_H - FILL_H + 5;

  if (isNew) {
    card.innerHTML = buildCardHTML(tank, pct, lvlClass, statusBadge, sensorAlert, FILL_H, FILL_Y);
    animateFillIn(FILL_H, FILL_Y);
  } else {
    softUpdateCard(tank, pct, lvlClass, statusBadge, sensorAlert, FILL_H, FILL_Y);
  }
}

function buildCardHTML(tank, pct, lvlClass, statusBadge, sensorAlert, FILL_H, FILL_Y) {
  const SVG_H = 90;
  const color = levelColor(tank.level_pct);
  return `
  <div class="tank-header">
    <div class="tank-name-row">
      <span class="tank-name">${escHtml(tank.name)}</span>
      <span class="tank-id">${fmt(tank.capacity_l)} Litre Capacity</span>
    </div>
    <div id="status-badge-wrap">${statusBadge}</div>
  </div>
  <div class="tank-visual ${lvlClass}" id="tank-visual">
    <div class="tank-svg-wrap">
      <svg viewBox="0 0 60 100" fill="none" xmlns="http://www.w3.org/2000/svg">
        <rect x="4" y="5" width="52" height="90" rx="6"
          fill="rgba(255,255,255,0.04)" stroke="rgba(255,255,255,0.12)" stroke-width="1.5"/>
        <g clip-path="url(#tClip)">
          <rect class="tank-water" id="water-rect"
            x="5.5" y="${FILL_Y}" width="49" height="${FILL_H}"
            rx="${FILL_H > 5 ? 4 : 2}" fill="${color}"/>
          <path class="tank-wave" id="water-wave" fill="${color}" opacity="0.6"
            d="M 0 ${FILL_Y} Q 15 ${FILL_Y-3} 30 ${FILL_Y} T 60 ${FILL_Y} T 90 ${FILL_Y} T 120 ${FILL_Y} L 120 100 L 0 100 Z"/>
        </g>
        <rect x="5.5" y="5.75" width="49" height="89" rx="5" fill="url(#sheen)" opacity="0.3"/>
        <defs>
          <clipPath id="tClip"><rect x="5.5" y="5.75" width="49" height="89" rx="5"/></clipPath>
          <linearGradient id="sheen" x1="0" y1="0" x2="1" y2="0">
            <stop offset="0%"   stop-color="white" stop-opacity="0.08"/>
            <stop offset="40%"  stop-color="white" stop-opacity="0"/>
            <stop offset="100%" stop-color="white" stop-opacity="0"/>
          </linearGradient>
        </defs>
        <line x1="4" y1="${5+SVG_H*0.25}" x2="8" y2="${5+SVG_H*0.25}" stroke="rgba(255,255,255,0.15)" stroke-width="1"/>
        <line x1="4" y1="${5+SVG_H*0.5}"  x2="8" y2="${5+SVG_H*0.5}"  stroke="rgba(255,255,255,0.15)" stroke-width="1"/>
        <line x1="4" y1="${5+SVG_H*0.75}" x2="8" y2="${5+SVG_H*0.75}" stroke="rgba(255,255,255,0.15)" stroke-width="1"/>
      </svg>
      <div class="tank-pct-overlay" id="overlay-pct">${tank.level_pct !== null ? pct + '%' : '—'}</div>
    </div>
    <div class="tank-level-info ${lvlClass}" id="level-info">
      <div>
        <span class="level-pct" id="info-pct">${fmtPct(tank.level_pct)}</span>
        <span class="level-unit">%</span>
      </div>
      <div class="level-volume"><strong id="info-vol">${fmt(tank.volume_l)} L</strong> available</div>
    </div>
  </div>
  <div class="tank-progress ${lvlClass}" id="progress-wrap">
    <div class="progress-track">
      <div class="progress-fill" id="progress-fill" style="width:${pct}%"></div>
    </div>
  </div>
  <div class="tank-stats">
    <div class="stat-item">
      <div class="stat-label">📡 Wi-Fi Signal</div>
      <div class="stat-val" id="stat-rssi">${fmtRssi(tank.rssi)}</div>
    </div>
  </div>
  <div id="sensor-alert-wrap">${sensorAlert}</div>`;
}

function animateFillIn(FILL_H, FILL_Y) {
  setTimeout(() => {
    const rect = $('water-rect'), wave = $('water-wave');
    if (!rect) return;
    rect.style.transition = 'none'; wave.style.transition = 'none';
    rect.setAttribute('height', '0'); rect.setAttribute('y', '95');
    wave.setAttribute('d', 'M 0 95 Q 15 92 30 95 T 60 95 T 90 95 T 120 95 L 120 100 L 0 100 Z');
    requestAnimationFrame(() => requestAnimationFrame(() => {
      rect.style.transition = ''; wave.style.transition = '';
      rect.setAttribute('height', FILL_H); rect.setAttribute('y', FILL_Y);
      wave.setAttribute('d', `M 0 ${FILL_Y} Q 15 ${FILL_Y-3} 30 ${FILL_Y} T 60 ${FILL_Y} T 90 ${FILL_Y} T 120 ${FILL_Y} L 120 100 L 0 100 Z`);
    }));
  }, 50);
}

function softUpdateCard(tank, pct, lvlClass, statusBadge, sensorAlert, FILL_H, FILL_Y) {
  const color = levelColor(tank.level_pct);

  const visual = $('tank-visual'),    li = $('level-info'),    pw = $('progress-wrap');
  if (visual) visual.className = `tank-visual ${lvlClass}`;
  if (li)     li.className     = `tank-level-info ${lvlClass}`;
  if (pw)     pw.className     = `tank-progress ${lvlClass}`;

  $('status-badge-wrap').innerHTML       = statusBadge;
  $('overlay-pct').innerText             = tank.level_pct !== null ? pct + '%' : '—';
  $('info-pct').innerText                = fmtPct(tank.level_pct);
  $('info-vol').innerText                = `${fmt(tank.volume_l)} L`;
  $('progress-fill').style.width         = `${pct}%`;
  $('stat-rssi').innerText               = fmtRssi(tank.rssi);
  $('sensor-alert-wrap').innerHTML       = sensorAlert;

  const rect = $('water-rect');
  if (rect) { rect.setAttribute('height', FILL_H); rect.setAttribute('y', FILL_Y); rect.setAttribute('fill', color); }
  const wave = $('water-wave');
  if (wave) { wave.setAttribute('fill', color); wave.setAttribute('d', `M 0 ${FILL_Y} Q 15 ${FILL_Y-3} 30 ${FILL_Y} T 60 ${FILL_Y} T 90 ${FILL_Y} T 120 ${FILL_Y} L 120 100 L 0 100 Z`); }
}

function statusBadgeHTML(state) {
  const map = { online:['badge-online','Online'], stale:['badge-stale','Stale'], lost:['badge-lost','Lost'], waiting:['badge-waiting','Waiting'] };
  const [cls, label] = map[state] || ['badge-waiting', state || 'Unknown'];
  return `<span class="tank-status-badge ${cls}">${label}</span>`;
}

// ── Startup ────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {
  document.body.insertAdjacentHTML('afterbegin', SVG_DEFS);
  log('info', 'Dashboard ready');
  autoConnect();
});
)rawliteral";

// ─────────────────────────────────────────────────────────────────
// Sensor: IQR-filtered median
// ─────────────────────────────────────────────────────────────────

static void sort_floats(float *arr, int n) {
  for (int i = 1; i < n; i++) {
    float key = arr[i];
    int j = i - 1;
    while (j >= 0 && arr[j] > key) { arr[j + 1] = arr[j]; j--; }
    arr[j + 1] = key;
  }
}

static float readSingleSample() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 60000); // 60 ms timeout
  return (dur == 0) ? -1.0f : (dur * 0.0343f) / 2.0f;
}

void readSensor() {
  float samples[SENSOR_SAMPLES];
  int valid = 0;

  for (int i = 0; i < SENSOR_SAMPLES; i++) {
    float d = readSingleSample();
    if (d >= 5.0f && d <= 400.0f) samples[valid++] = d;
    delay(SENSOR_SAMPLE_DELAY_MS);
  }

  if (valid < 2) { sensor_error = true; current_distance = -1.0f; return; }

  sort_floats(samples, valid);

  float q1  = samples[valid / 4];
  float q3  = samples[(valid * 3) / 4];
  float iqr = q3 - q1;
  float lo  = (iqr < 2.0f) ? q1 - 5.0f : q1 - iqr * 1.5f;
  float hi  = (iqr < 2.0f) ? q3 + 5.0f : q3 + iqr * 1.5f;

  float filtered[SENSOR_SAMPLES];
  int fc = 0;
  for (int i = 0; i < valid; i++)
    if (samples[i] >= lo && samples[i] <= hi) filtered[fc++] = samples[i];

  current_distance = (fc == 0) ? samples[valid / 2] : filtered[fc / 2];
  sensor_error = false;

  float range = empty_distance_cm - full_distance_cm;
  if (range <= 0.0f) { current_pct = 0; current_vol = 0; return; }

  float level = constrain(empty_distance_cm - current_distance, 0.0f, range);
  current_pct = (int)round((level * 100.0f) / range);
  current_vol = (int)round((capacity_l * level) / range);
}

// ─────────────────────────────────────────────────────────────────
// WebSocket
// ─────────────────────────────────────────────────────────────────

void notifyClients() {
  StaticJsonDocument<512> doc;
  doc["type"] = "state";

  JsonObject tank  = doc.createNestedArray("devices").createNestedObject();
  tank["tank"]       = 1;
  tank["name"]       = "My Water Tank";
  tank["capacity_l"] = capacity_l;

  JsonObject state  = tank.createNestedObject("state");
  state["level_pct"]    = current_pct;   // kept at last good value on sensor error
  state["sensor_error"] = sensor_error;
  state["rssi"]         = WiFi.RSSI();
  state["conn_state"]   = "online";
  state["consumed_l"]   = 0;

  JsonObject settings = doc.createNestedObject("settings");
  settings["empty_distance_cm"] = empty_distance_cm;
  settings["full_distance_cm"]  = full_distance_cm;

  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) return;
  data[len] = 0;

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, (char *)data)) return;

  if (doc["type"] == "update_settings") {
    if (doc.containsKey("empty_distance_cm")) empty_distance_cm = doc["empty_distance_cm"].as<float>();
    if (doc.containsKey("full_distance_cm"))  full_distance_cm  = doc["full_distance_cm"].as<float>();
    if (doc.containsKey("capacity_l"))        capacity_l        = doc["capacity_l"].as<int>();
    readSensor();
    notifyClients();
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("[WS] #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      notifyClients();
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("[WS] #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    default: break;
  }
}

// ─────────────────────────────────────────────────────────────────
// Setup & Loop
// ─────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, password);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  Serial.print("[WiFi] Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(1000); Serial.print('.'); }
  Serial.printf("\n[WiFi] Connected — open http://%s/\n", WiFi.localIP().toString().c_str());

  ws.onEvent(onEvent);
  server.addHandler(&ws);

  server.on("/",           HTTP_GET, [](AsyncWebServerRequest *r){ r->send_P(200, "text/html",              index_html); });
  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *r){ r->send_P(200, "text/html",              index_html); });
  server.on("/style.css",  HTTP_GET, [](AsyncWebServerRequest *r){ r->send_P(200, "text/css",               style_css);  });
  server.on("/app.js",     HTTP_GET, [](AsyncWebServerRequest *r){ r->send_P(200, "application/javascript", app_js);     });

  server.begin();
  Serial.println("[HTTP] Server started");
}

void loop() {
  ws.cleanupClients();
  unsigned long t = millis();
  if (t - lastReadTime      >= READ_INTERVAL)      { lastReadTime      = t; readSensor();    }
  if (t - lastBroadcastTime >= BROADCAST_INTERVAL) { lastBroadcastTime = t; notifyClients(); }
}
