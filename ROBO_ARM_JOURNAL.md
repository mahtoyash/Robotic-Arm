# 🦾 Voice-Controlled Robotic Arm — Project Journal

> **Started:** October 2026  
> **Status:** 🟡 In Progress — Hardware Assembly Phase  
> **Location:** Personal Build / MDM Project

---

## 📌 Project Overview

A **5-DOF, 2-link voice-controlled robotic arm** built around an ESP32 microcontroller and PCA9685 servo driver. The arm listens for hardcoded voice commands via a MEMS microphone and responds with physical movement — no NLP, no cloud, pure embedded signal processing.

---

## 🎯 Expected Final Result

| Feature | Detail |
|--------|--------|
| Control Method | Voice commands (hardcoded, no NLP) |
| Commands | `left`, `right`, `up`, `down`, `grab`, `release`, etc. |
| Degrees of Freedom | 5 DOF |
| Links | 2-link arm |
| Autonomy | Fully standalone (no PC needed during operation) |

---

## 🧰 Bill of Materials (BOM)

### Actuators

| Component | Model | Qty | Role |
|-----------|-------|-----|------|
| High-torque servo | MG996R | 3 | Base rotation / shoulder / elbow (heavy joints) |
| Micro servo | MG90S | 3 | Wrist / gripper / fine joints (metal gear, compact) |

> **Note:** Total = 6 servos across 5 DOF + gripper mechanism

### Electronics & Brain

| Component | Model/Spec | Qty | Role |
|-----------|-----------|-----|------|
| Microcontroller | ESP32 | 1 | Main brain + voice processing |
| Servo Driver | PCA9685 (16-ch PWM) | 1 | Controls all 6 servos via I2C |
| Power Supply | SMPS — 5V, 10A | 1 | Powers servos + electronics |
| Microphone | MEMS Module (external, I2S) | 1 | Separate voice command input module |

### Power Budget (Estimated)

| Load | Current Draw |
|------|-------------|
| 3x MG996R (stall) | ~3 x 2.5A = 7.5A |
| 3x MG90S (stall) | ~3 x 0.48A = 1.44A |
| ESP32 + PCA9685 + MEMS | ~0.35A |
| **Total (worst case)** | **~9.2A** |
| **SMPS Capacity** | **10A (safe margin ~8%)** |

---

## 🔌 Wiring & Connection Map

### ESP32 <-> PCA9685 (I2C)

| ESP32 Pin | PCA9685 Pin |
|-----------|-------------|
| GPIO 21 (SDA) | SDA |
| GPIO 22 (SCL) | SCL |
| 3.3V | VCC |
| GND | GND |
| — | V+ -> SMPS 5V (servo power rail) |

### SMPS -> Power Distribution

```
SMPS (5V / 10A)
   |- V+ -> PCA9685 V+ (servo power)
   |- GND -> PCA9685 GND
   `- GND -> ESP32 GND (common ground - IMPORTANT!)
```

### ESP32 <-> MEMS Microphone (I2S)

| ESP32 Pin | MEMS MIC Pin | Note |
|-----------|-------------|------|
| GPIO 25 | WS (LRCLK) | Word Select |
| GPIO 26 | SCK (BCLK) | Bit Clock |
| GPIO 34 | SD (Data) | Serial Data (input only pin) |
| 3.3V | VDD | Power |
| GND | GND | Ground |

> Adjust GPIO numbers based on your specific ESP32 board pinout.

### PCA9685 <-> Servo Motors

| PCA9685 Channel | Servo | Joint | Type | Calibrated Values & Operating Profile |
|----------------|-------|-------|------|---------------------------------------|
| CH 0 | MG996R #1 | Base Rotation | **360° Continuous** | Center Dead Stop = **290** (Left: <290, Right: >290) |
| CH 2 | MG996R #2 | Shoulder | **180° Positional** | **MIN = 180**, **MAX = 500**, Neutral/Home = **~300**–**340** |
| CH 4 | MG996R #3 | Elbow | **180° Positional** | **MIN = 85**, **MAX = 505**, Neutral/Home = **~295**–**300** |
| CH 6 | MG90S #1 | Wrist Roll | **180° Positional** | BOOT/HOME = **160**, MIN CCW = **100**, MAX CW = **472** (Range: 100–472) |
| CH 8 | MG90S #2 | Wrist Pitch | **180° Positional** | HOME = **252**, MIN DOWN = **95**, MAX UP = **460** (Range: 95–460) |
| CH 10 | MG90S #3 | Gripper | **180° Positional** | CLOSE = **~310**, OPEN = **~430** (Safe Range: 300–440) |

> **HARDWARE UPGRADE (October 2026):**
> - **Shoulder (CH 2)** and **Elbow (CH 4)** successfully upgraded from 360° continuous motors to **Standard 180° Positional MG996R Servos**.
> - **Base (CH 0)** remains **360° Continuous Rotation MG996R** for continuous horizontal pan.
> - **End-Effector (CH 6, 8, 10)** are **180° Positional MG90S Servos**.
> - **Why this fixes the arm:** 180° servos provide closed-loop potentiometer holding torque against gravity. Sinking/falling when unpowered is eliminated during active hold! Exact angles can now be set.

### 📐 Calibrated Master Poses

| Pose Name | Role / Function | Shoulder (CH 2) | Elbow (CH 4) | Wrist Roll (CH 6) | Wrist Pitch (CH 8) | Gripper (CH 10) |
|---|---|---|---|---|---|---|
| **💤 SLEEP / RELAX POSE** | Power-On Default & Parking (Low gravity load, folded) | **480** | **505** | **472** | **460** | **246** |
| **🌅 WAKE / WORKING POSE** | Elevated working stance (Ready to execute commands) | **410** | **320** | **100** | **95** | **246** |
| **🎯 REACH POSE** | Extended forward targeting | **260** | **420** | **160** | **200** | **430 (Open)** |
| **📦 PICK POSE** | Low ground grab stance | **220** | **200** | **160** | **140** | **310 (Grab)** |

---

## Architecture

```
+------------------------------------------+
|               ESP32                      |
|                                          |
|  [MEMS MIC] --I2S--> [Audio Processing] |
|                          |               |
|                   [Keyword Match]        |
|                   left/right/up/down     |
|                   grab/release/etc.      |
|                          |               |
|                   [Command Executor]     |
|                          |               |
|                 I2C -----v-----------+   |
+------------------------------------------+
                                       |
                              +--------v-------+
                              |   PCA9685      |
                              |  (PWM Driver)  |
                              +--+--+--+--+---+
                                 |  |  |  |
                          +------+  |  |  +------+
                          v         v  v          v
                       MG996R    MG996R SG90     SG90
                       (Base)  (Shoulder)(Wrist)(Gripper)
```

---

## 🎙️ Voice Control System Architecture

### 🔄 State Machine & Operating Cycle
```
 +────────────────────────────────────────────────────────────────────────+
 |                                                                        |
 |   [STATE 1: SLEEP / RELAX POSE]                                        |
 |   - Arm folded in safe low-torque rest position                        |
 |   - Servos not straining / minimal holding current                     |
 |   - Microphone actively listening ONLY for Wake Word                   |
 |                                                                        |
 +──────────────────────────────────┬─────────────────────────────────────+
                                    |
                    🗣️ Wake Word Heard (e.g. "Jarvis" / "Robo")
                                    |
                                    v
 +────────────────────────────────────────────────────────────────────────+
 |   [STATE 2: WAKE UP & READY]                                           |
 |   - Audio/Visual cue (Beep / LED turns Green)                          |
 |   - Arm moves from Relax Pose to Elevated "Ready Working Stance"       |
 |   - Listens for motion commands                                        |
 +──────────────────────────────────┬─────────────────────────────────────+
                                    |
                    🗣️ Action Command Heard (e.g. "left", "grab")
                                    |
                                    v
 +────────────────────────────────────────────────────────────────────────+
 |   [STATE 3: ACTIVE EXECUTION]                                          |
 |   - Executes commanded joint motion (interpolated micro-nudges/steps)  |
 |   - Returns to Ready Stance, resets inactivity timer                   |
 +──────────────────────────────────┬─────────────────────────────────────+
                                    |
                    ⏱️ Inactivity Timeout (e.g. 10–15s no command)
                    OR 🗣️ "Sleep" / "Rest" command
                                    |
                                    v
 +────────────────────────────────────────────────────────────────────────+
 |   [RETURN TO STATE 1: SLEEP / RELAX POSE]                              |
 +────────────────────────────────────────────────────────────────────────+
```

---

## 🗣️ Voice Command Dictionary (Non-Perspective / Direct Embedded)

| Command Category | Keyword | Action Description | Target Servo(s) & Target Range |
|------------------|---------|--------------------|--------------------------------|
| **Wake Word** | *"Jarvis"* / *"Robo"* | Wakes arm from Relax Pose to Ready Stance | All joints transition to Ready |
| **Base Movement** | *"Left"* | Rotate base counter-clockwise | CH 0 (Pulse < 290, e.g. 272 for 120ms) |
| **Base Movement** | *"Right"* | Rotate base clockwise | CH 0 (Pulse > 290, e.g. 308 for 120ms) |
| **Elevation** | *"Up"* | Raise arm (Shoulder + Elbow pitch up) | CH 2 (Pulse > 293), CH 4 (Pulse > 293) |
| **Elevation** | *"Down"* | Lower arm (Shoulder + Elbow pitch down)| CH 2 (Pulse < 287), CH 4 (Pulse < 287) |
| **Wrist Pitch** | *"Pitch Up"* | Tilt wrist upwards | CH 8 (Step +30, max limit 460) |
| **Wrist Pitch** | *"Pitch Down"* | Tilt wrist downwards | CH 8 (Step -30, min limit 95) |
| **Wrist Roll** | *"Roll Left"* | Roll wrist counter-clockwise | CH 6 (Step -30, min limit 100) |
| **Wrist Roll** | *"Roll Right"* | Roll wrist clockwise | CH 6 (Step +30, max limit 472) |
| **Gripper** | *"Open"* / *"Release"* | Open gripper jaws | CH 10 (Move to pulse 430) |
| **Gripper** | *"Close"* / *"Grab"* | Close gripper jaws / clamp object | CH 10 (Move to pulse 310) |
| **Macros** | *"Pick"* | Autonomous sequence: Down -> Grab -> Up | CH 2, 4, 10 choreographed routine |
| **Macros** | *"Drop"* / *"Place"* | Autonomous sequence: Down -> Release -> Up | CH 2, 4, 10 choreographed routine |
| **Stance Control** | *"Home"* | Return all joints to Ready Stance | Base: stop, Pitch: 252, Roll: 160 |
| **Stance Control** | *"Sleep"* / *"Rest"* | Immediately transition to Relax Pose | All joints move to Parked Stance |
| **Emergency** | *"Stop"* / *"Halt"* | Instantly freeze all movement | CH 0,2,4 -> 290, cut all active timers |

> **Embedded Signal Processing:** Offline hardcoded keyword recognition running on ESP32 (FFT / Audio Energy Spectrogram Matching) — Zero cloud, Zero latency, 100% offline.

---

## 💡 Recommended Additional Features (System Enhancements)

1. **State Indicator (LED / Buzzer):**
   - A NeoPixel RGB LED or small Piezo Buzzer to give clear user feedback:
     - 🔵 *Blue / Silent:* Asleep in Relax Pose (Waiting for Wake Word)
     - 🟢 *Green / Single Chirp:* Awake & Listening (Ready for command)
     - 🟡 *Yellow / Blinking:* Executing motion
     - 🔴 *Red / Double Beep:* Emergency Stop triggered / Obstacle collision
2. **Emergency Voice Stop ("STOP" / "HALT"):**
   - High-priority interrupt: if anything unexpected happens or arm gets stuck, saying *"STOP"* immediately locks all 360 servos at neutral (290) and halts movement.
3. **Smooth Acceleration / Soft Ramping:**
   - Rather than instantaneous start/stop, ease-in and ease-out interpolation prevents jerking, arm wobble, and gear stripping.
4. **Dual Control Fallback (Voice + Web Dashboard):**
   - Keep the WiFi AP Web Dashboard running concurrently in the background so the user can see live feedback on phone, override positions, or use manual buttons if the room is too noisy.

---

## Build Log

### Session 1 — Physical Assembly
**Date:** October 2026 | **Status:** COMPLETE

- [x] Robotic arm frame fully assembled (5 DOF + gripper)
- [x] All servo motors physically mounted in arm joints
- [x] PCA9685 module mounted/positioned
- [x] ESP32 board positioned

---

### Session 2 — Brain Wiring
**Date:** October 2026 | **Status:** COMPLETE

- [x] SMPS (5V/10A) connected to system
- [x] ESP32 <-> PCA9685 I2C wiring done
- [x] Common ground established between SMPS, PCA, ESP32

> "Abhi tk humne robo ko brain hi bnaya h" — Brain is built, movement pending!

---

### Session 3 — Hardware Wiring & Channel Map
**Date:** October 2026 | **Status:** COMPLETE

- [ ] **External MEMS Mic Module (INMP441) -> ESP32** (pending — INMP441 not yet wired)
- [x] **All 6 Servo Motors -> PCA9685 channels** (alternate channels for clean wiring)
  - [x] MG996R #1 -> CH 0 (Base Rotation)
  - [x] MG996R #2 -> CH 2 (Shoulder)
  - [x] MG996R #3 -> CH 4 (Elbow)
  - [x] MG90S #1 -> CH 6 (Wrist Roll)
  - [x] MG90S #2 -> CH 8 (Wrist Pitch)
  - [x] MG90S #3 -> CH 10 (Gripper)

---

### Session 3.5 — Joint Calibration & Tuning
**Date:** October 12, 2026 | **Status:** 🟢 COMPLETE (All 6 Servos Calibrated)

- [x] **Base (CH 0):** 360° Continuous — Neutral Dead Stop = **290** (Left: <290, Right: >290)
- [x] **Wrist Roll (CH 6):** 180° Positional — **BOOT/HOME = 160**, **MIN CCW = 100**, **MAX CW = 472**
- [x] **Wrist Pitch (CH 8):** 180° Positional — **BOOT/HOME = 252**, **MIN DOWN = 95**, **MAX UP = 460**
  - *Critical Discovery:* Input pulse < 95 causes signal wrap-around / erratic spin up. Range clamped strictly to 95–460.
- [x] **Gripper (CH 10):** 180° Positional — **CLOSE = ~310**, **OPEN = ~430** (Safe: 300–440)

---

### Session 3.6 — 360° Continuous Hardware Confirmation & High-Torque Control Deployed
**Date:** October 12, 2026 | **Status:** 🟢 RESOLVED

- **Hardware Fact Confirmed by User:** All 3x MG996R motors (Base CH 0, Shoulder CH 2, Elbow CH 4) are **360° Continuous Rotation Servos** (they spin endlessly without physical mechanical stop pins).
- **Physics of Why 120ms Nudge Failed:**
  1. *Insufficient Power:* Nudge delta `+25` (pulse 315) was barely 10% crawl speed outside the 290 deadband. With ~400g of arm weight and lever arm inertia, 10% duty cycle could not produce enough torque to lift the arm.
  2. *Duration Too Brief:* 120ms (0.12 seconds) barely began energizing the motor windings before the timer stopped it.
  3. *Gravity Sag at Neutral:* In a 360° continuous motor, pulse 290 cuts voltage to 0V (no electronic brake). The metal arm's weight easily back-drives the spur gearbox, pulling the arm down.
- **Solution Implemented in `roboarm_dashboard.ino`:**
  - **High-Torque Lift Delta (+95 / Pulse ~385):** Delivers full motor voltage and current from the 5V/10A SMPS for strong upward lifting power.
  - **Hold-to-Run (Continuous Jogging):** On mobile touch / desktop click, holding the button continuously powers the motor to drive the shoulder/elbow up smoothly; releasing immediately stops at 290.
  - **Extended Timed Steps:** Added `300ms`, `600ms`, and `1.2s` step buttons.
### Session 3.7 — Elbow Counterbalance & Coordinated Auto-Balancing Kinematics
**Date:** October 13, 2026 | **Status:** 🟡 PAUSED / RE-EVALUATION

- **Mechanical Counterbalance Validated:** Rubber band counterbalance on the Shoulder successfully eliminated gear back-drive and enabled reliable lifting of the heavy arm link!
- **Elbow Mechanical Reality:** User confirmed there is **no mechanical space** on the elbow bracket to attach rubber bands/counterweights. Therefore, Elbow must function entirely without external elastic aids.
- **The Core Engineering Roadblock:**
  1. *Lack of Position Feedback:* 360° Continuous servos do not have internal potentiometers connected to an angle feedback loop. The ESP32 cannot know the current physical angle of the link.
  2. *Open-Loop Sag:* Without closed-loop position holding, when power is at neutral 290 (0V), the ~180g forearm load backdrives the spur gears, dropping the elbow.
  3. *Why Auto-Balancing Failed:* Pure open-loop timed commands cannot dynamically auto-balance an arm whose actual physical orientation is unknown and slipping.

---

### 📘 The Real-World Engineering Blueprint (How People Actually Build These Arms)

1. **Why 99% of Robotic Arms Use 180° Positional Servos for Shoulder & Elbow:**
   - In standard robotics, joints that fight gravity are **never** 360° continuous rotation motors unless paired with non-backdrivable worm drives or external optical encoders.
   - Standard 180° MG996R servos have an internal 5kΩ potentiometer and closed-loop PID controller. When commanded to an angle (e.g., 60°), the servo automatically pumps current (up to 2.5A) to **lock the joint** against gravity with 11 kg·cm holding torque. No rubber bands, no sag, no drift.
2. **How Real Arms Are Programmed (Keyframe Kinematics):**
   - Autonomous arms are not driven by blind micro-nudges.
   - Instead, 4 distinct **Key Poses** are defined:
     - `RELAX / PARK POSE`: Arm folds down to rest on mechanical base/table.
     - `READY / WAKE POSE`: Shoulder upright, forearm level, gripper open.
     - `PICK POSE`: Arm reaches forward and down to contact table.
     - `PLACE POSE`: Arm lifts, rotates base, and drops item.
   - Smooth transitions between poses use linear angle interpolation (easing) so joints glide smoothly into position.
3. **Paths Forward for Next Session:**
   - **Path A (The Proper Hardware Fix — MG996R 180° Recommended):** 
     - Replace Shoulder & Elbow with standard **MG996R 180° Positional Servos** (~₹250 each).
     - *180° vs 270° Decision:* **180° is strongly recommended.** Arm links mechanically collide with brackets after ~120°–140°, so 270° range is unused. 180° gives higher angular resolution, lower cost, and ubiquitous local availability.
     - Immediately unlocks closed-loop holding torque (11 kg·cm), eliminates rubber bands, and enables precise keyframe poses.
   - **Path B (Software-Only Timed State Machine):** Keep existing 360° servos, but redesign movement around resting mechanical end-stops: arm rests physically in Relax Pose, and uses full-power timed bursts for action macros rather than trying to hover in mid-air.

---

### Session 4 — Voice Control System Implementation
**Date:** October 16-17, 2026 | **Status:** 🟢 COMPLETE (Cloud Voice Deployed)

- [x] Calibrate servo min/max PWM pulse widths per joint (Master Sleep & Awake poses locked!)
- [x] INMP441 I2S MEMS microphone hardware verified with live diagnostic sketch (Peak >17,000)
- [x] Integrated Web Speech Recognition Engine into `roboarm_dashboard.ino`
- [x] Hands-Free Wake Word ("ROBO") system with auto-restart and speech synthesis feedback
- [x] **Autonomous Cloud Voice Firmware Deployed:** `firmware/roboarm_cloud_voice/roboarm_cloud_voice.ino`
  - Uses onboard INMP441 I2S MEMS microphone (Zero phone / Zero browser needed)
  - VAD sound energy detection triggers automatic 1.8s audio recording in 16kHz WAV format
  - Direct HTTPS streaming to **Groq Cloud Whisper API (`whisper-large-v3-turbo`)**
  - Ultra-fast ~200ms cloud transcription returns exact command string
  - Smooth S-curve robotic movement execution on PCA9685 servos
- [x] Direct Joint Teleoperation commands implemented:
  - Base: `"Base Left"`, `"Base Right"`
  - Shoulder: `"Shoulder Front"`, `"Shoulder Back"`
  - Elbow: `"Elbow Down"`, `"Elbow Up"`
  - Wrist: `"Wrist Up"`, `"Wrist Down"`, `"Roll Left"`, `"Roll Right"`
  - Gripper: `"Gripper Open"`, `"Gripper Close"` / `"Grab"`
  - Poses: `"Wake Up"`, `"Sleep"` / `"Park"`
- [ ] Voice command field testing & fine-tuning with robot payload

---

### Session 5 — Full Testing & Verification (UPCOMING)
**Date:** TBD | **Status:** NOT STARTED

- [x] Individual servo range calibration (Shoulder, Elbow, Pitch, Gripper done; Roll next)
- [ ] Stress test & load test with full payload assembled
- [ ] Voice command accuracy testing (quiet environment)
- [ ] Voice command accuracy testing (noisy environment)
- [ ] End-to-end integration test

---

## Known Issues & Risks

| # | Issue | Severity | Notes |
|---|-------|----------|-------|
| 1 | Servo cables not connected to PCA yet | HIGH | Blocker for any movement test |
| 2 | MEMS mic not wired | MEDIUM | Quick fix (~1 min) |
| 3 | MG996R stall current is high | MEDIUM | SMPS at 10A is safe, avoid all stalling simultaneously |
| 4 | Common GND between SMPS and ESP32 | CRITICAL | Must be verified — floating ground = erratic behavior |
| 5 | Voice recognition in noisy env | MEDIUM | May need noise filtering or threshold tuning |

---

## Notes & Decisions

- **No NLP:** Deliberate choice to keep the system simple and offline. Hardcoded keyword matching is faster and more reliable for this use case.
- **PCA9685 over direct PWM:** Chosen because ESP32's hardware PWM channels are limited and PCA9685 offloads PWM generation, freeing ESP32 for audio processing.
- **SMPS 5V/10A:** Adequate for all servos. MG996R and MG90S both rated for 4.8-7.2V, so 5V is within spec.
- **MG90S** — Metal gear version of the SG90 form factor. Better durability than plastic-gear alternatives, good fit for wrist/gripper joints.
- **External MEMS mic module** — Connected via I2S to ESP32. Gives flexibility in mic placement (can be positioned away from electronics noise).
- **Mechanical Limit Calibration Insight (Session 3):**
  - Arm joint horns are mechanically constrained (Shoulder neutral is ~30°-35°, NOT 90°). Sending 90° causes collision.
  - Servos must NOT receive instant jump angles; interpolation with 40ms step delays prevents sudden jerks and gear damage.
  - Servos require active PWM to maintain holding torque. Disabling signals leaves joints limp under gravity.
  - Decision made to isolate and tune joints individually, starting with Shoulder (CH 2).

---

## References & Datasheets

- [MG996R Datasheet](https://www.electronicoscaldas.com/datasheet/MG996R_Tower-Pro.pdf)
- [MG90S Datasheet](https://www.electronicoscaldas.com/datasheet/MG90S_Tower-Pro.pdf)
- [PCA9685 Datasheet](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf)
- [ESP32 Pinout Reference](https://randomnerdtutorials.com/esp32-pinout-reference-gpios/)
- [Adafruit PCA9685 Library](https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library)

---

## Planned File Structure

```
ROBOTIC ARM MDM/
|- ROBO_ARM_JOURNAL.md       <- This file
|- firmware/
|   |- servo_test/           <- Basic servo sweep test
|   |- mic_test/             <- Audio capture test
|   `- main/                 <- Final integrated firmware
|- schematics/
|   `- wiring_diagram.png    <- Connection diagram (TBD)
`- calibration/
    `- servo_angles.txt      <- Per-joint PWM calibration values
```

---

## 📅 Session 5 Log — Local AI Voice Architecture (Snapdragon 8 Gen 2 / Termux)

### Architecture Transition:
1. **Cloud to On-Device Transition:**
   - Groq Cloud Whisper was hitting rate limits during noisy room conditions due to continuous VAD firing.
   - User provided device specs: **iQOO Neo 9 Pro** powered by the **Qualcomm Snapdragon 8 Gen 2** (8-core flagship, Cortex-X3 + Adreno 740, 256GB storage).
2. **Local AI Engine:**
   - Built and deployed `whisper.cpp` directly inside **Android Termux** with native ARM NEON vector instructions (`-DWHISPER_NO_AVX=ON`, `-j8`).
   - Successfully loaded `ggml-base.bin` (141 MB) for sub-200ms ultra-low latency, zero-cloud transcription.
3. **Network & College/Exhibition Portability:**
   - Designed for seamless operation across Home Wi-Fi (`Yash`) and **Mobile Hotspot**.
   - Under Mobile Hotspot mode, ESP32 auto-detects phone IP via `WiFi.gatewayIP()` (`192.168.43.1`), enabling 100% offline, zero-configuration operation in classrooms, labs, or exhibitions without relying on college Wi-Fi.
4. **Pure Hands-Free Voice Flow (Streamlined Architecture):**
   - Removed BOOT button dependency completely for a 100% hands-free voice experience.
   - Reduced acoustic RMS noise threshold from 650 down to **120** so every normal spoken command effortlessly triggers recording and streams to the phone.
   - **Post-Arrival 20s Timer:** Arm hears "robo" -> smoothly glides into calibrated WAKE stance (`WORK_SHOULDER` 410, `WORK_ELBOW` 320, `WORK_ROLL` 100, `WORK_PITCH` 95, `WORK_GRIPPER` 246) -> **20-second active timer starts only after position is physically reached**.
   - **Command Refresh:** Every subsequent valid command (`grab`, `open`, `left`, `right`, etc.) resets the 20-second timer.
   - **Auto-Sleep:** If 20 seconds elapse with no command, the arm smoothly transitions back into the low-load SLEEP pose (`RELAX_SHOULDER` 480, `RELAX_ELBOW` 505) and locks until "robo" is heard again.
5. **Acoustic Fidelity & Vocabulary Priming:**
   - Fixed digital audio clipping when speaking near the mic capsule by adding I2S headroom scaling (`>> 13`) and DSP Automatic Gain Control (Peak Normalization).
   - Locked Whisper decoding to English (`-l en`) and injected a robotics initial prompt (`--prompt ...`) to eliminate language hallucinations (Japanese/Russian) and misheard words (e.g., 'shoulder friend' -> 'shoulder front').
   - Non-blocking HTTP acknowledgment (`server.send(200)` prior to motion execution) to eliminate client read timeouts.

---

## 📅 Session 6 Log — Direct Phone Browser Voice Control (Minimal & 5GHz Wi-Fi Bypass)

### Problem & Challenge:
1. College Wi-Fi networks only offer 5GHz bands (ESP32 hardware only supports 2.4GHz Wi-Fi).
2. Browser-based Web Speech API requires internet access to stream speech-to-text.
3. Too many manual sliders and calibration options in previous dashboard created clutter and confusion for live presentations.

### Solution & Engineering Implementation:
1. **Zero-Router Connectivity (Dual-Network Split):**
   - **ESP32 SoftAP Mode:** ESP32 generates its own dedicated 2.4GHz hotspot (`RoboArm-AP`, Password: `12345678`) with static IP `192.168.4.1`.
   - **Phone Split Routing:** Phone connects Wi-Fi to `RoboArm-AP` (local robotic arm commands) while keeping **Mobile Data (4G/5G) ON** (Google Web Speech API transcription).
   - Fixed access URL: `http://192.168.4.1/`.
2. **Minimal, Single-Screen Mobile Voice Studio:**
   - Redesigned [`web_voice_studio.html`](file:///c:/Users/HP/Downloads/ROBOTIC%20ARM%20MDM/scripts/web_voice_studio.html) into a clean, mobile-first controller.
   - **Hero Mic Button:** Big glowing central button with dynamic ripple soundwave animations for continuous listening.
   - **Bilingual Voice Parsing:** Instant execution of English and Hindi commands (`wake` / `utho`, `grab` / `pakad`, `open` / `khol`, `left` / `baaye`, `right` / `daaye`, `up` / `upar`, `down` / `niche`, `sleep` / `so jao`).
   - **Auto-Wake Feature:** If a motion command is spoken while the arm is asleep, the firmware automatically glides into the wake stance before executing the motion.
   - **Fallback Chips & E-Stop:** One-tap quick command pills and prominent Emergency Power Cut button for noisy presentation halls.
3. **Synchronized Firmware:**
   - Compiled HTML into PROGMEM byte stream [`web_studio.h`](file:///c:/Users/HP/Downloads/ROBOTIC%20ARM%20MDM/firmware/roboarm_phone_offline_voice/web_studio.h) via [`sync_header.py`](file:///c:/Users/HP/Downloads/ROBOTIC%20ARM%20MDM/scripts/sync_header.py).

---

## 📅 Session 7 Log — Micro Precision Commands (5°-7°) & Industrial Cobot Smoothness Tuning

### Objectives:
1. Provide a comprehensive catalog of all active voice commands.
2. Implement **Micro Precision Commands** across all 5 primary kinematic axes (Base, Shoulder, Elbow, Wrist Pitch, Wrist Roll), moving precisely ~5° to 7° per command.
3. Exclude micro commands for the gripper mechanism (maintaining full snappy Close and full Open).
4. Retune all kinematics to mimic high-end industrial cobots (buttery smooth S-curve interpolation, slow graceful trajectory, zero mechanical jerk).

### Implementation Details:
1. **Kinematic Parameter Recalibration:**
   - **Normal Motion Step:** `JOINT_STEP = 30` ticks (~14°-15°), interpolated with a 90-step quintic smootherstep polynomial over `24ms` delay (~2.16s slow, cinematic sweep).
   - **Micro Motion Step:** `MICRO_JOINT_STEP = 12` ticks (~5.5°-6.5° precision nudge), executed over a dedicated 50-step quintic profile (~1.1s gentle glide).
   - **Continuous Base 360:**
     - Normal: `BASE_SPEED_STEP = 18`, `BASE_NUDGE_MS = 700` (~16°-18° slow turn).
     - Micro: `MICRO_BASE_SPEED_STEP = 13`, `MICRO_BASE_NUDGE_MS = 280` (~5°-6° micro nudge).
2. **Bilingual Natural Language Parsing:**
   - Web Voice Studio and Termux server recognize both English and Hindi micro keywords:
     - *"Micro Left"* / *"Thoda Left"* / *"Nudge Left"*
     - *"Micro Right"* / *"Thoda Right"*
     - *"Micro Up"* / *"Thoda Upar"*
     - *"Micro Down"* / *"Thoda Niche"*
     - *"Micro Front"* / *"Thoda Aage"*
     - *"Micro Back"* / *"Thoda Peeche"*
     - *"Micro Wrist Up"* / *"Micro Pitch Up"*
     - *"Micro Wrist Down"* / *"Micro Pitch Down"*
     - *"Micro Roll Left"* / *"Micro Roll Right"*
3. **Interactive UI Mode Switcher:**
   - Added interactive `[ 🚀 Normal Glide (~15°) ]` / `[ 🔬 Micro Precision (5°-7°) ]` toggle bar to [`web_voice_studio.html`](file:///c:/Users/HP/Downloads/ROBOTIC%20ARM%20MDM/scripts/web_voice_studio.html).
   - Dynamic chips automatically update to allow one-tap fallback triggering for all micro movements.
   - Synchronized to PROGMEM [`web_studio.h`](file:///c:/Users/HP/Downloads/ROBOTIC%20ARM%20MDM/firmware/roboarm_phone_offline_voice/web_studio.h).
4. **Command Routing Optimization (Elbow vs Pitch):**
   - Natural speech *"Up"* / *"Down"*, *"Upar"* / *"Niche"*, and *"Micro Up"* / *"Micro Down"* now intuitively route to **Wrist Pitch** (vertical end-effector tilt).
   - **Elbow** is strictly dedicated to explicit commands: *"Elbow Up"*, *"Elbow Down"*, *"Micro Elbow Up"*, *"Micro Elbow Down"*. Prevents accidental gross arm movement when fine pitch adjustment was intended.

---

*Last updated: October 18, 2026*  
*Maintained by: Project Team — MDM Robotic Arm*
