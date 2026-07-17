# 💧 ESP8266 Water Tank Level Monitor

A real-time water-level dashboard running entirely on an **ESP8266** (NodeMCU / Wemos D1 Mini) with a **JSN-SR04M** ultrasonic sensor. No cloud, no app, no internet required — just connect to the device's IP in any browser.

---

## Features

| Feature | Detail |
|---|---|
| Live dashboard | WebSocket push every 5 s, animated SVG tank visual |
| Noise rejection | IQR-filtered median over 5 sensor samples |
| In-browser calibration | Adjust empty/full distances and capacity without reflashing |
| PROGMEM hosting | HTML, CSS, JS served from flash — no SD card or LittleFS needed |
| Exponential back-off | Client auto-reconnects up to 8 times on disconnect |

---

## Hardware

| Pin | GPIO | Label | Connection |
|-----|------|-------|------------|
| TRIG | 12 | D6 | JSN-SR04M TRIG |
| ECHO | 13 | D7 | JSN-SR04M ECHO |
| VCC | — | 3.3 V / 5 V | Sensor VCC |
| GND | — | GND | Sensor GND |

> The JSN-SR04M is waterproof and can measure 20–400 cm.

---

## Project Structure

```
tank/
├── platformio.ini          # PlatformIO build config
├── src/
│   ├── main.cpp            # Firmware (WiFi, WebSocket, sensor logic)
│   └── web_assets.h        # PROGMEM-embedded HTML/CSS/JS (auto-generated)
├── data/                   # Canonical web asset sources (edit here)
│   ├── index.html
│   ├── style.css
│   └── app.js
└── scripts/
    └── embed_assets.py     # Regenerates web_assets.h from data/
```

---

## Getting Started

### 1. Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- Python 3 (for the asset-embed helper)

### 2. Configure Wi-Fi credentials

Open `src/main.cpp` and update:

```cpp
const char *ssid     = "YOUR_SSID";
const char *password = "YOUR_PASSWORD";
```

> **Security tip:** create `src/credentials.h` (already gitignored) and `#include` it instead.

### 3. Flash

```bash
pio run -t upload
pio device monitor   # note the IP address printed
```

### 4. Open the dashboard

Navigate to `http://<device-ip>/` in any browser on the same network.

---

## Editing the Web UI

1. Edit the files in `data/`.
2. Run the embed script to regenerate `src/web_assets.h`:
   ```bash
   python3 scripts/embed_assets.py
   ```
3. Re-flash with `pio run -t upload`.

---

## Sensor Calibration

Open the **⚙️ Settings** modal in the dashboard and enter:

| Field | Meaning |
|---|---|
| **Empty Distance** | Distance (cm) from sensor to tank bottom when empty |
| **Full Distance** | Distance (cm) from sensor to water surface when full |
| **Capacity** | Total tank volume in litres |

Click **Save to ESP8266** — changes take effect immediately without reflashing.

---

## WebSocket Protocol

### Server → Client (`state`)

```json
{
  "type": "state",
  "devices": [{
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
  }],
  "settings": {
    "empty_distance_cm": 100,
    "full_distance_cm": 20
  }
}
```

### Client → Server (`update_settings`)

```json
{
  "type": "update_settings",
  "empty_distance_cm": 100,
  "full_distance_cm": 20,
  "capacity_l": 1000
}
```

---

## License

MIT
