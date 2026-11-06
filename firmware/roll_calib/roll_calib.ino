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
//  Joint Channels & Calibrated Locks
// ─────────────────────────────────────────
const int SHOULDER_CH = 2;  // MG996R — Locked at 290
const int ELBOW_CH    = 4;  // MG996R — Locked at 290
const int ROLL_CH     = 6;  // MG90S  — Wrist Roll on CH6
const int PITCH_CH    = 8;  // MG90S  — Locked at Calibrated Home 252
const int GRIPPER_CH  = 10; // MG90S  — Locked at Calibrated Safe 350

const int ARM_LOCK_PULSE     = 290;
const int PITCH_LOCK_PULSE   = 252;
const int GRIPPER_LOCK_PULSE = 350;

// CALIBRATED BOOT/START POSITION: 160
int currentRollPulse = 160;

// ─────────────────────────────────────────
//  WiFi AP Config
// ─────────────────────────────────────────
const char* ssid     = "RoboArm-Roll";
const char* password = "roboarm123";

WebServer server(80);

void lockOtherJoints() {
  pwm.setPWM(SHOULDER_CH, 0, ARM_LOCK_PULSE);
  pwm.setPWM(ELBOW_CH,    0, ARM_LOCK_PULSE);
  pwm.setPWM(PITCH_CH,    0, PITCH_LOCK_PULSE);
  pwm.setPWM(GRIPPER_CH,  0, GRIPPER_LOCK_PULSE);
}

void setRollPulse(int p) {
  // Calibrated limits: 100 (Min CCW) to 472 (Max CW)
  currentRollPulse = constrain(p, 100, 472);
  pwm.setPWM(ROLL_CH, 0, currentRollPulse);
  Serial.printf("Roll Pulse Set & Held: %d\n", currentRollPulse);
}

void stepRoll(int delta) {
  setRollPulse(currentRollPulse + delta);
}

void cutAllPower() {
  for (int ch = 0; ch < 16; ch++) {
    pwm.setPWM(ch, 0, 0);
  }
  Serial.println("All Power Cut!");
}

// ─────────────────────────────────────────
//  Web UI (Calibrated Wrist Roll Controller)
// ─────────────────────────────────────────
const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1.0"/>
<title>Wrist Roll Calibrated Controller (CH 6)</title>
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
  h1{font-size:1.35rem;font-weight:700;color:#10b981;margin-bottom:4px;}
  .subtitle{font-size:0.8rem;color:#94a3b8;margin-bottom:12px;}
  .lock-badge{
    display:inline-block;padding:4px 12px;
    background:#05966920;border:1px solid #10b98140;
    border-radius:12px;font-size:0.75rem;color:#10b981;margin-bottom:16px;line-height:1.4;
  }

  .box{
    background:#0f172a;border:1px solid #1e293b;
    border-radius:16px;padding:16px 14px;margin-bottom:14px;text-align:left;
  }
  .box-title{font-size:0.85rem;font-weight:700;color:#10b981;margin-bottom:4px;}
  .box-desc{font-size:0.75rem;color:#64748b;margin-bottom:12px;line-height:1.4;}

  .hero-val{font-size:2.5rem;font-weight:700;color:#10b981;text-align:center;margin:4px 0 12px;}
  input[type=range]{width:100%;height:8px;border-radius:4px;background:#1e293b;outline:none;margin-bottom:14px;}

  .btn-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:10px;}
  .btn{
    padding:14px 8px;font-size:0.88rem;font-weight:700;border:none;
    border-radius:12px;cursor:pointer;transition:all 0.1s ease;
  }
  .btn:active{transform:scale(0.96);}

  /* CW ROLL BUTTONS */
  .btn-cw-lg{background:#059669;color:#fff;}
  .btn-cw-md{background:#10b981;color:#fff;}
  .btn-cw-sm{background:#34d399;color:#064e3b;font-size:0.8rem;padding:10px;}

  /* CCW ROLL BUTTONS */
  .btn-ccw-lg{background:#0284c7;color:#fff;}
  .btn-ccw-md{background:#0ea5e9;color:#fff;}
  .btn-ccw-sm{background:#38bdf8;color:#0c4a6e;font-size:0.8rem;padding:10px;}

  .btn-home{
    width:100%;padding:13px;background:#1e293b;border:1px solid #334155;
    color:#f8fafc;font-size:0.88rem;font-weight:700;border-radius:10px;cursor:pointer;margin-top:6px;
  }

  .preset-grid{display:grid;grid-template-columns:repeat(5,1fr);gap:6px;margin-top:10px;}
  .btn-pre{
    padding:8px 2px;background:#1e293b;border:1px solid #334155;
    color:#cbd5e1;border-radius:8px;font-size:0.75rem;font-weight:700;cursor:pointer;
  }

  .btn-cut{
    width:100%;padding:14px;background:#7f1d1d;border:1px solid #ef444455;
    color:#fca5a5;font-size:0.92rem;font-weight:700;border-radius:12px;cursor:pointer;margin-top:4px;
  }
  .status{margin-top:14px;font-size:0.82rem;color:#10b981;min-height:18px;font-weight:600;}
  .summary-badge{
    background:#064e3b25;border:1px solid #05966950;border-radius:12px;
    padding:10px;margin-bottom:12px;font-size:0.78rem;color:#6ee7b7;line-height:1.4;text-align:center;
  }
</style>
</head>
<body>
<div class="card">
  <h1>🔄 WRIST ROLL (CH 6)</h1>
  <p class="subtitle">Fully Calibrated Range: 100 → 472</p>
  <span class="lock-badge">🔒 Locked: Shoulder(290), Elbow(290), Pitch(252), Gripper(350)</span>

  <div class="summary-badge">
    <b>Calibrated Limits:</b><br/>
    MIN CCW = <b>100</b> &nbsp;|&nbsp; BOOT / HOME = <b>160</b> &nbsp;|&nbsp; MAX CW = <b>472</b>
  </div>

  <!-- ── Current Position Hero & Slider ── -->
  <div class="box">
    <div class="box-title">📍 Live Position / Pulse (100 → 472)</div>
    <div class="hero-val" id="disp">160</div>
    <input type="range" min="100" max="472" value="160" id="slider" oninput="onSlide(this.value)" onchange="sendPulse(this.value)">

    <div class="preset-grid">
      <button class="btn-pre" onclick="sendPulse(100)">100 (Min)</button>
      <button class="btn-pre" onclick="sendPulse(160)">160 (HOME)</button>
      <button class="btn-pre" onclick="sendPulse(250)">250</button>
      <button class="btn-pre" onclick="sendPulse(360)">360</button>
      <button class="btn-pre" onclick="sendPulse(472)">472 (Max)</button>
    </div>
  </div>

  <!-- ── Step Adjustments ── -->
  <div class="box">
    <div class="box-title">⚡ Step & Hold Adjustments</div>
    <div class="box-desc">
      Roll CW ya CCW step karke angle adjust karo:
    </div>

    <!-- Medium Steps -->
    <div class="btn-grid">
      <button class="btn btn-cw-md" onclick="step(+30)">🔄 ROLL CW (+30)</button>
      <button class="btn btn-ccw-md" onclick="step(-30)">🔄 ROLL CCW (-30)</button>
    </div>

    <!-- Micro Steps -->
    <div class="btn-grid">
      <button class="btn btn-cw-sm" onclick="step(+10)">Micro CW (+10)</button>
      <button class="btn btn-ccw-sm" onclick="step(-10)">Micro CCW (-10)</button>
    </div>

    <!-- Big Steps -->
    <div class="btn-grid">
      <button class="btn btn-cw-lg" onclick="step(+60)">🚀 Big CW (+60)</button>
      <button class="btn btn-ccw-lg" onclick="step(-60)">🚀 Big CCW (-60)</button>
    </div>

    <button class="btn-home" onclick="sendPulse(160)">🏠 RETURN HOME (160)</button>
  </div>

  <button class="btn-cut" onclick="cutPower()">⚠️ CUT ALL POWER (PWM 0)</button>
  <div class="status" id="status">Ready. Initialized at Boot Position 160!</div>
</div>

<script>
let currentVal = 160;

function setStatus(msg){document.getElementById('status').innerText = msg;}

function updateUI(val){
  currentVal = parseInt(val);
  document.getElementById('disp').innerText = currentVal;
  document.getElementById('slider').value = currentVal;
}

function onSlide(v){
  document.getElementById('disp').innerText = v;
}

function sendPulse(p){
  updateUI(p);
  setStatus('Holding pulse: ' + p);
  fetch('/set?pulse=' + p)
    .then(r => r.text())
    .then(m => setStatus(m))
    .catch(() => setStatus('Connection Error!'));
}

function step(delta){
  let target = currentVal + delta;
  if(target < 100) target = 100;
  if(target > 472) target = 472;
  sendPulse(target);
}

function cutPower(){
  fetch('/cut').then(() => setStatus('All PWM Cut! All Servos Relaxed.'));
}
</script>
</body>
</html>
)rawliteral";

// ─────────────────────────────────────────
//  HTTP Handlers
// ─────────────────────────────────────────
void handleRoot() { server.send_P(200, "text/html", HTML); }

void handleSet() {
  if (server.hasArg("pulse")) {
    int p = server.arg("pulse").toInt();
    setRollPulse(p);
    server.send(200, "text/plain", "Roll Set & Held at: " + String(currentRollPulse));
  } else {
    server.send(400, "text/plain", "Missing pulse");
  }
}

void handleStep() {
  if (server.hasArg("delta")) {
    int d = server.arg("delta").toInt();
    stepRoll(d);
    server.send(200, "text/plain", "Roll Stepped to: " + String(currentRollPulse));
  } else {
    server.send(400, "text/plain", "Missing delta");
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
  Serial.println("\n=== Wrist Roll Calibrator (CH 6) ===");

  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(SERVO_FREQ);
  delay(50);

  // Lock heavy joints & calibrated wrist pitch / gripper
  lockOtherJoints();
  Serial.println("Shoulder (CH2) & Elbow (CH4) locked at 290");
  Serial.println("Pitch (CH8) locked at 252 | Gripper (CH10) locked at 350");

  // Prime Roll to requested Boot/Home position (160)
  setRollPulse(currentRollPulse);
  Serial.printf("Wrist Roll (CH6) initialized at Boot Position: %d\n", currentRollPulse);

  WiFi.softAP(ssid, password);
  Serial.printf("WiFi: %s  |  http://%s\n", ssid, WiFi.softAPIP().toString().c_str());

  server.on("/",     handleRoot);
  server.on("/set",  handleSet);
  server.on("/step", handleStep);
  server.on("/cut",  handleCut);
  server.begin();

  Serial.println("Wrist Roll Ready! Boot Position 160 Active.");
}

void loop() {
  server.handleClient();
}
