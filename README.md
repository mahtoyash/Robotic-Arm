<div align="center">

# 🦾 Voice-Controlled 5-DOF Robotic Arm

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Microcontroller](https://img.shields.io/badge/MCU-ESP32-blue.svg)](https://www.espressif.com/)
[![Servo Driver](https://img.shields.io/badge/Driver-PCA9685%20(16--Ch)-red.svg)](https://www.adafruit.com/product/815)
[![Python](https://img.shields.io/badge/Python-3.8+-green.svg)](https://www.python.org/)
[![Status](https://img.shields.io/badge/Status-Active%20Development-success.svg)]()

**An intelligent 5-DOF, 2-link robotic arm powered by ESP32, offering real-time dual-mode voice control (Cloud AI & Local Offline) with multilingual command support (English & Hindi) and an integrated Web Dashboard.**

[Overview](#-project-overview) • [Key Features](#-key-features) • [System Architecture](#-system-architecture) • [Bill of Materials](#-bill-of-materials-bom) • [Wiring & Schematics](#-wiring--connections) • [Getting Started](#-getting-started) • [Voice Commands](#-voice-command-dictionary) • [Contributing](#-contributing)

</div>

---

## 📌 Project Overview

This project is a fully articulated **5 Degrees of Freedom (5-DOF)** robotic arm with a 2-link mechanical kinematic chain. Built with heavy-duty metal-gear servos (MG996R) for base, shoulder, and elbow articulations, and precision micro servos (MG90S) for wrist roll, wrist pitch, and gripper actions.

The arm can be operated through **three distinct control paradigms**:
1. **Local Offline Voice Control (Android Termux / PC)**: Zero internet dependency. Processes spoken English and Hindi commands with sub-second latency and triggers the arm over local Wi-Fi.
2. **Cloud AI Voice Control (Groq Whisper API)**: Direct on-board recording via an INMP441 I2S MEMS microphone, streaming audio directly from the ESP32 to Groq Whisper for high-accuracy cloud transcription.
3. **Interactive Web Dashboard**: A responsive, browser-based graphical user interface hosted directly by the ESP32, providing real-time servo sliders, step increments, and one-click macro poses (Home, Rest, Pick, Place).

---

## 🎯 Objectives & Goals

- **Multilingual Voice Interaction**: Native recognition of both English (`"grab"`, `"up"`, `"sleep"`) and Hindi (`"pakad"`, `"upar"`, `"so jao"`) commands.
- **Low Latency & Standalone Reliability**: Dual-network architecture allowing the arm to operate entirely offline without external internet or cloud latency.
- **Hardware Protection & Calibration First**: Dedicated single-joint isolation calibration sketches to establish software bounds, preventing gear stripping and motor stalls.
- **Modular Embedded Codebase**: Clean separation between hardware abstraction (PCA9685 I2C driver), networking (AP + Station dual mode), and speech recognition server layers.

---

## 🧠 System Architecture

```mermaid
flowchart TD
    subgraph Input_Modes["🎮 Input & Control Channels"]
        A1["📱 Smartphone Mic / Termux"] -->|HTTP REST| B1["Python Local Server<br>(arm_server.py)"]
        A2["🎙️ INMP441 MEMS Mic"] -->|I2S Bus| B2["ESP32 Audio Streamer<br>(WAV Chunking)"]
        A3["💻 Web Browser Studio"] -->|HTTP / WebSockets| C["ESP32 Core Brain<br>(Dual Mode: AP / STA)"]
    end

    B1 -->|REST API JSON| C
    B2 -->|HTTPS Post| D["☁️ Groq Cloud API<br>(Whisper STT)"]
    D -->|Transcribed Text| C

    subgraph Hardware_Execution["⚙️ Physical Hardware Execution"]
        C -->|I2C Bus (SDA:21, SCL:22)| E["PCA9685 16-Channel<br>PWM Servo Driver"]
        F["⚡ SMPS (5V / 10A)"] -->|Servo Rail Power| E
        E -->|CH 0| M1["Base (MG996R 360°)"]
        E -->|CH 2| M2["Shoulder (MG996R 180°)"]
        E -->|CH 4| M3["Elbow (MG996R 180°)"]
        E -->|CH 6| M4["Wrist Roll (MG90S 180°)"]
        E -->|CH 8| M5["Wrist Pitch (MG90S 180°)"]
        E -->|CH 10| M6["Gripper Claw (MG90S 180°)"]
    end
```

---

## 🧰 Bill of Materials (BOM)

### 1. Actuators
| Component | Model | Qty | Joint Role | Operating Spec |
|:---|:---|:---:|:---|:---|
| High-Torque Servo | **MG996R** | 1 | Base Rotation | 360° Continuous Rotation (Dead-stop @ 290) |
| High-Torque Servo | **MG996R** | 2 | Shoulder & Elbow | 180° Positional (Metal Gears, high stall torque) |
| Micro Metal-Gear Servo | **MG90S** | 3 | Wrist Roll, Pitch, Gripper | 180° Positional (Lightweight end-effector) |

### 2. Electronics & Brain
| Component | Specification | Role |
|:---|:---|:---|
| Microcontroller | **ESP32 DevKit V1 (30-pin / 38-pin)** | Wi-Fi server, I2S sampler, I2C master controller |
| PWM Servo Driver | **PCA9685 (16-Channel 12-bit I2C)** | Offloads PWM generation from the ESP32 |
| Digital Microphone | **INMP441 Omnidirectional MEMS** | Low-noise digital I2S microphone module |
| Power Supply | **SMPS (5V DC, 10A output)** | High-current regulated power for all 6 servos |

---

## 🔌 Wiring & Connections

### 1. ESP32 to PCA9685 (I2C)
| ESP32 Pin | PCA9685 Pin | Description |
|:---|:---|:---|
| **GPIO 21** | **SDA** | I2C Data Line |
| **GPIO 22** | **SCL** | I2C Clock Line |
| **3.3V** | **VCC** | Logic Power |
| **GND** | **GND** | Common Ground |
| *External* | **V+** | Connected directly to SMPS +5V (Servo Power Rail) |

### 2. ESP32 to INMP441 Microphone (I2S)
| ESP32 Pin | INMP441 Pin | Note |
|:---|:---|:---|
| **GPIO 25** | **WS** (LRCLK) | Word Select |
| **GPIO 26** | **SCK** (BCLK) | Bit Clock |
| **GPIO 34** | **SD** (DIN) | Serial Data (Input-only pin) |
| **3.3V** | **VDD** | 3.3V Logic Power |
| **GND** | **GND / L/R** | Ground (L/R tied to GND selects Left Channel) |

### 3. PCA9685 Servo Channel Map & Calibrated Limits
| Channel | Joint | Type | Calibrated Working Pulse Range |
|:---:|:---|:---|:---|
| **CH 0** | Base Rotation | 360° Continuous | Center Stop: `290` \| Left: `<290` \| Right: `>290` |
| **CH 2** | Shoulder | 180° Positional | Min: `180` \| Max: `500` \| Default: `~300` |
| **CH 4** | Elbow | 180° Positional | Min: `85` \| Max: `505` \| Default: `~295` |
| **CH 6** | Wrist Roll | 180° Positional | Min CCW: `100` \| Max CW: `472` \| Home: `160` |
| **CH 8** | Wrist Pitch | 180° Positional | Min Down: `95` \| Max Up: `460` \| Home: `252` |
| **CH 10** | Gripper Claw | 180° Positional | Closed (Grip): `380` \| Fully Open: `180` |

---

## 📂 Repository Layout

```text
├── .github/                       # Issue and Pull Request templates
│   ├── ISSUE_TEMPLATE/
│   │   ├── bug_report.md
│   │   └── feature_request.md
│   └── pull_request_template.md
├── firmware/                      # ESP32 Arduino sketches
│   ├── base_calib/                # Single-joint calibration for Base
│   ├── shoulder_calib/            # Single-joint calibration for Shoulder
│   ├── elbow_calib/               # Single-joint calibration for Elbow
│   ├── wrist_calib/               # Single-joint calibration for Wrist Pitch
│   ├── roll_calib/                # Single-joint calibration for Wrist Roll
│   ├── gripper_calib/             # Single-joint calibration for Gripper
│   ├── inmp441_test/              # Diagnostic I2S audio test for MEMS mic
│   ├── roboarm_dashboard/         # Full manual Wi-Fi Web Dashboard
│   ├── roboarm_cloud_voice/       # Cloud voice control using Groq Whisper API
│   └── roboarm_phone_offline_voice/ # Offline phone-assisted voice firmware
├── scripts/                       # Host server & tools
│   ├── arm_server.py              # Python Flask local speech recognition server
│   ├── sync_header.py             # Compiles web studio HTML into ESP32 PROGMEM header
│   └── web_voice_studio.html      # Responsive web UI for testing & voice studio
├── CODE_OF_CONDUCT.md             # Contributor Covenant Code of Conduct
├── CONTRIBUTING.md                # Contribution guidelines and workflow
├── LICENSE                        # MIT License
├── README.md                      # Project documentation
└── ROBO_ARM_JOURNAL.md            # Hardware journal, calculations, logs
```

---

## 🚀 Getting Started

### Prerequisites
- [Arduino IDE](https://www.arduino.cc/en/software) with ESP32 board support.
- Python 3.8+ on your host machine or Android phone with [Termux](https://termux.dev/).
- Libraries required in Arduino IDE:
  - `Adafruit PWM Servo Driver Library`

---

### Step 1: Calibration (Crucial First Step)
Before assembling all joints or running automated routines, calibrate each joint to prevent physical over-travel:
1. Open any sketch from `firmware/*_calib/*.ino` (e.g. `shoulder_calib.ino`).
2. Upload it to the ESP32.
3. Connect your phone or laptop to the Wi-Fi access point broadcasted by the ESP32 (e.g., `RoboArm-Shoulder-Calib`, password `roboarm123`).
4. Navigate to `http://192.168.4.1` in your browser.
5. Use the sliders and step buttons to determine your safe minimum, maximum, and home pulse values.

---

### Step 2: Option A — Local Offline Voice Control (Recommended)
Fast, zero internet required, multilingual!

1. Flash `firmware/roboarm_phone_offline_voice/roboarm_phone_offline_voice.ino` to the ESP32.
2. In the code, set your Wi-Fi SSID and password:
   ```cpp
   const char* HOME_SSID = "YOUR_WIFI_SSID";
   const char* HOME_PASS = "YOUR_WIFI_PASSWORD";
   ```
3. On your Android smartphone (in Termux) or PC:
   ```bash
   cd scripts
   pip install flask requests SpeechRecognition
   python arm_server.py
   ```
4. The server will detect your speech in real-time, translate the command, and dispatch REST calls directly to the arm.

---

### Step 3: Option B — Cloud Voice Control (Groq Whisper)
1. Open `firmware/roboarm_cloud_voice/roboarm_cloud_voice.ino`.
2. Insert your Wi-Fi credentials and Groq API Key:
   ```cpp
   const char* WIFI_SSID     = "YOUR_WIFI_SSID";
   const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
   const char* GROQ_API_KEY  = "YOUR_GROQ_API_KEY_HERE";
   ```
3. Upload to the ESP32. Speak into the INMP441 microphone after the beep/trigger. The audio will be uploaded to Groq Whisper for instant transcription and arm execution.

---

### Step 4: Option C — Web Dashboard Manual Control
1. Open and upload `firmware/roboarm_dashboard/roboarm_dashboard.ino`.
2. Connect to the `RoboArm-Studio` Wi-Fi AP or view its assigned STA IP on the Serial Monitor.
3. Open `http://192.168.4.1` (or local IP) to control all 6 servos from interactive sliders and execute preset poses.

---

## 🎙️ Voice Command Dictionary

The system understands natural voice commands in both English and Hindi:

| Action | English Voice Trigger | Hindi Voice Trigger (हिन्दी) |
|:---|:---|:---|
| **Awaken / Wake** | `"hey robo"`, `"hello robo"`, `"wake up"` | `"uth"`, `"utho"`, `"jag"` |
| **Sleep / Rest** | `"sleep"`, `"relax"`, `"rest"` | `"so jao"`, `"aaram karo"` |
| **Grip / Close** | `"grab"`, `"close"`, `"hold"`, `"catch"` | `"pakad"`, `"pakdo"`, `"band"` |
| **Release / Open** | `"open"`, `"release"`, `"drop"` | `"khol"`, `"kholo"`, `"chhod"` |
| **Base Left** | `"left"`, `"turn left"` | `"baayein"`, `"left ghumo"` |
| **Base Right** | `"right"`, `"turn right"` | `"daayein"`, `"right ghumo"` |
| **Arm Up** | `"up"`, `"lift"` | `"upar"`, `"upar uthao"` |
| **Arm Down** | `"down"`, `"lower"` | `"neeche"`, `"jhuko"` |
| **Wrist CW / CCW** | `"roll left"`, `"roll right"` | `"kalai ghumao"` |
| **Reset / Home** | `"reset"`, `"home"` | `"seedhe ho jao"`, `"home"` |

---

## 📸 Media & Demos

*(Add your robotic arm photos, video links, or GIFs here)*

```text
[ Hardware Setup Photo ]       [ Web Studio Interface ]       [ Voice Control Demo GIF ]
```

---

## 🤝 Contributing

Contributions make the open-source community an amazing place to learn, inspire, and create! Any contributions you make are **greatly appreciated**.

Please see [CONTRIBUTING.md](CONTRIBUTING.md) for details on code style, testing, and pull request procedures.

---

## 🛡️ Code of Conduct

We are committed to providing a friendly, safe, and welcoming environment for all. Please review our [Code of Conduct](CODE_OF_CONDUCT.md) before participating.

---

## 📜 License

Distributed under the **MIT License**. See [LICENSE](LICENSE) for more details.

---

<div align="center">
  <b>Built with ❤️ by <a href="https://github.com/mahtoyash">Yash Mahto</a></b>
</div>
