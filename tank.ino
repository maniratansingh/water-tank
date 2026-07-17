/*
 * ================================================================
 *  ESP8266 Water Tank Level Monitor
 *  Single-file Arduino sketch — just open and upload.
 * ================================================================
 *
 *  HARDWARE
 *  --------
 *  Board  : ESP8266 (NodeMCU 1.0 or Wemos D1 Mini)
 *  Sensor : JSN-SR04M waterproof ultrasonic distance sensor
 *  TRIG   : GPIO 12  (NodeMCU pin label D6)
 *  ECHO   : GPIO 13  (NodeMCU pin label D7)
 *
 *  REQUIRED LIBRARIES  (install via Arduino Library Manager)
 *  ---------------------------------------------------------
 *  1. ArduinoJson        by Benoit Blanchon   (>= 6.x)
 *  2. ESPAsyncTCP        by me-no-dev
 *  3. ESPAsyncWebServer  by me-no-dev
 *
 *  USAGE
 *  -----
 *  1. Set your Wi-Fi SSID and password in the "Wi-Fi Credentials" section below.
 *  2. In Arduino IDE: Tools → Board → NodeMCU 1.0 (ESP-12E Module)
 *  3. Click Upload.
 *  4. Open Serial Monitor at 115200 baud — note the IP address printed.
 *  5. Open http://<that-ip>/ in any browser on the same Wi-Fi network.
 *  6. Use the ⚙️ Settings button in the browser to calibrate the sensor
 *     without reflashing.
 * ================================================================
 */

// ================================================================
//  INCLUDES
// ================================================================
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <pgmspace.h>

// ================================================================
//  WI-FI CREDENTIALS  ← change these before uploading
// ================================================================
const char *ssid     = "YOUR_SSID";
const char *password = "YOUR_PASSWORD";

// ================================================================
//  SENSOR PINS  (JSN-SR04M)
// ================================================================
#define TRIG_PIN 12  // D6 on NodeMCU
#define ECHO_PIN 13  // D7 on NodeMCU

// ================================================================
//  CONFIGURATION  (also adjustable live from the web UI)
// ================================================================
float empty_distance_cm = 100.0; // Distance (cm) when tank is empty
float full_distance_cm  =  20.0; // Distance (cm) when tank is full
int   capacity_l        = 1000;  // Total tank capacity in litres

// ================================================================
//  RUNTIME STATE
// ================================================================
float current_distance = -1;
int   current_pct      = 0;
int   current_vol      = 0;
bool  sensor_error     = false;

// ================================================================
//  SERVERS
// ================================================================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ================================================================
//  TIMING
// ================================================================
unsigned long lastReadTime      = 0;
unsigned long lastBroadcastTime = 0;
const unsigned long READ_INTERVAL      = 2000; // Read sensor every 2 s
const unsigned long BROADCAST_INTERVAL = 5000; // Push to browser every 5 s

// ================================================================
//  SENSOR TUNING
// ================================================================
#define SENSOR_SAMPLES         5
#define SENSOR_SAMPLE_DELAY_MS 50

// ================================================================
//  WEB ASSETS  (HTML / CSS / JS stored in flash via PROGMEM)
// ================================================================

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Kids Water Monitor — ESP8266 Standalone</title>
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
      <p class="modal-sub">Configure the sensor calibration directly on the device.</p>

      <div class="form-group">
        <label for="set-capacity">Total Capacity (Liters)</label>
        <input id="set-capacity" type="number" placeholder="1000" />
      </div>

      <div class="form-group">
        <label for="set-empty">Empty Distance (cm)</label>
        <p class="input-hint">Distance from sensor to the bottom of the tank</p>
        <input id="set-empty" type="number" placeholder="100" />
      </div>

      <div class="form-group">
        <label for="set-full">Full Distance (cm)</label>
        <p class="input-hint">Distance from sensor to the maximum water level</p>
        <input id="set-full" type="number" placeholder="20" />
      </div>

      <button class="btn-primary" onclick="saveSettings()">
        <span class="btn-icon">💾</span>
        Save to ESP8266
      </button>
    </div>
  </div>

  <!-- Main app -->
  <div id="app" class="app">
    <!-- Header -->
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

    <!-- Single Tank container -->
    <main class="single-tank-layout" id="tank-card-container">
      <!-- dynamically populated -->
    </main>

    <!-- Log panel -->
    <section class="log-section">
      <div class="log-header">
        <span class="log-title">📡 Event Log</span>
        <button class="btn-ghost-sm" onclick="clearLog()">Clear</button>
      </div>
      <div id="event-log" class="log-body"></div>
    </section>
  </div>

  <!-- Toast container -->
  <div id="toast-container"></div>

  <script src="app.js"></script>
</body>
</html>
)rawliteral";

// ----------------------------------------------------------------

const char style_css[] PROGMEM = R"rawliteral(
/* ============================================================
   SmartGhar TankSync Dashboard — Simplified CSS Design System
   ============================================================ */

/* ── Tokens ──────────────────────────────────────────────── */
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
  --green-dim:     #16a34a;
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
  --shadow-glow-blue: 0 0 24px rgba(0,112,243,0.35);

  --font-body:     'Inter', system-ui, sans-serif;
  --font-mono:     'JetBrains Mono', 'Fira Code', monospace;

  --transition:    all 0.2s cubic-bezier(0.4,0,0.2,1);
}

/* ── Reset & Base ─────────────────────────────────────────── */
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

body {
  font-family: var(--font-body);
  background-color: var(--bg-base);
  color: var(--text-1);
  min-height: 100vh;
  overflow-x: hidden;
  line-height: 1.6;
}

/* Animated gradient background */
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

/* ── Utilities ───────────────────────────────────────────── */
.hidden { display: none !important; }

/* ── Modal overlay ───────────────────────────────────────── */
.modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(8,12,20,0.92);
  backdrop-filter: blur(12px);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
  animation: fadeIn 0.3s ease;
}

/* ── App header ──────────────────────────────────────────── */
.app {
  position: relative;
  z-index: 1;
  min-height: 100vh;
  display: flex;
  flex-direction: column;
}

.app-header {
  position: sticky;
  top: 0;
  z-index: 100;
  background: var(--bg-glass);
  backdrop-filter: blur(20px);
  border-bottom: 1px solid var(--border);
  padding: 0.875rem 1.5rem;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 1rem;
}

.header-left { display: flex; align-items: center; gap: 0.875rem; }

.header-logo {
  width: 36px; height: 36px;
  background: radial-gradient(circle, rgba(0,112,243,0.2), transparent);
  border-radius: 50%;
  border: 1px solid rgba(0,112,243,0.3);
  display: flex; align-items: center; justify-content: center;
  padding: 6px;
  flex-shrink: 0;
}
.header-logo svg { width: 100%; height: 100%; }

.header-title {
  font-size: 1rem;
  font-weight: 800;
  letter-spacing: -0.02em;
  background: linear-gradient(135deg, #f0f4ff, var(--blue-light));
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}

.header-sub {
  font-size: 0.7rem;
  color: var(--text-3);
  font-weight: 400;
  margin-top: -2px;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  flex-wrap: wrap;
  justify-content: flex-end;
}

/* WS status badge */
.ws-badge {
  display: flex;
  align-items: center;
  gap: 0.45rem;
  padding: 0.3rem 0.75rem;
  border-radius: 999px;
  font-size: 0.75rem;
  font-weight: 600;
  border: 1px solid transparent;
  transition: var(--transition);
}

.ws-dot {
  width: 7px; height: 7px;
  border-radius: 50%;
  display: inline-block;
  flex-shrink: 0;
}

.ws-connecting {
  background: rgba(245,158,11,0.12);
  border-color: rgba(245,158,11,0.3);
  color: var(--yellow);
}
.ws-connecting .ws-dot {
  background: var(--yellow);
  animation: pulse 1.2s ease-in-out infinite;
}

.ws-connected {
  background: rgba(34,197,94,0.12);
  border-color: rgba(34,197,94,0.3);
  color: var(--green);
}
.ws-connected .ws-dot {
  background: var(--green);
  animation: pulse 2s ease-in-out infinite;
}

.ws-error, .ws-disconnected {
  background: rgba(239,68,68,0.12);
  border-color: rgba(239,68,68,0.3);
  color: var(--red);
}
.ws-error .ws-dot, .ws-disconnected .ws-dot { background: var(--red); }

/* ── Single Tank Container Layout ────────────────────────── */
.single-tank-layout {
  display: flex;
  align-items: center;
  justify-content: center;
  flex: 1;
  padding: 2rem 1.5rem;
  max-width: 520px;
  width: 100%;
  margin: 0 auto;
}

/* ── Tank card ───────────────────────────────────────────── */
.tank-card {
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  padding: 1.75rem;
  transition: var(--transition);
  position: relative;
  overflow: hidden;
  width: 100%;
  box-shadow: var(--shadow-card);
  animation: cardIn 0.4s cubic-bezier(0.34,1.56,0.64,1) both;
}

.tank-card::before {
  content: '';
  position: absolute;
  inset: 0;
  border-radius: var(--radius-lg);
  background: linear-gradient(135deg, rgba(255,255,255,0.04) 0%, transparent 60%);
  pointer-events: none;
}

.tank-card:hover {
  background: var(--bg-card-hover);
  border-color: var(--border-strong);
}

/* Status glow borders */
.tank-card.status-online  { --card-glow: rgba(34,197,94,0.1); border-color: rgba(34,197,94,0.25); }
.tank-card.status-stale   { --card-glow: rgba(245,158,11,0.1); border-color: rgba(245,158,11,0.25); }
.tank-card.status-lost    { --card-glow: rgba(239,68,68,0.1); border-color: rgba(239,68,68,0.25); }
.tank-card.status-online::after,
.tank-card.status-stale::after,
.tank-card.status-lost::after {
  content: '';
  position: absolute;
  inset: -1px;
  border-radius: var(--radius-lg);
  background: var(--card-glow);
  z-index: -1;
}

/* Card header */
.tank-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  margin-bottom: 1.5rem;
}

.tank-name-row {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}

.tank-name {
  font-size: 1.25rem;
  font-weight: 800;
  color: var(--text-1);
  letter-spacing: -0.01em;
}

.tank-id {
  font-size: 0.75rem;
  font-family: var(--font-mono);
  color: var(--text-2);
}

.tank-status-badge {
  padding: 0.3rem 0.75rem;
  border-radius: 999px;
  font-size: 0.7rem;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  flex-shrink: 0;
}

.badge-online  { background: rgba(34,197,94,0.15);  color: var(--green);  border: 1px solid rgba(34,197,94,0.3); }
.badge-stale   { background: rgba(245,158,11,0.15); color: var(--yellow); border: 1px solid rgba(245,158,11,0.3); }
.badge-lost    { background: rgba(239,68,68,0.15);  color: var(--red);    border: 1px solid rgba(239,68,68,0.3); }
.badge-waiting { background: rgba(138,100,255,0.15);color: #a78bfa;       border: 1px solid rgba(138,100,255,0.3); }

/* ── Water tank visual ───────────────────────────────────── */
.tank-visual {
  display: flex;
  align-items: center;
  gap: 1.75rem;
  margin-bottom: 1.5rem;
}

.tank-svg-wrap {
  position: relative;
  width: 100px;
  flex-shrink: 0;
}

.tank-svg-wrap svg {
  width: 100px;
  height: 140px;
  display: block;
}

/* Water fill animation */
.tank-water {
  transition: height 1s cubic-bezier(0.4, 0, 0.2, 1),
              y 1s cubic-bezier(0.4, 0, 0.2, 1),
              fill 0.5s;
}

.tank-wave {
  animation: waveScroll 3s linear infinite;
  transition: transform 1s cubic-bezier(0.4, 0, 0.2, 1), fill 0.5s;
}

@keyframes waveScroll {
  from { transform: translateX(0); }
  to { transform: translateX(-60px); }
}

.tank-pct-overlay {
  position: absolute;
  bottom: 16px;
  left: 0; right: 0;
  text-align: center;
  font-size: 0.875rem;
  font-weight: 800;
  font-family: var(--font-mono);
  color: #fff;
  text-shadow: 0 1px 4px rgba(0,0,0,0.6);
  pointer-events: none;
}

/* Big level display */
.tank-level-info {
  flex: 1;
}

.level-pct {
  font-size: 3.5rem;
  font-weight: 800;
  font-family: var(--font-mono);
  line-height: 1;
  letter-spacing: -0.03em;
}

.level-unit {
  font-size: 1.25rem;
  font-weight: 600;
  color: var(--text-2);
  margin-left: 2px;
}

.level-volume {
  font-size: 0.95rem;
  color: var(--text-2);
  margin-top: 6px;
}

.level-volume strong {
  color: var(--text-1);
  font-weight: 700;
}

/* Progress bar */
.tank-progress {
  margin-bottom: 1.5rem;
}

.progress-track {
  height: 8px;
  background: rgba(255,255,255,0.07);
  border-radius: 999px;
  overflow: hidden;
  position: relative;
}

.progress-fill {
  height: 100%;
  border-radius: 999px;
  transition: width 1s cubic-bezier(0.4,0,0.2,1);
  position: relative;
}

.progress-fill::after {
  content: '';
  position: absolute;
  right: 0; top: 0; bottom: 0;
  width: 20px;
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

/* Water fill color stops */
.level-critical .tank-water, .level-critical .tank-wave { fill: url(#waterCritical); }
.level-low      .tank-water, .level-low      .tank-wave { fill: url(#waterLow); }
.level-mid      .tank-water, .level-mid      .tank-wave { fill: url(#waterMid); }
.level-good     .tank-water, .level-good     .tank-wave { fill: url(#waterGood); }
.level-full     .tank-water, .level-full     .tank-wave { fill: url(#waterFull); }

/* ── Stats grid (2 columns) ──────────────────────────────── */
.tank-stats {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 0.75rem;
}

.stat-item {
  background: rgba(255,255,255,0.03);
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  padding: 0.625rem;
  text-align: center;
  transition: var(--transition);
}

.stat-item:hover {
  background: rgba(255,255,255,0.06);
  border-color: var(--border-strong);
}

.stat-label {
  font-size: 0.65rem;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  color: var(--text-3);
  font-weight: 600;
  margin-bottom: 4px;
}

.stat-val {
  font-size: 0.9rem;
  font-weight: 700;
  font-family: var(--font-mono);
  color: var(--text-1);
}

/* Sensor error badge */
.sensor-alert {
  margin-top: 1rem;
  padding: 0.625rem 0.875rem;
  background: rgba(239,68,68,0.1);
  border: 1px solid rgba(239,68,68,0.25);
  border-radius: var(--radius-sm);
  font-size: 0.75rem;
  color: #fca5a5;
  display: flex;
  align-items: center;
  gap: 0.4rem;
}

/* ── Toast notifications ──────────────────────────────────── */
#toast-container {
  position: fixed;
  bottom: 1.5rem;
  right: 1.5rem;
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  z-index: 9999;
}

.toast {
  padding: 0.75rem 1.25rem;
  border-radius: var(--radius-md);
  font-size: 0.8125rem;
  font-weight: 500;
  backdrop-filter: blur(12px);
  animation: toastIn 0.3s cubic-bezier(0.34,1.56,0.64,1);
  box-shadow: var(--shadow-card);
  max-width: 320px;
}

.toast-info    { background: rgba(0,112,243,0.85);  border: 1px solid rgba(96,200,255,0.3); color: #fff; }
.toast-success { background: rgba(22,163,74,0.85);  border: 1px solid rgba(34,197,94,0.3);  color: #fff; }
.toast-warn    { background: rgba(180,83,9,0.85);   border: 1px solid rgba(245,158,11,0.3); color: #fff; }
.toast-error   { background: rgba(185,28,28,0.85);  border: 1px solid rgba(239,68,68,0.3);  color: #fff; }

#svg-defs { position: absolute; width: 0; height: 0; overflow: hidden; }

/* ── Animations ──────────────────────────────────────────── */
@keyframes fadeIn { from { opacity: 0; } to { opacity: 1; } }
@keyframes slideUp {
  from { opacity: 0; transform: translateY(24px) scale(0.96); }
  to   { opacity: 1; transform: translateY(0)    scale(1); }
}
@keyframes cardIn {
  from { opacity: 0; transform: translateY(16px); }
  to   { opacity: 1; transform: translateY(0); }
}
@keyframes logIn { from { opacity: 0; transform: translateX(-8px); } to { opacity: 1; transform: translateX(0); } }
@keyframes toastIn {
  from { opacity: 0; transform: translateX(16px) scale(0.95); }
  to   { opacity: 1; transform: translateX(0)    scale(1); }
}
@keyframes toastOut { to { opacity: 0; transform: translateX(16px) scale(0.95); } }
@keyframes pulse {
  0%, 100% { opacity: 1;   transform: scale(1); }
  50%       { opacity: 0.5; transform: scale(0.85); }
}

/* ── Responsive ──────────────────────────────────────────── */
@media (max-width: 640px) {
  .app-header { padding: 0.75rem 1rem; }
  .single-tank-layout  { padding: 1.5rem 1rem; }
  .log-section { margin: 0 1rem 1rem; }
  .modal-box   { padding: 2rem 1.5rem 1.5rem; }
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

.modal-logo {
  display: flex;
  justify-content: center;
  margin-bottom: 1.25rem;
}

.logo-ring {
  width: 72px; height: 72px;
  background: radial-gradient(circle, rgba(0,112,243,0.2), transparent 70%);
  border-radius: 50%;
  border: 1px solid rgba(0,112,243,0.3);
  display: flex; align-items: center; justify-content: center;
  padding: 14px;
  box-shadow: 0 0 30px rgba(0,112,243,0.25);
}

.logo-ring svg { width: 100%; height: 100%; }

.modal-title {
  font-size: 1.75rem;
  font-weight: 800;
  text-align: center;
  background: linear-gradient(135deg, #f0f4ff 0%, var(--blue-light) 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
  letter-spacing: -0.02em;
}

.modal-sub {
  text-align: center;
  color: var(--text-2);
  font-size: 0.875rem;
  margin-top: 0.4rem;
  margin-bottom: 2rem;
}

/* Form elements */
.form-group {
  margin-bottom: 1.25rem;
}

.form-group label {
  display: block;
  font-size: 0.8125rem;
  font-weight: 600;
  color: var(--text-2);
  text-transform: uppercase;
  letter-spacing: 0.06em;
  margin-bottom: 0.5rem;
}

.optional {
  font-weight: 400;
  text-transform: none;
  letter-spacing: 0;
  font-size: 0.75rem;
  color: var(--text-3);
}

.input-row {
  display: flex;
  align-items: center;
  background: rgba(255,255,255,0.04);
  border: 1px solid var(--border-strong);
  border-radius: var(--radius-sm);
  overflow: hidden;
  transition: var(--transition);
}

.input-row:focus-within {
  border-color: var(--blue);
  box-shadow: 0 0 0 3px rgba(0,112,243,0.2);
}

.input-prefix, .input-suffix {
  padding: 0.625rem 0.75rem;
  font-family: var(--font-mono);
  font-size: 0.75rem;
  color: var(--text-3);
  background: rgba(255,255,255,0.03);
  white-space: nowrap;
  user-select: none;
}

.input-prefix { border-right: 1px solid var(--border); }
.input-suffix { border-left: 1px solid var(--border); }

input[type="text"],
input[type="password"],
input[type="number"] {
  width: 100%;
  background: rgba(255,255,255,0.04);
  border: 1px solid var(--border-strong);
  border-radius: var(--radius-sm);
  padding: 0.625rem 0.875rem;
  color: var(--text-1);
  font-family: var(--font-mono);
  font-size: 0.875rem;
  outline: none;
  transition: var(--transition);
}

.input-row input[type="text"] {
  border: none;
  border-radius: 0;
  background: transparent;
  flex: 1;
  min-width: 0;
}

input[type="text"]:focus,
input[type="password"]:focus,
input[type="number"]:focus {
  border-color: var(--blue);
  box-shadow: 0 0 0 3px rgba(0,112,243,0.2);
}

input::placeholder { color: var(--text-3); }

/* Buttons */
.btn-primary {
  width: 100%;
  padding: 0.875rem 1.5rem;
  background: linear-gradient(135deg, var(--blue) 0%, #0052cc 100%);
  color: #fff;
  border: none;
  border-radius: var(--radius-sm);
  font-family: var(--font-body);
  font-size: 0.9375rem;
  font-weight: 700;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 0.5rem;
  transition: var(--transition);
  box-shadow: 0 4px 16px rgba(0,112,243,0.4);
  margin-top: 0.5rem;
}

.btn-primary:hover {
  transform: translateY(-1px);
  box-shadow: 0 6px 24px rgba(0,112,243,0.5);
}

.btn-ghost {
  background: transparent;
  border: none;
  color: var(--blue-light);
  font-family: var(--font-body);
  font-size: 0.875rem;
  cursor: pointer;
  padding: 0.25rem 0.5rem;
  border-radius: 4px;
  transition: var(--transition);
}
.btn-ghost:hover { background: rgba(96,200,255,0.1); }

.btn-ghost-sm {
  background: transparent;
  border: 1px solid var(--border);
  color: var(--text-2);
  font-family: var(--font-body);
  font-size: 0.75rem;
  cursor: pointer;
  padding: 0.25rem 0.625rem;
  border-radius: 4px;
  transition: var(--transition);
}
.btn-ghost-sm:hover { border-color: var(--border-strong); color: var(--text-1); }

.demo-link {
  text-align: center;
  margin-top: 1rem;
}

.btn-icon-only {
  background: rgba(255,255,255,0.05);
  border: 1px solid var(--border);
  color: var(--text-2);
  width: 32px; height: 32px;
  border-radius: var(--radius-sm);
  cursor: pointer;
  font-size: 0.75rem;
  transition: var(--transition);
  display: flex; align-items: center; justify-content: center;
}
.btn-icon-only:hover {
  background: rgba(239,68,68,0.15);
  border-color: rgba(239,68,68,0.4);
  color: var(--red);
}

/* ── Log panel ───────────────────────────────────────────── */
.log-section {
  margin: 0 1.5rem 1.5rem;
  background: var(--bg-card);
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  overflow: hidden;
}

.log-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0.625rem 1rem;
  background: rgba(255,255,255,0.02);
  border-bottom: 1px solid var(--border);
}

.log-title {
  font-size: 0.8125rem;
  font-weight: 600;
  color: var(--text-2);
}

.log-body {
  height: 120px;
  overflow-y: auto;
  padding: 0.5rem;
  display: flex;
  flex-direction: column-reverse;
  gap: 2px;
}

.log-body::-webkit-scrollbar { width: 4px; }
.log-body::-webkit-scrollbar-track { background: transparent; }
.log-body::-webkit-scrollbar-thumb { background: var(--border-strong); border-radius: 999px; }

.log-entry {
  padding: 0.3rem 0.625rem;
  border-radius: 4px;
  font-size: 0.72rem;
  font-family: var(--font-mono);
  display: flex;
  gap: 0.75rem;
  align-items: baseline;
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

/* ── Settings Modal Custom Additions ─────────────────────── */
.modal-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 0.5rem;
}
.modal-header .modal-title {
  margin-bottom: 0;
}
.input-hint {
  font-size: 0.8rem;
  color: #a0aec0;
  margin-top: -0.25rem;
  margin-bottom: 0.5rem;
}
)rawliteral";

// ----------------------------------------------------------------

const char app_js[] PROGMEM = R"rawliteral(
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
    // Sync settings fields only when the modal is closed (don't overwrite mid-edit)
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

    // Trigger load animation
    setTimeout(() => {
      const rect = document.getElementById('water-rect');
      const wave = document.getElementById('water-wave');
      if (rect) {
        rect.style.transition = 'none'; wave.style.transition = 'none';
        rect.setAttribute('height', '0'); rect.setAttribute('y', '95');
        wave.setAttribute('d', `M 0 95 Q 15 92 30 95 T 60 95 T 90 95 T 120 95 L 120 100 L 0 100 Z`);
        requestAnimationFrame(() => requestAnimationFrame(() => {
          rect.style.transition = ''; wave.style.transition = '';
          rect.setAttribute('height', FILL_H); rect.setAttribute('y', FILL_Y);
          wave.setAttribute('d', `M 0 ${FILL_Y} Q 15 ${FILL_Y-3} 30 ${FILL_Y} T 60 ${FILL_Y} T 90 ${FILL_Y} T 120 ${FILL_Y} L 120 100 L 0 100 Z`);
        }));
      }
    }, 50);

  } else {
    // Soft update — only touch changed DOM nodes so CSS transitions play
    const visual = document.getElementById('tank-visual-container');
    const levelInfo = document.getElementById('level-info-container');
    const progCont = document.getElementById('progress-container');

    if (visual) visual.className = `tank-visual ${lvlClass}`;
    if (levelInfo) levelInfo.className = `tank-level-info ${lvlClass}`;
    if (progCont) progCont.className = `tank-progress ${lvlClass}`;

    document.getElementById('status-badge-container').innerHTML = statusBadge;
    document.getElementById('overlay-pct').innerText = tank.level_pct !== null ? pct + '%' : '—';
    document.getElementById('info-pct').innerText = fmtPct(tank.level_pct);
    document.getElementById('info-vol').innerText = `${fmt(tank.volume_l)} L`;
    document.getElementById('progress-fill').style.width = `${tank.level_pct ?? 0}%`;
    document.getElementById('stat-rssi').innerText = fmtRssi(tank.rssi);
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

function animateTankWater() {
  const rect = document.getElementById('water-rect');
  if (rect) {
    const h = rect.getAttribute('height');
    rect.setAttribute('height', '0');
    requestAnimationFrame(() => requestAnimationFrame(() => {
      rect.setAttribute('height', h);
    }));
  }
}

// ── Init ───────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {
  document.body.insertAdjacentHTML('afterbegin', SVG_DEFS);
  log('info', 'Standalone ESP8266 App ready');
  autoConnect();
});
)rawliteral";

// ================================================================
//  SENSOR FUNCTIONS
// ================================================================

void sort_floats(float *arr, int n) {
  for (int i = 1; i < n; i++) {
    float key = arr[i];
    int j = i - 1;
    while (j >= 0 && arr[j] > key) {
      arr[j + 1] = arr[j];
      j--;
    }
    arr[j + 1] = key;
  }
}

float readSingleSample() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // 60 ms timeout — covers 15 ms JSN settling time + 400 cm echo path
  long duration = pulseIn(ECHO_PIN, HIGH, 60000);
  if (duration == 0) return -1.0;
  return (duration * 0.0343) / 2.0;
}

void readSensor() {
  float samples[SENSOR_SAMPLES];
  int valid_count = 0;

  for (int i = 0; i < SENSOR_SAMPLES; i++) {
    float d = readSingleSample();
    if (d >= 5.0 && d <= 400.0) {   // Valid range for JSN-SR04M
      samples[valid_count++] = d;
    }
    delay(SENSOR_SAMPLE_DELAY_MS);
  }

  if (valid_count < 2) {
    sensor_error = true;
    current_distance = -1;
    return;
  }

  sort_floats(samples, valid_count);

  // IQR filter (Interquartile Range)
  int q1_idx = valid_count / 4;
  int q3_idx = (valid_count * 3) / 4;
  float q1  = samples[q1_idx];
  float q3  = samples[q3_idx];
  float iqr = q3 - q1;

  float lower = q1 - (iqr * 1.5);
  float upper = q3 + (iqr * 1.5);

  // Widen bounds when variance is extremely small to avoid over-rejection
  if (iqr < 2.0) {
    lower = q1 - 5.0;
    upper = q3 + 5.0;
  }

  float filtered[SENSOR_SAMPLES];
  int fcount = 0;
  for (int i = 0; i < valid_count; i++) {
    if (samples[i] >= lower && samples[i] <= upper) {
      filtered[fcount++] = samples[i];
    }
  }

  // Prefer filtered median; fall back to raw median if all were rejected
  current_distance = (fcount == 0) ? samples[valid_count / 2] : filtered[fcount / 2];
  sensor_error = false;

  float range = empty_distance_cm - full_distance_cm;
  if (range <= 0) {
    current_pct = 0;
    current_vol = 0;
  } else {
    float level = empty_distance_cm - current_distance;
    if (level < 0)     level = 0;
    if (level > range) level = range;

    // round() avoids integer-truncation bias
    current_pct = round((level * 100.0) / range);
    current_vol = round((capacity_l * level) / range);
  }
}

// ================================================================
//  WEBSOCKET FUNCTIONS
// ================================================================

void notifyClients() {
  StaticJsonDocument<512> doc;
  doc["type"] = "state";

  JsonArray  devices = doc.createNestedArray("devices");
  JsonObject tank    = devices.createNestedObject();
  tank["tank"]       = 1;
  tank["name"]       = "My Water Tank";
  tank["capacity_l"] = capacity_l;

  JsonObject state = tank.createNestedObject("state");
  // BUG FIX: keep current_pct on sensor error so the UI freezes at the
  // last known level instead of snapping to 0 %.
  state["level_pct"]    = current_pct;
  state["sensor_error"] = sensor_error;
  state["rssi"]         = WiFi.RSSI();
  state["conn_state"]   = "online";
  state["consumed_l"]   = 0;

  JsonObject settings = doc.createNestedObject("settings");
  settings["empty_distance_cm"] = empty_distance_cm;
  settings["full_distance_cm"]  = full_distance_cm;

  String output;
  serializeJson(doc, output);
  ws.textAll(output);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len &&
      info->opcode == WS_TEXT) {
    data[len] = 0;

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, (char *)data);
    if (!err) {
      if (doc["type"] == "update_settings") {
        if (doc.containsKey("empty_distance_cm"))
          empty_distance_cm = doc["empty_distance_cm"];
        if (doc.containsKey("full_distance_cm"))
          full_distance_cm = doc["full_distance_cm"];
        if (doc.containsKey("capacity_l"))
          capacity_l = doc["capacity_l"];

        // Re-read with new calibration and push result immediately
        readSensor();
        notifyClients();
      }
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n",
                    client->id(), client->remoteIP().toString().c_str());
      notifyClients(); // Push current state to the new client immediately
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

// ================================================================
//  SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); // Clear any stale saved credentials
  delay(100);
  WiFi.begin(ssid, password);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Open: http://");
  Serial.println(WiFi.localIP());

  // WebSocket
  ws.onEvent(onEvent);
  server.addHandler(&ws);

  // Serve web assets from PROGMEM
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });
  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/css", style_css);
  });
  server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "application/javascript", app_js);
  });

  server.begin();
  Serial.println("HTTP server started");
}

// ================================================================
//  LOOP
// ================================================================
void loop() {
  ws.cleanupClients();

  unsigned long currentMillis = millis();

  if (currentMillis - lastReadTime >= READ_INTERVAL) {
    lastReadTime = currentMillis;
    readSensor();
  }

  if (currentMillis - lastBroadcastTime >= BROADCAST_INTERVAL) {
    lastBroadcastTime = currentMillis;
    notifyClients();
  }
}
