#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <WiFi.h>
#include <WebServer.h>

// ─────────────────────────────────────────
//  PCA9685 Config
// ─────────────────────────────────────────
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
#define SERVO_FREQ 50

// ─────────────────────────────────────────
//  Joint Channels & Calibrated Settings
// ─────────────────────────────────────────
const int SHOULDER_CH   = 2;  // Locked at 290
const int ELBOW_CH      = 4;  // Locked at 290
const int GRIPPER_CH    = 10; // MG90S Gripper Joint

const int ARM_LOCK_PULSE = 290;

// Gripper Positional Calibration Points
int gripperClosePulse = 310; // Calibrated CLOSE position (~300-310)
int gripperOpenPulse  = 430; // Calibrated OPEN position (~420-440 safe, below 484)
int currentGripperPos = 310;

// ─────────────────────────────────────────
//  WiFi AP Config
// ─────────────────────────────────────────
const char* ssid     = "RoboArm-Gripper";
const char* password = "roboarm123";

WebServer server(80);

void lockArmJoints() {
  pwm.setPWM(SHOULDER_CH, 0, ARM_LOCK_PULSE);
  pwm.setPWM(ELBOW_CH, 0, ARM_LOCK_PULSE);
}

void setGripperPulse(int p) {
  p = constrain(p, 140, 620);
  currentGripperPos = p;
  pwm.setPWM(GRIPPER_CH, 0, currentGripperPos);
  Serial.printf("Gripper Position Set: %d\n", currentGripperPos);
}

void cutAllPower() {
  for (int ch = 0; ch < 16; ch++) {
    pwm.setPWM(ch, 0, 0);
  }
}

// ─────────────────────────────────────────
//  Web UI (Gripper Calibrated Controller)
// ─────────────────────────────────────────
const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1.0"/>
<title>Gripper Calibrated Controller</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&display=swap');
  *{margin:0;padding:0;box-sizing:border-box;}
  body{
    font-family:'Inter',sans-serif;
    background:#0a0b10;
    color:#e5e7eb;
    min-height:100vh;
    display:flex;
    flex-direction:column;
    align-items:center;
    padding:16px;
  }
  .card{
    background:#12141e;
    border:1px solid #1f2335;
    border-radius:24px;
    padding:24px 20px;
    width:100%;
    max-width:440px;
    box-shadow:0 12px 40px rgba(0,0,0,0.5);
    text-align:center;
  }
  h1{
    font-size:1.4rem;
    font-weight:700;
    color:#38bdf8;
    margin-bottom:4px;
  }
  .subtitle{
    font-size:0.8rem;
    color:#94a3b8;
    margin-bottom:14px;
  }
  .lock-badge{
    display:inline-block;
    padding:4px 12px;
    background:#05966920;
    border:1px solid #10b98140;
    border-radius:12px;
    font-size:0.75rem;
    color:#10b981;
    margin-bottom:18px;
  }
  .state-box{
    background:#0f172a;
    border:2px solid #0284c755;
    border-radius:18px;
    padding:16px;
    margin-bottom:20px;
  }
  .state-label{
    font-size:0.8rem;
    color:#94a3b8;
    text-transform:uppercase;
    letter-spacing:1px;
    margin-bottom:4px;
  }
  .state-val{
    font-size:2.6rem;
    font-weight:700;
    color:#38bdf8;
    line-height:1;
    margin:6px 0;
  }
  .state-text{
    font-size:0.95rem;
    font-weight:600;
    color:#cbd5e1;
  }
  .main-actions{
    display:grid;
    grid-template-columns:1fr 1fr;
    gap:12px;
    margin-bottom:14px;
  }
  .btn-action{
    padding:20px 10px;
    font-size:1.05rem;
    font-weight:700;
    border:none;
    border-radius:16px;
    cursor:pointer;
    transition:all 0.1s ease;
    display:flex;
    flex-direction:column;
    align-items:center;
    justify-content:center;
    gap:6px;
  }
  .btn-action:active{transform:scale(0.96);}
  .btn-open{
    background:#0284c7;
    color:#ffffff;
    box-shadow:0 4px 20px #0284c740;
  }
  .btn-close{
    background:#334155;
    color:#f1f5f9;
  }
  .btn-sub{
    font-size:0.75rem;
    opacity:0.8;
    font-weight:400;
  }
  .half-btn{
    width:100%;
    padding:12px;
    background:#1e293b;
    border:1px solid #334155;
    color:#cbd5e1;
    border-radius:12px;
    font-size:0.85rem;
    font-weight:600;
    cursor:pointer;
    margin-bottom:16px;
  }
  .slider-section{
    background:#0f172a;
    border:1px solid #1e293b;
    border-radius:14px;
    padding:12px;
    margin-bottom:16px;
    text-align:left;
  }
  .slider-label{
    font-size:0.75rem;
    color:#94a3b8;
    margin-bottom:6px;
    display:flex;
    justify-content:space-between;
  }
  input[type=range]{
    width:100%;
    height:8px;
    border-radius:4px;
    background:#1e293b;
    outline:none;
  }
  .btn-cut{
    width:100%;
    padding:14px;
    background:#7f1d1d;
    border:1px solid #ef444455;
    color:#fca5a5;
    font-size:0.92rem;
    font-weight:700;
    border-radius:12px;
    cursor:pointer;
  }
  .status{
    margin-top:14px;
    font-size:0.82rem;
    color:#10b981;
    min-height:18px;
  }
</style>
</head>
<body>

<div class="card">
  <h1>🦾 GRIPPER CONTROLLER</h1>
  <p class="subtitle">Direct Calibrated Positions (MG90S)</p>
  <span class="lock-badge">🔒 Shoulder & Elbow Locked at 290</span>

  <!-- Live State Display -->
  <div class="state-box">
    <div class="state-label">Current Pulse</div>
    <div class="state-val" id="valDisp">310</div>
    <div class="state-text" id="stateDesc">Gripper Closed (310)</div>
  </div>

  <!-- Direct 1-Click Open/Close -->
  <div class="main-actions">
    <button class="btn-action btn-open" onclick="goTo(484, 'Gripper OPEN')">
      <span>👐 OPEN</span>
      <span class="btn-sub">Pulse 484 (Confirmed!)</span>
    </button>
    <button class="btn-action btn-close" onclick="goTo(310, 'Gripper CLOSED')">
      <span>✊ CLOSE</span>
      <span class="btn-sub">Pulse 310</span>
    </button>
  </div>

  <div class="main-actions">
    <button class="btn-action btn-open" style="background:#7c3aed;" onclick="goTo(550, 'Gripper Max OPEN')">
      <span>💪 MAX OPEN</span>
      <span class="btn-sub">Pulse 550</span>
    </button>
    <button class="btn-action btn-close" style="background:#0f766e;" onclick="goTo(200, 'Gripper Max CLOSE')">
      <span>🔒 MAX CLOSE</span>
      <span class="btn-sub">Pulse 200</span>
    </button>
  </div>

  <!-- Fine Slider -->
  <div class="slider-section">
    <div class="slider-label">
      <span>Full Range Slider (150 → 600)</span>
      <span id="sliderVal">310</span>
    </div>
    <input type="range" min="150" max="600" value="310" id="slider" oninput="onSlide(this.value)" onchange="goTo(this.value, 'Custom Position')">
  </div>

  <button class="btn-cut" onclick="cutPower()">⚠️ CUT ALL POWER</button>

  <div class="status" id="status">Ready. Tap OPEN or CLOSE!</div>
</div>

<script>
function setStatus(msg) {
  document.getElementById('status').innerText = msg;
}

function onSlide(v) {
  document.getElementById('sliderVal').innerText = v;
  document.getElementById('valDisp').innerText = v;
}

function goTo(pulse, label) {
  pulse = parseInt(pulse);
  document.getElementById('valDisp').innerText = pulse;
  document.getElementById('sliderVal').innerText = pulse;
  document.getElementById('slider').value = pulse;
  document.getElementById('stateDesc').innerText = label + ' (' + pulse + ')';
  setStatus('Moving to ' + pulse + '...');

  fetch('/set?pulse=' + pulse)
    .then(r => r.text())
    .then(m => setStatus(m))
    .catch(() => setStatus('Connection Error!'));
}

function cutPower() {
  fetch('/cut').then(() => {
    setStatus('All PWM Cut!');
    document.getElementById('stateDesc').innerText = 'Power Cut (PWM 0)';
  });
}
</script>

</body>
</html>
)rawliteral";

// ─────────────────────────────────────────
//  HTTP Handlers
// ─────────────────────────────────────────
void handleRoot() {
  server.send_P(200, "text/html", HTML);
}

void handleSet() {
  if (server.hasArg("pulse")) {
    int p = server.arg("pulse").toInt();
    setGripperPulse(p);
    server.send(200, "text/plain", "Gripper held at " + String(currentGripperPos));
  } else {
    server.send(400, "text/plain", "Missing pulse");
  }
}

void handleCut() {
  cutAllPower();
  server.send(200, "text/plain", "All Power Cut");
}

// ─────────────────────────────────────────
//  Setup & Loop
// ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Gripper Calibrated Controller (CH 10) ===");

  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(SERVO_FREQ);
  delay(50);

  // Lock arm joints
  lockArmJoints();
  Serial.println("Shoulder (CH2) & Elbow (CH4) locked at 290");

  // Initialize Gripper at Close position (310)
  setGripperPulse(gripperClosePulse);
  Serial.println("Gripper initialized to Close (310)");

  // Turn off remaining channels
  for (int ch = 0; ch < 16; ch++) {
    if (ch != SHOULDER_CH && ch != ELBOW_CH && ch != GRIPPER_CH) {
      pwm.setPWM(ch, 0, 0);
    }
  }

  // Start WiFi AP
  WiFi.softAP(ssid, password);
  Serial.printf("WiFi AP: %s\n", ssid);
  Serial.printf("Control URL: http://%s\n", WiFi.softAPIP().toString().c_str());

  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/cut", handleCut);
  server.begin();

  Serial.println("Gripper Controller Ready!");
}

void loop() {
  server.handleClient();
}
