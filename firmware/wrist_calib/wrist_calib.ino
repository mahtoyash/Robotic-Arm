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
//  Joint Channels & Calibrated Defaults
// ─────────────────────────────────────────
const int SHOULDER_CH = 2;  // MG996R — Locked at 290
const int ELBOW_CH    = 4;  // MG996R — Locked at 290
const int WRIST_CH    = 8;  // MG90S  — Wrist Pitch on CH8

const int ARM_LOCK_PULSE = 290;

// CALIBRATED BASE/HOME VALUE: 252 | UPPER LIMIT: 460
int currentWristPulse = 252;

// ─────────────────────────────────────────
//  WiFi AP Config
// ─────────────────────────────────────────
const char* ssid     = "RoboArm-Wrist";
const char* password = "roboarm123";

WebServer server(80);

void lockArmJoints() {
  pwm.setPWM(SHOULDER_CH, 0, ARM_LOCK_PULSE);
  pwm.setPWM(ELBOW_CH, 0, ARM_LOCK_PULSE);
}

void setWristPulse(int p) {
  // Final Calibrated Safe Limits: 95 (Full Down) to 460 (Full Up)
  currentWristPulse = constrain(p, 95, 460);
  pwm.setPWM(WRIST_CH, 0, currentWristPulse);
  Serial.printf("Wrist Pulse Set & Held at: %d\n", currentWristPulse);
}

void stepWrist(int delta) {
  setWristPulse(currentWristPulse + delta);
}

void cutAllPower() {
  for (int ch = 0; ch < 16; ch++) {
    pwm.setPWM(ch, 0, 0);
  }
  Serial.println("All PWM Cut!");
}

// ─────────────────────────────────────────
//  Web UI (Deep-Down Range Explorer)
// ─────────────────────────────────────────
const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1.0"/>
<title>Wrist Pitch Lower Limit Finder (CH 8)</title>
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
  h1{font-size:1.35rem;font-weight:700;color:#38bdf8;margin-bottom:4px;}
  .subtitle{font-size:0.8rem;color:#94a3b8;margin-bottom:12px;}
  .lock-badge{
    display:inline-block;padding:4px 12px;
    background:#05966920;border:1px solid #10b98140;
    border-radius:12px;font-size:0.75rem;color:#10b981;margin-bottom:16px;
  }

  .box{
    background:#0f172a;border:1px solid #1e293b;
    border-radius:16px;padding:16px 14px;margin-bottom:14px;text-align:left;
  }
  .box-title{font-size:0.85rem;font-weight:700;color:#38bdf8;margin-bottom:4px;}
  .box-desc{font-size:0.75rem;color:#64748b;margin-bottom:12px;line-height:1.4;}

  .hero-val{font-size:2.5rem;font-weight:700;color:#38bdf8;text-align:center;margin:4px 0 12px;}
  input[type=range]{width:100%;height:8px;border-radius:4px;background:#1e293b;outline:none;margin-bottom:14px;}

  .btn-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:10px;}
  .btn{
    padding:14px 8px;font-size:0.88rem;font-weight:700;border:none;
    border-radius:12px;cursor:pointer;transition:all 0.1s ease;
  }
  .btn:active{transform:scale(0.96);}

  /* UP BUTTONS */
  .btn-up-lg{background:#8b5cf6;color:#fff;}
  .btn-up-md{background:#6366f1;color:#fff;}
  .btn-up-sm{background:#4f46e5;color:#e0e7ff;font-size:0.8rem;padding:10px;}

  /* DOWN BUTTONS */
  .btn-dn-lg{background:#0284c7;color:#fff;}
  .btn-dn-md{background:#0ea5e9;color:#fff;}
  .btn-dn-sm{background:#38bdf8;color:#0f172a;font-size:0.8rem;padding:10px;}

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
  .limits-box{
    background:#1e1b4b;border:1px solid #4338ca;border-radius:12px;
    padding:10px;margin-bottom:12px;font-size:0.75rem;color:#c7d2fe;line-height:1.4;text-align:center;
  }
</style>
</head>
<body>
<div class="card">
  <h1>🦾 WRIST PITCH (CH 8)</h1>
  <p class="subtitle">Fully Calibrated Range: 95 → 460</p>
  <span class="lock-badge">🔒 Shoulder (CH2) & Elbow (CH4) Locked at 290</span>

  <div class="limits-box">
    <b>Wrist Pitch Final Calibration:</b><br/>
    MIN DOWN = <b>95</b> &nbsp;|&nbsp; HOME = <b>252</b> &nbsp;|&nbsp; MAX UP = <b>460</b>
  </div>

  <!-- ── Current Position Hero & Slider ── -->
  <div class="box">
    <div class="box-title">📍 Live Position / Pulse (95 → 460)</div>
    <div class="hero-val" id="disp">252</div>
    <input type="range" min="95" max="460" value="252" id="slider" oninput="onSlide(this.value)" onchange="sendPulse(this.value)">

    <div class="preset-grid">
      <button class="btn-pre" onclick="sendPulse(95)">95 (Full DN)</button>
      <button class="btn-pre" onclick="sendPulse(170)">170 (Med DN)</button>
      <button class="btn-pre" onclick="sendPulse(252)">252 (HOME)</button>
      <button class="btn-pre" onclick="sendPulse(350)">350 (Med UP)</button>
      <button class="btn-pre" onclick="sendPulse(460)">460 (Full UP)</button>
    </div>
  </div>

  <!-- ── Step & Hold Buttons ── -->
  <div class="box">
    <div class="box-title">⚡ Step Down Testing (Find Where It Stops)</div>
    <div class="box-desc">
      Micro DOWN (-5 / -10) se dekho exact kis number par jaakar full down hota hai:
    </div>

    <!-- Micro Steps for Precision Testing -->
    <div class="btn-grid">
      <button class="btn btn-dn-sm" onclick="step(-5)">⬇ Micro DN (-5)</button>
      <button class="btn btn-dn-sm" onclick="step(-10)">⬇ Step DN (-10)</button>
    </div>

    <div class="btn-grid">
      <button class="btn btn-up-sm" onclick="step(+5)">⬆ Micro UP (+5)</button>
      <button class="btn btn-up-sm" onclick="step(+10)">⬆ Step UP (+10)</button>
    </div>

    <button class="btn-home" onclick="sendPulse(252)">🏠 RETURN HOME (252)</button>
  </div>

  <button class="btn-cut" onclick="cutPower()">⚠️ CUT ALL POWER (PWM 0)</button>
  <div class="status" id="status">Ready. Lower limit range 50 - 100 open!</div>
</div>

<script>
let currentVal = 252;

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
  if(target < 95) target = 95;
  if(target > 460) target = 460;
  sendPulse(target);
}

function cutPower(){
  fetch('/cut').then(() => setStatus('All PWM Cut! Motor Relaxed.'));
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
    setWristPulse(p);
    server.send(200, "text/plain", "Locked & Holding at: " + String(currentWristPulse));
  } else {
    server.send(400, "text/plain", "Missing pulse");
  }
}

void handleStep() {
  if (server.hasArg("delta")) {
    int d = server.arg("delta").toInt();
    stepWrist(d);
    server.send(200, "text/plain", "Stepped to: " + String(currentWristPulse));
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
  Serial.println("\n=== Wrist Pitch Lower Limit Finder (CH 8) ===");

  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(SERVO_FREQ);
  delay(50);

  // Lock heavy arm joints
  lockArmJoints();
  Serial.println("Shoulder (CH2) & Elbow (CH4) locked at 290");

  // Prime wrist to calibrated position (252)
  setWristPulse(currentWristPulse);
  Serial.printf("Wrist (CH8) primed & held at: %d\n", currentWristPulse);

  // Silence all other channels
  for (int ch = 0; ch < 16; ch++) {
    if (ch != SHOULDER_CH && ch != ELBOW_CH && ch != WRIST_CH) {
      pwm.setPWM(ch, 0, 0);
    }
  }

  WiFi.softAP(ssid, password);
  Serial.printf("WiFi: %s  |  http://%s\n", ssid, WiFi.softAPIP().toString().c_str());

  server.on("/",     handleRoot);
  server.on("/set",  handleSet);
  server.on("/step", handleStep);
  server.on("/cut",  handleCut);
  server.begin();

  Serial.println("Wrist Pitch Ready! Range 50 - 460 Open.");
}

void loop() {
  server.handleClient();
}
