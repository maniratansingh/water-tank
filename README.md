# 💧 Water Tank Level Monitor — ESP8266

> **One file. Flash it. Open browser. Done.**  
> A real-time water tank level monitor that runs entirely on an ESP8266, served from flash memory. No cloud. No app. No internet needed.

---

## 💡 The Philosophy — A Poor Man's Water Monitor

I came across **[TankSync by @Techposts](https://github.com/Techposts/TankSync)** and loved the idea.  
His build is professional, reliable, runs all day without issues, and is genuinely well-engineered.

**But I had one problem — I didn't want to spend that much.**

So I asked myself: *what is the absolute minimum I need to know if my tank is full or empty?*  
The answer was simple — just a percentage on my phone. Nothing more.

That's what this is. No fancy features. No multi-tank support. No OTA updates.  
Just a ₹486 sensor stuck to a tank lid, telling me how much water I have left.

This is the **DIY, jugaad, poor man's version** — built with whatever was cheapest at the local electronics shop.

---

## 🧾 Bill of Materials — Total Cost ₹ ~490

| Component | What it does | Cost |
|---|---|---|
| **ESP8266 NodeMCU** | The brain — runs Wi-Fi + web server | ₹ 175 |
| **JSN-SR04M** | Waterproof ultrasonic sensor — measures water level | ₹ 259 |
| **CCTV junction box** | Weatherproof enclosure to house the electronics | ₹ 50 |
| **Resistors + wire + solder** | Wiring it all up | ₹ 2–5 |
| **Total** | | **≈ ₹ 490** |

> That's less than ₹500 for a live water level monitor on your phone.  
> No subscription. No hub. No internet required. Just your home Wi-Fi.

---

## 🙏 Credit & Comparison

| | This project | [TankSync](https://github.com/Techposts/TankSync) (original) |
|---|---|---|
| **Philosophy** | Cheap DIY / jugaad | Professional & reliable |
| **Complexity** | Barebones — 1 file | Multi-file, structured |
| **Features** | Level % + Wi-Fi signal + calibration | Multi-tank, history, alerts, OTA, and more |
| **Reliability** | Good enough for home use | Rock solid, runs 24/7 |
| **Best for** | ₹500 budget builds, learning, hacking | Anyone who wants it to just work |

> **If you want something that works reliably, professionally, and has all the features — use [TankSync](https://github.com/Techposts/TankSync). The original author did an excellent job.**  
>  
> **If you want the cheapest possible thing that gets the job done — you're in the right place.**

---

## 📸 Dashboard Preview

The live web dashboard shows water level, Wi-Fi signal, animated tank fill, and lets you calibrate the sensor without reflashing — all over WebSocket.

---

## 🔩 Hardware Required

| Component | Details |
|---|---|
| **Microcontroller** | ESP8266 — NodeMCU 1.0 (ESP-12E) or Wemos D1 Mini |
| **Sensor** | JSN-SR04M waterproof ultrasonic distance sensor |
| **Power** | USB (from NodeMCU's onboard regulator), or external 5V |
| **Wires** | 4 × jumper wires (ideally colour-coded) |

---

## 🔌 Wiring / Circuit Diagram

```
ESP8266 NodeMCU          JSN-SR04M Sensor
┌─────────────┐          ┌──────────────┐
│             │          │              │
│        3.3V ●━━━━━━━━━━● VCC          │
│         GND ●━━━━━━━━━━● GND          │
│  D6 / GPIO12 ●━━━━━━━━━● TRIG         │
│  D7 / GPIO13 ●━━━━━━━━━● ECHO         │
│             │          │              │
└─────────────┘          └──────────────┘
```

### Wire Colour Convention

| Wire | NodeMCU Pin | Sensor Pin | Colour |
|------|-------------|------------|--------|
| Power | 3.3V | VCC | 🔴 Red |
| Ground | GND | GND | ⚫ Black |
| Trigger | D6 / GPIO 12 | TRIG | 🟡 Yellow |
| Echo | D7 / GPIO 13 | ECHO | 🟢 Green |

> **⚠️ Important — Use 3.3 V, NOT 5 V**  
> The JSN-SR04M module will accept 5 V on its VCC and the sensor itself runs fine, but the ESP8266's GPIO pins are **NOT** 5 V tolerant. If you power the sensor from 5 V, the ECHO pin will output 5 V back into GPIO 13 and permanently damage the chip. Power from **3.3 V only**, or use a voltage divider on the ECHO line if you must use 5 V.

---

## 📐 How the JSN-SR04M Works

The JSN-SR04M is a **waterproof** variant of the popular HC-SR04 ultrasonic sensor. It uses two piezoelectric transducers:

```
     [TRIG pulse]
          │
          ▼
  ┌───────────────┐        ┌──────────────┐
  │  TRANSMITTER  │──────▶ │     WATER    │ ──▶ echo reflected
  │  (40 kHz burst)│       │    SURFACE   │
  └───────────────┘        └──────────────┘
          │
     [ECHO pin goes HIGH]
     [stays HIGH until echo received]
          │
          ▼
     pulseIn() measures duration → distance
```

**Distance formula:**

```
distance (cm) = (echo_duration_μs × 0.0343) / 2
```

- `0.0343` cm/μs = speed of sound at ~20°C
- Divided by 2 because the sound travels **to** the water and **back**

**Measurement range:** 20 cm – 400 cm (the JSN-SR04M has a 20 cm blind zone near the sensor face)

---

## 📏 Sensor Calibration Explained

The sensor measures **distance from the sensor to the water surface**. The firmware converts this into a **percentage** and **litres** using two calibration points you set:

```
        [SENSOR]
           │
           │ ← full_distance_cm (e.g. 20 cm) — water touches here when FULL
           │
          ~~~  ← water surface (varies)
           │
           │
           │ ← empty_distance_cm (e.g. 100 cm) — sensor-to-bottom when EMPTY
           │
        [BOTTOM OF TANK]
```

**Level calculation:**

```
range = empty_distance_cm - full_distance_cm      (e.g. 100 - 20 = 80 cm)
level = empty_distance_cm - current_distance      (how much water is present, in cm)
level = constrain(level, 0, range)                (clamp to valid range)

percentage = round((level × 100) / range)
volume_L   = round((capacity_L × level) / range)
```

> **Key insight:** `empty_distance_cm` is always **larger** than `full_distance_cm` because when the tank is empty the sensor looks further down to reach the bottom, and when the tank is full the water surface is closer to the sensor.

### Setting Calibration Without Reflashing

Open the **⚙️ Settings** button in the browser dashboard and enter:

| Field | What to measure |
|---|---|
| **Empty Distance** | Mount the sensor above the tank. With the tank **completely empty**, measure the distance from the sensor to the tank floor (e.g. 100 cm) |
| **Full Distance** | With the tank **completely full**, measure the distance from the sensor to the water surface (e.g. 20 cm) |
| **Capacity** | Total volume of the tank in litres (e.g. 1000) |

Click **Save to ESP8266** — the device applies changes immediately over WebSocket, no reflash needed.

---

## 🧠 Firmware Architecture

```
tank.ino  (single file — top to bottom)
│
├── Includes             ArduinoJson, ESP8266WiFi, ESPAsyncTCP, ESPAsyncWebServer
├── Wi-Fi Credentials    ssid / password — only thing you must change
├── Pin Definitions      TRIG_PIN = 12 (D6), ECHO_PIN = 13 (D7)
├── Config Variables     empty_distance_cm, full_distance_cm, capacity_l
├── Runtime State        current_distance, current_pct, sensor_error ...
├── Server Objects       AsyncWebServer server(80), AsyncWebSocket ws("/ws")
├── Timing Constants     READ_INTERVAL = 2 s, BROADCAST_INTERVAL = 5 s
│
├── ── WEB ASSETS (PROGMEM) ──────────────────────────────────────
│   ├── index_html[]     Full HTML dashboard page
│   ├── style_css[]      CSS design system (dark theme, animations)
│   └── app_js[]         WebSocket client + animated SVG tank renderer
│
├── ── SENSOR FUNCTIONS ──────────────────────────────────────────
│   ├── sort_floats()    Insertion sort for the sample array
│   ├── readSingleSample()   Fire one pulse → return distance in cm
│   └── readSensor()    Collect 5 samples → IQR filter → median → compute %
│
├── ── WEBSOCKET FUNCTIONS ───────────────────────────────────────
│   ├── notifyClients()          Serialise state → push JSON to all browsers
│   ├── handleWebSocketMessage() Parse settings-update from browser
│   └── onEvent()               Route WS_EVT_CONNECT / DATA / DISCONNECT
│
├── setup()    Wi-Fi connect → register WS handler → serve PROGMEM routes → start server
└── loop()     cleanupClients() → readSensor every 2 s → notifyClients every 5 s
```

---

## 🔬 Noise Filtering — IQR Median Algorithm

Raw ultrasonic readings are noisy (reflections, temperature, vibration). The firmware takes **5 samples** and filters them:

```
Step 1 — Collect 5 samples, reject obviously invalid ones (< 5 cm or > 400 cm)

Step 2 — Sort the valid samples:
         [22.1,  22.3,  22.4,  22.6,  28.9]   ← 28.9 is an outlier

Step 3 — Compute Interquartile Range (IQR):
         Q1  = samples[n/4]      = 22.2
         Q3  = samples[3n/4]     = 22.5
         IQR = Q3 - Q1           = 0.3

Step 4 — Set fence bounds:
         lower = Q1 - 1.5×IQR  = 22.2 - 0.45 = 21.75
         upper = Q3 + 1.5×IQR  = 22.5 + 0.45 = 22.95
         (if IQR < 2.0 → use ±5 cm to avoid over-rejection)

Step 5 — Keep only in-fence values:
         [22.1,  22.3,  22.4,  22.6]   ← 28.9 removed ✓

Step 6 — Take the median of filtered values → 22.35 cm  ✓
```

This eliminates single-bounce reflections and electrical spikes while remaining responsive to genuine water level changes.

---

## 📡 WebSocket Protocol

The firmware and browser communicate over a persistent WebSocket at `ws://<device-ip>/ws`.

### Server → Browser  (`type: "state"`)

Sent immediately on connection, then every 5 seconds:

```json
{
  "type": "state",
  "devices": [
    {
      "tank": 1,
      "name": "My Water Tank",
      "capacity_l": 1000,
      "state": {
        "level_pct": 72,
        "sensor_error": false,
        "rssi": -58,
        "conn_state": "online",
        "consumed_l": 0
      }
    }
  ],
  "settings": {
    "empty_distance_cm": 100,
    "full_distance_cm": 20
  }
}
```

### Browser → Server  (`type: "update_settings"`)

Sent when the user clicks **Save** in the settings modal:

```json
{
  "type": "update_settings",
  "empty_distance_cm": 100,
  "full_distance_cm": 20,
  "capacity_l": 1000
}
```

The device applies the new calibration, re-reads the sensor, and immediately pushes a fresh `state` message back.

---

## 🌐 Browser Reconnection Logic

The JavaScript client uses **exponential back-off** reconnection so if Wi-Fi drops briefly the browser automatically recovers without a page refresh:

```
Attempt 1 → wait  2 s
Attempt 2 → wait  4 s
Attempt 3 → wait  8 s
Attempt 4 → wait 16 s
...
Attempt 8 → wait 30 s (capped)
→ "Max retries reached. Please refresh."
```

---

## ⚡ How PROGMEM Works

The ESP8266 has limited RAM (~80 KB usable). Storing large strings in RAM would crash the device. `PROGMEM` tells the compiler to keep the string in **flash memory** (4 MB on NodeMCU), and `send_P()` streams it directly from flash to the TCP socket without loading it into RAM.

```cpp
// Stored in FLASH — costs 0 RAM:
const char index_html[] PROGMEM = R"rawliteral(
  ... your entire HTML page ...
)rawliteral";

// Streamed from flash to browser — never touches heap:
request->send_P(200, "text/html", index_html);
```

---

## 🚀 Getting Started

### 1. Install Libraries

In Arduino IDE → **Tools → Manage Libraries**:
- Search `ArduinoJson` → Install (by Benoit Blanchon, version 6.x)
- Search `ESPAsyncTCP` → Install (by me-no-dev)
- Search `ESP Async WebServer` → Install (by me-no-dev)

### 2. Set Your Wi-Fi

Open `tank.ino` and find:
```cpp
const char *ssid     = "YOUR_SSID";
const char *password = "YOUR_PASSWORD";
```
Replace with your actual Wi-Fi credentials.

### 3. Select Your Board

**Tools → Board → ESP8266 Boards → NodeMCU 1.0 (ESP-12E Module)**  
(or **Wemos D1 Mini** if that's your board)

Set **Upload Speed: 921600** for fastest flashing.

### 4. Upload

Click **Upload (→)**. Wait for "Done uploading."

### 5. Open Serial Monitor

**Tools → Serial Monitor** → set baud to **115200**.  
You'll see:
```
Connecting to WiFi.....
Connected to WiFi
IP Address: 192.168.1.105
Open: http://192.168.1.105
HTTP server started
```

### 6. Open the Dashboard

Navigate to `http://192.168.1.105/` (use your actual IP) in any browser on the same Wi-Fi.

---

## 🔧 Troubleshooting

| Problem | Likely Cause | Fix |
|---|---|---|
| Dashboard shows `—` for level | Sensor not wired or placed | Check wiring; sensor needs ≥ 20 cm clearance |
| Sensor alert: "Too near" | Object closer than 20 cm (blind zone) | Move sensor further from water surface |
| Level jumps erratically | Reflections inside tank | Add baffles; ensure sensor faces straight down |
| Can't connect to Wi-Fi | Wrong SSID/password | Re-flash with correct credentials |
| Browser can't reach device | Wrong IP / different network | Check Serial Monitor for correct IP |
| Level reads 100% when empty | `empty_distance_cm` < `full_distance_cm` | Swap the values in Settings |

---

## 📁 Repository Structure

```
water-tank/
├── tank.ino          ← Everything in one file. This is what you flash.
├── docs/
│   └── wiring_diagram.jpg   ← Circuit wiring diagram
├── .gitignore
└── README.md
```

---

## 📄 License

MIT — use freely, modify freely, just keep the attribution.
