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
//  Hardware Channels & Settings
// ─────────────────────────────────────────
const int BASE_CH     = 0;   // MG996R 360° (Locked at neutral 290)
const int SHOULDER_CH = 2;   // MG996R 180° (Locked at safe calibrated ~300)
const int ELBOW_CH    = 4;   // MG996R 180° Positional (TESTING JOINT)
const int ROLL_CH     = 6;   // MG90S 180° (160)
const int PITCH_CH    = 8;   // MG90S 180° (252)
const int GRIPPER_CH  = 10;  // MG90S 180° (350)

// Safe testing envelope for 180° Servo (Extended lower floor: 60 to 600)
const int MIN_SAFE_PULSE     = 60;
const int MAX_SAFE_PULSE     = 600;
const int DEFAULT_BOOT_PULSE = 300;
const int SHOULDER_HOLD_PULSE = 300; // Calibrated safe mid (180 to 500)

int currentElbowPulse = DEFAULT_BOOT_PULSE;

// ─────────────────────────────────────────
//  WiFi AP Config
// ─────────────────────────────────────────
const char* ssid     = "RoboArm-Elbow-180";
const char* password = "roboarm123";

WebServer server(80);

void lockOtherJoints() {
  // Hold Base 360° at dead-center 290 so it doesn't spin
  pwm.setPWM(BASE_CH, 0, 290);
  
  // Hold Shoulder 180° at safe calibrated center
  pwm.setPWM(SHOULDER_CH, 0, SHOULDER_HOLD_PULSE);
  
  // Cut or keep relaxed the wrist/gripper during elbow calibration
  pwm.setPWM(ROLL_CH, 0, 0);
  pwm.setPWM(PITCH_CH, 0, 0);
  pwm.setPWM(GRIPPER_CH, 0, 0);
}

void setElbowPulse(int p) {
  currentElbowPulse = constrain(p, MIN_SAFE_PULSE, MAX_SAFE_PULSE);
  pwm.setPWM(ELBOW_CH, 0, currentElbowPulse);
  Serial.printf("Elbow 180° Pulse Set & Held at: %d\n", currentElbowPulse);
}

void stepElbow(int delta) {
  setElbowPulse(currentElbowPulse + delta);
}

void cutPower() {
  pwm.setPWM(ELBOW_CH, 0, 0);
  pwm.setPWM(SHOULDER_CH, 0, 0);
  pwm.setPWM(BASE_CH, 0, 0);
  Serial.println("⚠️ All PWM Cut! Motor Relaxed.");
}

// ─────────────────────────────────────────
//  Web UI (180° Positional Elbow Calibrator)
// ─────────────────────────────────────────
const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1.0"/>
<title>Elbow 180° Calibrator | RoboArm</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700;800&display=swap');
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
  h1{font-size:1.35rem;font-weight:800;color:#38bdf8;margin-bottom:4px;}
  .subtitle{font-size:0.8rem;color:#94a3b8;margin-bottom:12px;}
  .badge-row{display:flex;gap:6px;justify-content:center;margin-bottom:14px;flex-wrap:wrap;}
  .badge{
    display:inline-block;padding:4px 10px;
    background:#05966920;border:1px solid #10b98140;
    border-radius:12px;font-size:0.72rem;color:#10b981;
  }
  .guide-box{
    background:#1e1b4b;border:1px solid #4338ca;border-radius:14px;
    padding:12px;margin-bottom:14px;font-size:0.76rem;color:#c7d2fe;line-height:1.45;text-align:left;
  }
  .guide-box b{color:#a5b4fc;}
  .box{
    background:#0f172a;border:1px solid #1e293b;
    border-radius:16px;padding:16px 14px;margin-bottom:14px;text-align:left;
  }
  .box-title{font-size:0.85rem;font-weight:700;color:#38bdf8;margin-bottom:4px;}
  .box-desc{font-size:0.75rem;color:#64748b;margin-bottom:12px;line-height:1.4;}
  .hero-val{
    font-size:2.8rem;font-weight:800;color:#38bdf8;
    text-align:center;margin:6px 0 12px;letter-spacing:1px;
  }
  input[type=range]{
    width:100%;height:10px;border-radius:6px;
    background:#1e293b;outline:none;margin-bottom:14px;cursor:pointer;
  }
  .btn-grid-3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;margin-bottom:10px;}
  .btn{
    padding:12px 6px;font-size:0.85rem;font-weight:700;border:none;
    border-radius:12px;cursor:pointer;transition:all 0.1s ease;
  }
  .btn:active{transform:scale(0.96);}
  .btn-inc{background:#6366f1;color:#fff;}
  .btn-inc:hover{background:#4f46e5;}
  .btn-dec{background:#0284c7;color:#fff;}
  .btn-dec:hover{background:#0369a1;}
  .btn-home{
    width:100%;padding:13px;background:#1e293b;border:1px solid #334155;
    color:#f8fafc;font-size:0.88rem;font-weight:700;border-radius:10px;cursor:pointer;margin-top:6px;
  }
  .btn-home:hover{background:#334155;}
  .preset-grid{display:grid;grid-template-columns:repeat(6,1fr);gap:5px;margin-top:10px;}
  .btn-pre{
    padding:8px 2px;background:#1e293b;border:1px solid #334155;
    color:#cbd5e1;border-radius:8px;font-size:0.75rem;font-weight:700;cursor:pointer;
  }
  .btn-pre:hover{background:#334155;color:#fff;}
  .direct-form{display:flex;gap:8px;margin-top:12px;}
  .direct-input{
    flex:1;background:#1e293b;border:1px solid #334155;border-radius:10px;
    padding:8px 12px;color:#fff;font-size:0.9rem;font-weight:600;text-align:center;
  }
  .btn-set{
    background:#38bdf8;color:#0f172a;border:none;border-radius:10px;
    padding:8px 16px;font-size:0.85rem;font-weight:700;cursor:pointer;
  }
  .btn-cut{
    width:100%;padding:14px;background:#7f1d1d;border:1px solid #ef444455;
    color:#fca5a5;font-size:0.92rem;font-weight:700;border-radius:12px;cursor:pointer;margin-top:4px;
  }
  .btn-cut:active{transform:scale(0.97);}
  .status{margin-top:14px;font-size:0.82rem;color:#10b981;min-height:18px;font-weight:600;}
</style>
</head>
<body>
<div class="card">
  <h1>🦾 ELBOW 180° CALIBRATOR</h1>
  <p class="subtitle">MG996R Positional Servo (Channel 4)</p>
  <div class="badge-row">
    <span class="badge">🔒 Base (CH0) 360° at 290</span>
    <span class="badge">🔒 Shoulder (CH2) 180° Held at 300</span>
  </div>

  <div class="guide-box">
    <b>📋 Calibration Progress:</b><br/>
    • <b>MAX Limit:</b> 505 (Noted!)<br/>
    • <b>MIN Limit Testing:</b> Lower range open down to <b>60</b>.<br/>
    • 100 ke neeche <b>-5 / -1</b> karke dekhein kahan physical stop aata hai.
  </div>

  <!-- ── Current Position Hero & Slider ── -->
  <div class="box">
    <div class="box-title">📍 Live Position / Pulse (60 → 600)</div>
    <div class="hero-val" id="disp">300</div>
    <input type="range" min="60" max="600" value="300" id="slider" oninput="onSlide(this.value)" onchange="sendPulse(this.value)">

    <div class="preset-grid">
      <button class="btn-pre" onclick="sendPulse(70)">70</button>
      <button class="btn-pre" onclick="sendPulse(85)">85</button>
      <button class="btn-pre" onclick="sendPulse(100)">100</button>
      <button class="btn-pre" onclick="sendPulse(200)">200</button>
      <button class="btn-pre" onclick="sendPulse(300)">300 (MID)</button>
      <button class="btn-pre" onclick="sendPulse(505)">505 (MAX)</button>
    </div>

    <div class="direct-form">
      <input type="number" id="numInput" class="direct-input" placeholder="Type Pulse (60-600)" min="60" max="600" value="300"/>
      <button class="btn-set" onclick="sendManual()">GOTO</button>
    </div>
  </div>

  <!-- ── Step Controls ── -->
  <div class="box">
    <div class="box-title">⚡ Precision Step Controls</div>
    <div class="box-desc">
      Slowly find exact limits without sudden shock:
    </div>

    <!-- Decrement Group -->
    <div class="btn-grid-3">
      <button class="btn btn-dec" onclick="step(-1)">-1</button>
      <button class="btn btn-dec" onclick="step(-5)">-5</button>
      <button class="btn btn-dec" onclick="step(-15)">-15</button>
    </div>

    <!-- Increment Group -->
    <div class="btn-grid-3">
      <button class="btn btn-inc" onclick="step(+1)">+1</button>
      <button class="btn btn-inc" onclick="step(+5)">+5</button>
      <button class="btn btn-inc" onclick="step(+15)">+15</button>
    </div>

    <button class="btn-home" onclick="sendPulse(300)">🎯 CENTER / MIDPOINT (300)</button>
  </div>

  <button class="btn-cut" onclick="cutPower()">⚠️ CUT ALL POWER (EMERGENCY PWM 0)</button>
  <div class="status" id="status">Ready. Lower limit expanded down to 60.</div>
</div>

<script>
let currentVal = 300;

function setStatus(msg){document.getElementById('status').innerText = msg;}

function updateUI(val){
  currentVal = parseInt(val);
  document.getElementById('disp').innerText = currentVal;
  document.getElementById('slider').value = currentVal;
  document.getElementById('numInput').value = currentVal;
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

function sendManual(){
  let val = parseInt(document.getElementById('numInput').value);
  if(isNaN(val)) return;
  if(val < 60) val = 60;
  if(val > 600) val = 600;
  sendPulse(val);
}

function step(delta){
  let target = currentVal + delta;
  if(target < 60) target = 60;
  if(target > 600) target = 600;
  sendPulse(target);
}

function cutPower(){
  fetch('/cut').then(() => setStatus('⚠️ All PWM Cut! Motor Relaxed.'));
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
    setElbowPulse(p);
    server.send(200, "text/plain", "Locked & Holding at: " + String(currentElbowPulse));
  } else {
    server.send(400, "text/plain", "Missing pulse");
  }
}

void handleStep() {
  if (server.hasArg("delta")) {
    int d = server.arg("delta").toInt();
    stepElbow(d);
    server.send(200, "text/plain", "Stepped to: " + String(currentElbowPulse));
  } else {
    server.send(400, "text/plain", "Missing delta");
  }
}

void handleCut() {
  cutPower();
  server.send(200, "text/plain", "All Power Cut");
}

// ─────────────────────────────────────────
//  Setup & Loop
// ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Elbow 180° Positional Calibrator (CH 4) ===");

  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(SERVO_FREQ);
  delay(50);

  // Lock base 360 at 290 and Shoulder 180 at safe center 300
  lockOtherJoints();
  Serial.println("Base (CH0) locked at 290. Shoulder (CH2) locked at 300.");

  // Prime Elbow to 300 neutral midpoint
  setElbowPulse(currentElbowPulse);
  Serial.printf("Elbow (CH4) primed & held at: %d\n", currentElbowPulse);

  WiFi.softAP(ssid, password);
  Serial.printf("WiFi AP: %s  |  http://%s\n", ssid, WiFi.softAPIP().toString().c_str());

  server.on("/",     handleRoot);
  server.on("/set",  handleSet);
  server.on("/step", handleStep);
  server.on("/cut",  handleCut);
  server.begin();

  Serial.println("Web Server Ready! Connect to RoboArm-Elbow-180 and open 192.168.4.1");
}

void loop() {
  server.handleClient();
}
