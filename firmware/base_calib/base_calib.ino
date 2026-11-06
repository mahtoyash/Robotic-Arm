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
//  Joint Channels & Locks
// ─────────────────────────────────────────
const int BASE_CH     = 0;  // MG996R — TESTING JOINT: Base Rotation
const int SHOULDER_CH = 2;  // MG996R — Locked at 290
const int ELBOW_CH    = 4;  // MG996R — Locked at 290
const int ROLL_CH     = 6;  // MG90S  — Locked at 160
const int PITCH_CH    = 8;  // MG90S  — Locked at 252
const int GRIPPER_CH  = 10; // MG90S  — Locked at 350

const int ARM_LOCK_PULSE     = 290;
const int ROLL_LOCK_PULSE    = 160;
const int PITCH_LOCK_PULSE   = 252;
const int GRIPPER_LOCK_PULSE = 350;

// Base 360° Neutral & Nudge Settings
int baseStopPulse = 290; // Default expected neutral
int speedDelta    = 18;  // Safe controlled crawl speed

// ─────────────────────────────────────────
//  WiFi AP Config
// ─────────────────────────────────────────
const char* ssid     = "RoboArm-Base";
const char* password = "roboarm123";

WebServer server(80);

void lockOtherJoints() {
  pwm.setPWM(SHOULDER_CH, 0, ARM_LOCK_PULSE);
  pwm.setPWM(ELBOW_CH,    0, ARM_LOCK_PULSE);
  pwm.setPWM(ROLL_CH,     0, ROLL_LOCK_PULSE);
  pwm.setPWM(PITCH_CH,    0, PITCH_LOCK_PULSE);
  pwm.setPWM(GRIPPER_CH,  0, GRIPPER_LOCK_PULSE);
}

void sendBaseStop() {
  pwm.setPWM(BASE_CH, 0, baseStopPulse);
}

void timedBaseNudge(int pulse, int durationMs) {
  pwm.setPWM(BASE_CH, 0, pulse);
  delay(durationMs);
  sendBaseStop(); // Auto-stop back to neutral
}

void directHold(int p) {
  p = constrain(p, 150, 450);
  pwm.setPWM(BASE_CH, 0, p);
  Serial.printf("Base direct pulse: %d\n", p);
}

void cutAllPower() {
  for (int ch = 0; ch < 16; ch++) {
    pwm.setPWM(ch, 0, 0);
  }
  Serial.println("All Power Cut!");
}

// ─────────────────────────────────────────
//  Web UI (Base Rotation Calibrator)
// ─────────────────────────────────────────
const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1.0"/>
<title>Base Rotation Calibrator (CH 0)</title>
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
  h1{font-size:1.35rem;font-weight:700;color:#f59e0b;margin-bottom:4px;}
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
  .box-title{font-size:0.85rem;font-weight:700;color:#f59e0b;margin-bottom:4px;}
  .box-desc{font-size:0.75rem;color:#64748b;margin-bottom:12px;line-height:1.4;}

  .hero-val{font-size:2.5rem;font-weight:700;color:#f59e0b;text-align:center;margin:4px 0 10px;}
  input[type=range]{width:100%;height:8px;border-radius:4px;background:#1e293b;outline:none;margin-bottom:12px;}

  .tune-btns{display:flex;justify-content:center;gap:10px;margin-bottom:12px;}
  .btn-tune{
    padding:8px 16px;background:#1e293b;border:1px solid #334155;
    color:#f1f5f9;border-radius:8px;font-weight:700;cursor:pointer;
  }

  .btn-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:8px;}
  .btn{
    padding:14px 8px;font-size:0.88rem;font-weight:700;border:none;
    border-radius:12px;cursor:pointer;transition:all 0.1s ease;
  }
  .btn:active{transform:scale(0.96);}
  .btn-left{background:#f59e0b;color:#1e1b4b;}
  .btn-right{background:#3b82f6;color:#fff;}
  .btn-micro{font-size:0.8rem;padding:10px 6px;background:#1e293b;border:1px solid #334155;color:#cbd5e1;}

  .btn-stop{
    width:100%;padding:14px;background:#334155;color:#f8fafc;
    font-size:0.9rem;font-weight:700;border-radius:10px;border:none;cursor:pointer;margin-top:6px;
  }

  .btn-cut{
    width:100%;padding:14px;background:#7f1d1d;border:1px solid #ef444455;
    color:#fca5a5;font-size:0.92rem;font-weight:700;border-radius:12px;cursor:pointer;margin-top:4px;
  }
  .status{margin-top:14px;font-size:0.82rem;color:#10b981;min-height:18px;font-weight:600;}
  .warning-box{
    background:#451a03;border:1px solid #b45309;border-radius:10px;
    padding:8px 10px;font-size:0.73rem;color:#fde68a;margin-bottom:12px;line-height:1.4;
  }
</style>
</head>
<body>
<div class="card">
  <h1>🧭 BASE ROTATION (CH 0)</h1>
  <p class="subtitle">MG996R Servo — 360° Neutral & Direction Calibrator</p>
  <span class="lock-badge">🔒 Locked: Shoulder(290), Elbow(290), Pitch(252), Roll(160), Gripper(350)</span>

  <div class="warning-box">
    ⚠️ <b>Careful with Wires:</b> Base 360° ghoomne par wires wrap ho sakti hain. Micro-nudges (50ms–150ms) se safe test karein!
  </div>

  <!-- ── Section A: Find Neutral Stop ── -->
  <div class="box">
    <div class="box-title">1️⃣ Neutral Stop Finder (Find where motor is silent)</div>
    <div class="box-desc">
      Slider se neutral find karo jab tak base motor bilkul shaant (dead silent) na ho jaye:
    </div>
    <div class="hero-val" id="stopDisp">290</div>
    <input type="range" min="260" max="320" value="290" id="stopSlider" oninput="onStopSlide(this.value)" onchange="applyStop(this.value)">

    <div class="tune-btns">
      <button class="btn-tune" onclick="stepStop(-1)">− 1</button>
      <button class="btn-tune" onclick="resetStop290()">Reset 290</button>
      <button class="btn-tune" onclick="stepStop(+1)">+ 1</button>
    </div>
    <button class="btn-stop" onclick="applyStop(stopVal)">⏹ HOLD STOP (Send Neutral)</button>
  </div>

  <!-- ── Section B: Direction Nudges ── -->
  <div class="box">
    <div class="box-title">2️⃣ Direction & Movement Nudges</div>
    <div class="box-desc">
      Nudge karke dekho LEFT kis direction me ghoomta hai aur RIGHT kis me:
    </div>

    <!-- Normal Nudges (120ms) -->
    <div class="btn-grid">
      <button class="btn btn-left"  onclick="nudge('LEFT', 120)">◀ LEFT (120ms)</button>
      <button class="btn btn-right" onclick="nudge('RIGHT', 120)">▶ RIGHT (120ms)</button>
    </div>

    <!-- Micro Nudges (50ms) -->
    <div class="btn-grid">
      <button class="btn btn-micro" onclick="nudge('LEFT', 50)">Micro LEFT (50ms)</button>
      <button class="btn btn-micro" onclick="nudge('RIGHT', 50)">Micro RIGHT (50ms)</button>
    </div>

    <!-- Long Nudges (250ms) -->
    <div class="btn-grid">
      <button class="btn btn-micro" onclick="nudge('LEFT', 250)">Long LEFT (250ms)</button>
      <button class="btn btn-micro" onclick="nudge('RIGHT', 250)">Long RIGHT (250ms)</button>
    </div>
  </div>

  <button class="btn-cut" onclick="cutPower()">⚠️ CUT ALL POWER (PWM 0)</button>
  <div class="status" id="status">Ready. Neutral set to 290. All other joints locked!</div>
</div>

<script>
let stopVal = 290;
let delta = 18;

function setStatus(msg){document.getElementById('status').innerText = msg;}

function onStopSlide(v){document.getElementById('stopDisp').innerText = v;}

function applyStop(v){
  stopVal = parseInt(v);
  document.getElementById('stopDisp').innerText = stopVal;
  document.getElementById('stopSlider').value = stopVal;
  fetch('/set_stop?val=' + stopVal)
    .then(r => r.text())
    .then(m => setStatus('Base Neutral Set: ' + stopVal));
}

function stepStop(d){
  applyStop(stopVal + d);
}

function resetStop290(){
  applyStop(290);
}

function nudge(dir, ms){
  setStatus('Nudging ' + dir + ' (' + ms + 'ms)...');
  fetch('/nudge?dir=' + dir + '&ms=' + ms)
    .then(r => r.text())
    .then(m => setStatus(m))
    .catch(() => setStatus('Connection Error!'));
}

function cutPower(){
  fetch('/cut').then(() => setStatus('All PWM Cut! Base Relaxed.'));
}
</script>
</body>
</html>
)rawliteral";

// ─────────────────────────────────────────
//  HTTP Handlers
// ─────────────────────────────────────────
void handleRoot() { server.send_P(200, "text/html", HTML); }

void handleNudge() {
  if (server.hasArg("dir") && server.hasArg("ms")) {
    String dir = server.arg("dir");
    int ms     = constrain(server.arg("ms").toInt(), 20, 500);

    int targetPulse = baseStopPulse;
    // LEFT: > neutral (e.g. 290 + 18 = 308)
    // RIGHT: < neutral (e.g. 290 - 18 = 272)
    if      (dir == "LEFT")  targetPulse = baseStopPulse + speedDelta;
    else if (dir == "RIGHT") targetPulse = baseStopPulse - speedDelta;

    timedBaseNudge(targetPulse, ms);
    String msg = "Nudge " + dir + " | " + String(targetPulse) + " for " + String(ms) + "ms -> back to stop " + String(baseStopPulse);
    Serial.println(msg);
    server.send(200, "text/plain", msg);
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleSetStop() {
  if (server.hasArg("val")) {
    baseStopPulse = constrain(server.arg("val").toInt(), 250, 350);
    sendBaseStop();
    server.send(200, "text/plain", "Base Stop Set: " + String(baseStopPulse));
  } else {
    server.send(400, "text/plain", "Missing val");
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
  Serial.println("\n=== Base Rotation Calibrator (CH 0) ===");

  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(SERVO_FREQ);
  delay(50);

  // Lock all other joints firmly
  lockOtherJoints();
  Serial.println("Shoulder(290), Elbow(290), Pitch(252), Roll(160), Gripper(350) LOCKED!");

  // Prime Base to initial stop (290)
  sendBaseStop();
  Serial.printf("Base (CH0) initialized at neutral stop: %d\n", baseStopPulse);

  WiFi.softAP(ssid, password);
  Serial.printf("WiFi: %s  |  http://%s\n", ssid, WiFi.softAPIP().toString().c_str());

  server.on("/",         handleRoot);
  server.on("/nudge",    handleNudge);
  server.on("/set_stop", handleSetStop);
  server.on("/cut",      handleCut);
  server.begin();

  Serial.println("Base Calibrator Ready on CH0!");
}

void loop() {
  server.handleClient();
}
