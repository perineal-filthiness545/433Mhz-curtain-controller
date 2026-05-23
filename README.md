# Abalon Curtain 433MHz Analyzer and Controller

A web-based 433 MHz OOK signal analyzer, BMC protocol decoder, and controller for the **Abalon electric curtain motor** (and similar rolling-code 433 MHz OOK devices). Runs on an ESP32-C3 with a CC1101 RF transceiver. Integrates with Home Assistant via MQTT auto-discovery.

---

## Features

- **Live capture** — edge-timing ISR capture via CC1101 GDO0 async mode; packets stored in RAM
- **BMC decoder** — auto-detects Biphase Mark Code (T ≈ 360 µs), decodes up to 64 bits per frame
- **Capture wizard** — guided multi-capture flow with consistency check (detects rolling codes)
- **Flash persistence** — save captures to ESP32 NVS (survives reboots and scan resets)
- **BMC replay** — re-encodes stored BMC bytes and retransmits via CC1101 OOK async TX
- **Home Assistant** — MQTT auto-discovery publishes a `garage`-class Cover entity and individual Button entities per saved signal
- **Web UI** — single-page app served directly from the ESP32; no cloud, no app

---

## Hardware

| Part | Details |
|---|---|
| MCU | ESP32-C3 DevKitM-1 (single-core RISC-V, 160 MHz) |
| RF transceiver | CC1101 module (433 MHz) |
| Status LED | WS2812B on GPIO8 (optional) |

### CC1101 wiring

```
   ESP32-C3              CC1101
 ┌──────────────┐    ┌──────────────┐
 │         3.3V ├────┤ VCC          │
 │          GND ├────┤ GND          │
 │       GPIO 4 ├────┤ CSn          │
 │       GPIO 6 ├────┤ SCK          │
 │       GPIO 7 ├────┤ MOSI         │
 │       GPIO 5 ├────┤ MISO         │
 │       GPIO 3 ├────┤ GDO0         │
 │              │    │ GDO2 (n/c)   │
 └──────────────┘    └──────────────┘
```

> **Note:** CC1101 is 3.3 V only — do not connect VCC to 5 V.  
> GDO0 serves as ISR edge input during RX and async serial data output during TX.

---

## Protocol

The Abalon remote uses **OOK at 433.92 MHz** with **Biphase Mark Code (BMC)** encoding:

| Parameter | Value |
|---|---|
| Frequency | 433.92 MHz |
| Modulation | OOK (On-Off Keying) |
| Encoding | BMC (Biphase Mark Code) |
| Bit period T | ≈ 362 µs |
| Frame | 64 data bits, 3 repetitions |
| Security | Rolling code |

Because of the rolling code, **each button press can only be replayed once**. Capture a code while the motor is powered on, replay it immediately — the motor re-syncs on each successful command.

---

## Build & Flash

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or IDE extension)
- ESP32-C3 DevKitM-1 connected via USB

### Build and upload

```bash
pio run -t upload
```

Monitor serial output (115200 baud):

```bash
pio device monitor
```

### First-boot WiFi setup

On first boot (or after a WiFi reset) the device starts in access point mode:

1. Connect your phone or laptop to the **`433MHz-Setup`** WiFi network
2. Browse to **`http://192.168.4.1`** (most devices open a captive portal automatically)
3. Click **Scan Networks**, select your SSID, enter the password, and click **Save & Connect**
4. The device reboots and connects to your network; its IP address is printed on the serial monitor

To reset WiFi credentials later, open the web UI → **Home Assistant / MQTT** → **Reset WiFi**.

---

## Web UI

Navigate to the device IP in a browser.

| Section | Purpose |
|---|---|
| **Capture Wizard** | Guided 3-capture flow per button; shows match score (rolling code detection) |
| **Quick Capture** | Single capture per named button |
| **Packet list** | Live view of captured pulses, BMC decode, byte comparison across labels |
| **TX Replay** | Play back saved BMC captures; ☆ Save / ★ Del to persist to flash |
| **Download JSON** | Export all captures for offline analysis |
| **Home Assistant / MQTT** | Configure MQTT broker, set device name, republish discovery |

---

## Home Assistant Integration

The device uses **MQTT auto-discovery** — no manual YAML required.

### Requirements

- Mosquitto (or any MQTT broker) accessible from the ESP32
- HA MQTT integration configured with the same broker  
  *(Settings → Devices & Services → + Add Integration → MQTT)*

### Setup

1. Open the web UI → expand **Home Assistant / MQTT**
2. Set a **Device name** (e.g. `Living Room Curtain`)
3. Enter broker IP, port, and credentials if required
4. Click **Save & Connect**
5. After saving your OPEN / CLOSE / PAUSE signals, click **Republish Discovery**

### What gets created in HA

| Entity type | Condition | Name |
|---|---|---|
| Cover (`device_class: garage`) | OPEN + CLOSE both saved | Device name you set |
| Button | Each saved signal | `<Device name> <LABEL>` |

The cover entity accepts `OPEN`, `CLOSE`, and `STOP` commands. `STOP` maps to a saved label named `PAUSE` or `STOP`.

---

## Libraries

| Library | Purpose |
|---|---|
| [RadioLib](https://github.com/jgromes/RadioLib) | CC1101 init, RSSI |
| [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) | Non-blocking HTTP server |
| [AsyncTCP](https://github.com/me-no-dev/AsyncTCP) | Async TCP for ESP32 |
| [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) | WS2812B status LED |
| [PubSubClient](https://github.com/knolleary/pubsubclient) | MQTT client |
| Preferences (built-in) | NVS flash storage |

---

## License

BSD 2-Clause — see [LICENSE](LICENSE).
