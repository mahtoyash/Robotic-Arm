# Contributing to Voice-Controlled Robotic Arm

Thank you for your interest in contributing to the **Voice-Controlled Robotic Arm** project. Whether you are fixing a bug, adding new features, improving kinematics/calibration algorithms, or enhancing documentation, your help is appreciated.

Please review these guidelines before submitting a contribution.

---

## Code of Conduct

This project and everyone participating in it is governed by our [Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this code. Please report unacceptable behavior at our [GitHub Issues](https://github.com/mahtoyash/Robotic-Arm/issues).

---

## Development Setup

### 1. Embedded / Firmware Setup (ESP32)
1. Install the [Arduino IDE](https://www.arduino.cc/en/software) (2.0+ recommended) or [PlatformIO](https://platformio.org/).
2. Add the ESP32 Board URL to your Arduino IDE Preferences:
   ```text
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Install the **esp32** board package via the Boards Manager.
4. Install the required libraries via Arduino Library Manager:
   - `Adafruit PWM Servo Driver Library` (by Adafruit)
   - `WiFi` and `WebServer` (included in ESP32 core)
   - `HTTPClient` and `WiFiClientSecure` (included in ESP32 core)

### 2. Python Voice Server Setup (PC / Android Termux)
1. Ensure Python 3.8+ is installed:
   ```bash
   python --version
   ```
2. Install the necessary dependencies:
   ```bash
   pip install flask requests SpeechRecognition
   ```
   *(Optional for local audio input on PC: `pip install pyaudio` or `pip install sounddevice`)*

---

## How to Contribute

### 1. Reporting Bugs
- Check the [Issues Tab](https://github.com/mahtoyash/Robotic-Arm/issues) first to ensure the bug hasn't already been reported.
- If it hasn't, open a new issue using the **Bug Report** template.
- Provide a clear, descriptive title, the microcontroller/hardware model you are using, steps to reproduce, and any serial log outputs.

### 2. Suggesting Enhancements
- Open a feature request issue describing the feature, why it is useful, and potential implementation approaches (e.g., Inverse Kinematics, ROS2 integration, Bluetooth LE support).

### 3. Submitting Code Changes (Pull Requests)

1. **Fork the Repository** on GitHub.
2. **Clone your fork** locally:
   ```bash
   git clone https://github.com/<your-username>/Robotic-Arm.git
   cd Robotic-Arm
   ```
3. **Create a descriptive feature branch**:
   ```bash
   git checkout -b feature/your-feature-name
   ```
4. **Make your changes**:
   - Keep code clean and well-commented.
   - For firmware, ensure servo pulse bounds adhere to hardware safety envelopes.
   - Do **NOT** commit hardcoded Wi-Fi passwords, private API keys, or machine-specific tokens.
5. **Test your changes**:
   - Verify that your sketch compiles cleanly for ESP32.
   - Test Python scripts with `python -m py_compile scripts/arm_server.py`.
6. **Commit your changes**:
   ```bash
   git commit -m "feat: add smooth easing trajectory for shoulder servo"
   ```
   *(We recommend following Conventional Commits format: `feat:`, `fix:`, `docs:`, `refactor:`, `chore:`).*
7. **Push to your fork**:
   ```bash
   git push origin feature/your-feature-name
   ```
8. **Open a Pull Request** against the `main` branch of `mahtoyash/Robotic-Arm`.

---

## Coding Guidelines

### C++ / Arduino Firmware
- Group pin definitions and hardware parameters at the top of the file.
- Always implement bounds checking (`min()` / `max()` or clamping) on servo pulse widths before sending signals to the PCA9685 to prevent physical motor stalls or gear damage.
- Keep blocking loops (`delay()`) out of core web/network loops where possible.

### Python Scripts
- Follow standard [PEP 8](https://peps.python.org/pep-0008/) conventions.
- Use explicit exception handling for socket/network communications with the ESP32.

---

## Community and Questions

Have questions or need help troubleshooting hardware/firmware?
- Open a discussion or question issue in the [GitHub Issues](https://github.com/mahtoyash/Robotic-Arm/issues).
- Check the [ROBO_ARM_JOURNAL.md](ROBO_ARM_JOURNAL.md) for full hardware logs, electrical power budgets, and wiring schematics.
