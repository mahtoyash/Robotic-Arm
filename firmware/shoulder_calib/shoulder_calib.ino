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
//  Hardware Channels
// ─────────────────────────────────────────
const int SHOULDER_CH = 2; // Testing joint: MG996R Positional Servo

// Full raw pulse range for testing (standard analog/digital servo safe bounds)
const int MIN_RAW_PULSE = 100;
const int MAX_RAW_PULSE = 650;
const int DEFAULT_TEST_PULSE = 300;

int currentShoulderPulse = DEFAULT_TEST_PULSE;
bool shoulderEngaged = false;

// ─────────────────────────────────────────
//  WiFi AP Config
// ─────────────────────────────────────────
const char* ssid     = "RoboArm-Shoulder-Calib";
const char* password = "roboarm123";

WebServer server(80);

// Helper: Convert PCA9685 pulse ticks (100-650) to approximate degrees (0-180°)
int pulseToApproxDeg(int pulse) {
  // 100 ticks ≈ ~500us (0°), 600 ticks ≈ ~2500us (180°)
  int deg = map(pulse, 120, 580, 0, 180);
  return constrain(deg, 0, 180);
}

// Power off ALL 16 channels completely
void powerOffAllMotors() {
  for (int ch = 0; ch < 16; ch++) {
    pwm.setPWM(ch, 0, 0);
  }
  shoulderEngaged = false;
  Serial.println("🛑 ALL MOTORS POWERED OFF (PWM 0 on all 16 channels).");
}

// Apply pulse ONLY to Shoulder (CH 2), ensuring all other channels stay 0
void setShoulderPulse(int p) {
  currentShoulderPulse = constrain(p, MIN_RAW_PULSE, MAX_RAW_PULSE);
  
  // Guarantee every other channel is completely unpowered
  for (int ch = 0; ch < 16; ch++) {
    if (ch != SHOULDER_CH) {
      pwm.setPWM(ch, 0, 0);
    }
  }
  
  // Send PWM to shoulder only
  pwm.setPWM(SHOULDER_CH, 0, currentShoulderPulse);
  shoulderEngaged = true;

  Serial.printf("🎯 Shoulder (CH2) Pulse: %d | Approx: ~%d° (All other motors: OFF)\n", 
                currentShoulderPulse, pulseToApproxDeg(currentShoulderPulse));
}

void stepShoulder(int delta) {
  setShoulderPulse(currentShoulderPulse + delta);
}

// ─────────────────────────────────────────
//  Web UI (Clean Slate Shoulder Calibrator)
// ─────────────────────────────────────────
const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1.0"/>
<title>Shoulder Angle Discovery | RoboArm</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800&display=swap');
  *{margin:0;padding:0;box-sizing:border-box;}
  body{
    font-family:'Inter',sans-serif;
    background:#08090e;
    color:#e2e8f0;
    min-height:100vh;
    display:flex;
    flex-direction:column;
    align-items:center;
    padding:16px 12px;
  }
  .card{
    background:#0f1322;
    border:1px solid #1e2640;
    border-radius:24px;
    padding:22px 18px;
    width:100%;
    max-width:460px;
    box-shadow:0 12px 40px rgba(0,0,0,0.5);
    text-align:center;
  }
  h1{font-size:1.3rem;font-weight:800;color:#38bdf8;margin-bottom:4px;letter-spacing:0.5px;}
  .subtitle{font-size:0.78rem;color:#94a3b8;margin-bottom:12px;}

  .status-banner{
    background:#052e16;border:1px solid #16a34a55;border-radius:12px;
    padding:8px 12px;font-size:0.75rem;color:#4ade80;font-weight:600;margin-bottom:14px;
    display:flex;align-items:center;justify-content:center;gap:6px;
  }

  .box{
    background:#13182b;border:1px solid #1e293b;
    border-radius:16px;padding:16px 14px;margin-bottom:14px;text-align:left;
  }
  .box-title{font-size:0.85rem;font-weight:700;color:#f1f5f9;margin-bottom:2px;}
  .box-sub{font-size:0.72rem;color:#64748b;margin-bottom:12px;}

  .hero-val-container{
    display:flex;justify-content:space-around;align-items:baseline;
    background:#0a0d18;border:1px solid #1e2640;border-radius:14px;
    padding:12px 6px;margin-bottom:14px;
  }
  .hero-item{text-align:center;}
  .hero-label{font-size:0.68rem;text-transform:uppercase;color:#64748b;font-weight:700;letter-spacing:0.5px;}
  .hero-num{font-size:2.2rem;font-weight:800;color:#38bdf8;line-height:1.1;}
  .hero-deg{font-size:1.5rem;font-weight:700;color:#a78bfa;line-height:1.1;}

  input[type=range]{
    width:100%;height:10px;border-radius:6px;
    background:#1e293b;outline:none;margin-bottom:14px;cursor:pointer;
  }

  .btn-grid{display:grid;grid-template-columns:repeat(4, 1fr);gap:6px;margin-bottom:8px;}
  .btn{
    padding:11px 4px;font-size:0.82rem;font-weight:700;border:none;
    border-radius:10px;cursor:pointer;transition:transform 0.1s ease;
  }
  .btn:active{transform:scale(0.95);}
  .btn-inc{background:#4f46e5;color:#fff;}
  .btn-inc:hover{background:#4338ca;}
  .btn-dec{background:#0284c7;color:#fff;}
  .btn-dec:hover{background:#0369a1;}

  .direct-form{display:flex;gap:8px;margin-top:10px;}
  .direct-input{
    flex:1;background:#0a0d18;border:1px solid #334155;border-radius:10px;
    padding:9px 12px;color:#fff;font-size:0.9rem;font-weight:600;text-align:center;
  }
  .btn-set{
    background:#38bdf8;color:#0f172a;border:none;border-radius:10px;
    padding:9px 18px;font-size:0.85rem;font-weight:700;cursor:pointer;
  }

  .btn-engage{
    width:100%;padding:13px;background:#059669;color:#fff;border:none;
    border-radius:12px;font-size:0.9rem;font-weight:700;cursor:pointer;margin-bottom:10px;
  }
  .btn-cut{
    width:100%;padding:14px;background:#7f1d1d;border:1px solid #ef444466;
    color:#fca5a5;font-size:0.9rem;font-weight:800;border-radius:12px;cursor:pointer;
  }
  .btn-cut:active{transform:scale(0.97);}

  .notepad{
    background:#0a0d18;border:1px dashed #334155;border-radius:14px;
    padding:12px;margin-top:12px;font-size:0.75rem;color:#94a3b8;line-height:1.5;text-align:left;
  }
  .notepad b{color:#38bdf8;}
  .status{margin-top:12px;font-size:0.8rem;color:#38bdf8;min-height:18px;font-weight:600;}
</style>
</head>
<body>
<div class="card">
  <h1>🦾 SHOULDER ANGLE FINDER</h1>
  <p class="subtitle">Channel 2 (MG996R) | Clean Slate Calibration</p>

  <div class="status-banner">
    <span>🔒 ALL OTHER MOTORS ARE COMPLETELY POWERED OFF (PWM = 0)</span>
  </div>

  <!-- Live Value Display -->
  <div class="box">
    <div class="box-title">📍 Shoulder Pulse & Angle</div>
    <div class="box-sub">Full range unlocked (100 → 650). Find your physical limits.</div>

    <div class="hero-val-container">
      <div class="hero-item">
        <div class="hero-label">Pulse Ticks</div>
        <div class="hero-num" id="dispPulse">300</div>
      </div>
      <div class="hero-item">
        <div class="hero-label">Estimated Angle</div>
        <div class="hero-deg" id="dispDeg">~70°</div>
      </div>
    </div>

    <input type="range" min="100" max="650" value="300" id="slider" oninput="onSlide(this.value)" onchange="sendPulse(this.value)">

    <div class="direct-form">
      <input type="number" id="numInput" class="direct-input" placeholder="Type Pulse (100-650)" min="100" max="650" value="300"/>
      <button class="btn-set" onclick="sendManual()">SEND PULSE</button>
    </div>
  </div>

  <!-- Precision Steppers -->
  <div class="box">
    <div class="box-title">⚡ Precision Step Nudge</div>
    <div class="box-sub">Step slowly until you find the exact mechanical limits:</div>

    <!-- Decrement -->
    <div class="btn-grid">
      <button class="btn btn-dec" onclick="step(-25)">-25</button>
      <button class="btn btn-dec" onclick="step(-10)">-10</button>
      <button class="btn btn-dec" onclick="step(-5)">-5</button>
      <button class="btn btn-dec" onclick="step(-1)">-1</button>
    </div>

    <!-- Increment -->
    <div class="btn-grid">
      <button class="btn btn-inc" onclick="step(+1)">+1</button>
      <button class="btn btn-inc" onclick="step(+5)">+5</button>
      <button class="btn btn-inc" onclick="step(+10)">+10</button>
      <button class="btn btn-inc" onclick="step(+25)">+25</button>
    </div>
  </div>

  <button class="btn-engage" onclick="sendPulse(300)">🎯 ENGAGE AT NEUTRAL (300)</button>
  <button class="btn-cut" onclick="cutPower()">🛑 POWER OFF ALL MOTORS (PWM 0)</button>

  <div class="notepad">
    <b>📝 How to record your new Shoulder limits:</b><br/>
    1. Step <b>down</b> (-10/-5/-1) until shoulder reaches bottom/back physical limit ➔ Note down <b>SHOULDER_MIN</b>.<br/>
    2. Step <b>up</b> (+10/+5/+1) until shoulder reaches upright/forward physical limit ➔ Note down <b>SHOULDER_MAX</b>.<br/>
    3. Find comfortable center/resting posture ➔ Note down <b>SHOULDER_REST</b>.
  </div>

  <div class="status" id="status">Standby. Motors powered off on boot. Ready to calibrate.</div>
</div>

<script>
let currentPulse = 300;

function calcDeg(p){
  let d = Math.round((p - 120) * (180 / 460));
  if(d < 0) d = 0;
  if(d > 180) d = 180;
  return d;
}

function updateUI(p){
  currentPulse = parseInt(p);
  document.getElementById('dispPulse').innerText = currentPulse;
  document.getElementById('dispDeg').innerText = '~' + calcDeg(currentPulse) + '°';
  document.getElementById('slider').value = currentPulse;
  document.getElementById('numInput').value = currentPulse;
}

function onSlide(v){
  document.getElementById('dispPulse').innerText = v;
  document.getElementById('dispDeg').innerText = '~' + calcDeg(v) + '°';
}

function setStatus(msg){
  document.getElementById('status').innerText = msg;
}

function sendPulse(p){
  updateUI(p);
  setStatus('Holding pulse: ' + p + ' (~' + calcDeg(p) + '°)');
  fetch('/set?pulse=' + p)
    .then(r => r.text())
    .then(m => setStatus(m))
    .catch(() => setStatus('Connection error!'));
}

function sendManual(){
  let val = parseInt(document.getElementById('numInput').value);
  if(isNaN(val)) return;
  if(val < 100) val = 100;
  if(val > 650) val = 650;
  sendPulse(val);
}

function step(delta){
  let target = currentPulse + delta;
  if(target < 100) target = 100;
  if(target > 650) target = 650;
  sendPulse(target);
}

function cutPower(){
  fetch('/cut')
    .then(r => r.text())
    .then(m => {
      setStatus('🛑 ' + m);
    })
    .catch(() => setStatus('Connection error!'));
}
</script>
</body>
</html>
)rawliteral";

// ─────────────────────────────────────────
//  HTTP Handlers
// ─────────────────────────────────────────
void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send_P(200, "text/html", HTML);
}

void handleSet() {
  if (server.hasArg("pulse")) {
    int p = server.arg("pulse").toInt();
    setShoulderPulse(p);
    server.send(200, "text/plain", "Shoulder held at: " + String(currentShoulderPulse) + " (~" + String(pulseToApproxDeg(currentShoulderPulse)) + "°)");
  } else {
    server.send(400, "text/plain", "Missing pulse");
  }
}

void handleStep() {
  if (server.hasArg("delta")) {
    int d = server.arg("delta").toInt();
    stepShoulder(d);
    server.send(200, "text/plain", "Shoulder stepped to: " + String(currentShoulderPulse) + " (~" + String(pulseToApproxDeg(currentShoulderPulse)) + "°)");
  } else {
    server.send(400, "text/plain", "Missing delta");
  }
}

void handleCut() {
  powerOffAllMotors();
  server.send(200, "text/plain", "All motors powered off (PWM 0)");
}

// ─────────────────────────────────────────
//  Setup & Loop
// ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Shoulder Angle Finder (Clean Slate Calibration) ===");

  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(SERVO_FREQ);
  delay(50);

  // Power OFF ALL 16 channels on startup (Zero PWM on all motors)
  powerOffAllMotors();
  Serial.println("Initial State: ALL MOTORS POWERED OFF (PWM 0).");
  Serial.println("Shoulder will only move when commanded via Web UI or Serial.");

  WiFi.softAP(ssid, password);
  Serial.printf("WiFi AP: %s  |  http://%s\n", ssid, WiFi.softAPIP().toString().c_str());

  server.on("/",     handleRoot);
  server.on("/set",  handleSet);
  server.on("/step", handleStep);
  server.on("/cut",  handleCut);
  server.begin();

  Serial.println("Ready! Connect to 'RoboArm-Shoulder-Calib' (pass: roboarm123) and open http://192.168.4.1");
}

void loop() {
  server.handleClient();
  
  // Optional Serial command input: type any pulse number (100-650) or 'c' to cut power
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.equalsIgnoreCase("c") || input.equalsIgnoreCase("cut") || input.equalsIgnoreCase("0")) {
      powerOffAllMotors();
    } else {
      int p = input.toInt();
      if (p >= MIN_RAW_PULSE && p <= MAX_RAW_PULSE) {
        setShoulderPulse(p);
      } else {
        Serial.println("Invalid pulse! Enter a value between 100 and 650, or 'c' to cut power.");
      }
    }
  }
}
