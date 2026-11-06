#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// ─────────────────────────────────────────
//  PCA9685 Config
// ─────────────────────────────────────────
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
#define SERVO_FREQ 50

// ─────────────────────────────────────────
//  Hardware Channel Map & Calibrated Limits
// ─────────────────────────────────────────
// CH 0: MG996R 360° Continuous Rotation (Base)
const int BASE_CH         = 0;
const int BASE_STOP       = 290;
const int BASE_SPEED_STEP = 25;   // Left: 265, Right: 315

// CH 2: MG996R 180° Positional (Shoulder)
const int SHOULDER_CH     = 2;
const int SHOULDER_MIN    = 180;
const int SHOULDER_MAX    = 500;

// CH 4: MG996R 180° Positional (Elbow)
const int ELBOW_CH        = 4;
const int ELBOW_MIN       = 85;
const int ELBOW_MAX       = 505;

// CH 6: MG90S 180° Positional (Wrist Roll)
const int ROLL_CH         = 6;
const int ROLL_MIN        = 100;
const int ROLL_MAX        = 472;

// CH 8: MG90S 180° Positional (Wrist Pitch)
const int PITCH_CH        = 8;
const int PITCH_MIN       = 95;
const int PITCH_MAX       = 460;

// CH 10: MG90S 180° Positional (Gripper)
const int GRIPPER_CH      = 10;
const int GRIPPER_MIN     = 180;
const int GRIPPER_MAX     = 450;
const int GRIPPER_CLOSE   = 246;
const int GRIPPER_OPEN    = 430;

// ─────────────────────────────────────────
//  Calibrated Master Poses
// ─────────────────────────────────────────
// 1. OFFICIAL SLEEP / RELAX POSE (Folded, Low Gravity Load, Rest Position)
const int RELAX_SHOULDER = 480;
const int RELAX_ELBOW    = 505;
const int RELAX_ROLL     = 472;
const int RELAX_PITCH    = 460;
const int RELAX_GRIPPER  = 246;

// 2. WORKING / WAKE READY POSE (Calibrated Active Working Stance)
const int WORK_SHOULDER  = 410;
const int WORK_ELBOW     = 320;
const int WORK_ROLL      = 100;
const int WORK_PITCH     = 95;
const int WORK_GRIPPER   = 246;

// ─────────────────────────────────────────
//  Live Joint State (Booting at RELAX Pose)
// ─────────────────────────────────────────
int curShoulder = RELAX_SHOULDER;
int curElbow    = RELAX_ELBOW;
int curRoll     = RELAX_ROLL;
int curPitch    = RELAX_PITCH;
int curGripper  = RELAX_GRIPPER;

// ─────────────────────────────────────────
//  WiFi Dual Mode: Home Wi-Fi (for Internet Speech) + Direct Standalone AP
// ─────────────────────────────────────────
const char* HOME_SSID     = "YOUR_WIFI_SSID";
const char* HOME_PASS     = "YOUR_WIFI_PASSWORD";

const char* AP_SSID       = "RoboArm-Studio";
const char* AP_PASS       = "roboarm123";

WebServer server(80);

// ─────────────────────────────────────────
//  Servo Control Primitives
// ─────────────────────────────────────────
void setShoulder(int p) {
  curShoulder = constrain(p, SHOULDER_MIN, SHOULDER_MAX);
  pwm.setPWM(SHOULDER_CH, 0, curShoulder);
  Serial.printf("Shoulder: %d\n", curShoulder);
}

void setElbow(int p) {
  curElbow = constrain(p, ELBOW_MIN, ELBOW_MAX);
  pwm.setPWM(ELBOW_CH, 0, curElbow);
  Serial.printf("Elbow: %d\n", curElbow);
}

void setRoll(int p) {
  curRoll = constrain(p, ROLL_MIN, ROLL_MAX);
  pwm.setPWM(ROLL_CH, 0, curRoll);
  Serial.printf("Roll: %d\n", curRoll);
}

void setPitch(int p) {
  curPitch = constrain(p, PITCH_MIN, PITCH_MAX);
  pwm.setPWM(PITCH_CH, 0, curPitch);
  Serial.printf("Pitch: %d\n", curPitch);
}

void setGripper(int p) {
  curGripper = constrain(p, GRIPPER_MIN, GRIPPER_MAX);
  pwm.setPWM(GRIPPER_CH, 0, curGripper);
  Serial.printf("Gripper: %d\n", curGripper);
}

// ─────────────────────────────────────────
//  Base 360° Continuous Control
// ─────────────────────────────────────────
void baseStop() {
  pwm.setPWM(BASE_CH, 0, BASE_STOP);
}

void baseNudge(int delta, int ms) {
  pwm.setPWM(BASE_CH, 0, BASE_STOP + delta);
  delay(ms);
  baseStop();
}

void smoothBaseNudge(int delta, int ms = 300) {
  int halfDelta = delta / 2;
  pwm.setPWM(BASE_CH, 0, BASE_STOP + halfDelta);
  delay(40);
  pwm.setPWM(BASE_CH, 0, BASE_STOP + delta);
  delay(max(ms - 80, 50));
  pwm.setPWM(BASE_CH, 0, BASE_STOP + halfDelta);
  delay(40);
  baseStop();
}

// ─────────────────────────────────────────
//  Smooth Single-Joint S-Curve Motion (Zero-Jerk)
// ─────────────────────────────────────────
void smoothJointTo(int ch, int &currentVal, int targetVal, int minLimit, int maxLimit, int stepDelayMs = 12) {
  targetVal = constrain(targetVal, minLimit, maxLimit);
  if (currentVal == targetVal) return;

  int startVal = currentVal;
  int delta = targetVal - startVal;
  int totalSteps = max(abs(delta), 30);

  for (int step = 1; step <= totalSteps; step++) {
    float t = (float)step / (float)totalSteps;
    float ease = (1.0f - cosf(t * 3.14159265f)) * 0.5f; // S-Curve Easing
    currentVal = startVal + round(delta * ease);
    pwm.setPWM(ch, 0, currentVal);
    delay(stepDelayMs);
  }
  currentVal = targetVal;
  pwm.setPWM(ch, 0, currentVal);
  Serial.printf("Smooth Joint CH%d -> %d\n", ch, currentVal);
}

void stepJoint(int ch, int delta) {
  if (ch == SHOULDER_CH)      smoothJointTo(SHOULDER_CH, curShoulder, curShoulder + delta, SHOULDER_MIN, SHOULDER_MAX, 10);
  else if (ch == ELBOW_CH)    smoothJointTo(ELBOW_CH,    curElbow,    curElbow + delta,    ELBOW_MIN,    ELBOW_MAX,    10);
  else if (ch == ROLL_CH)     smoothJointTo(ROLL_CH,     curRoll,     curRoll + delta,     ROLL_MIN,     ROLL_MAX,     10);
  else if (ch == PITCH_CH)    smoothJointTo(PITCH_CH,    curPitch,    curPitch + delta,    PITCH_MIN,    PITCH_MAX,    10);
  else if (ch == GRIPPER_CH)  setGripper(curGripper + delta); // Gripper remains fast!
}

// ─────────────────────────────────────────
//  Synchronized Multi-Axis S-Curve Gliding (Zero Vibration / Zero Jerk)
//  All joints move in unison, accelerate softly and ease into position simultaneously!
// ─────────────────────────────────────────
void smoothMoveTo(int targetS, int targetE, int targetR, int targetP, int targetG, int stepDelayMs = 14) {
  targetS = constrain(targetS, SHOULDER_MIN, SHOULDER_MAX);
  targetE = constrain(targetE, ELBOW_MIN, ELBOW_MAX);
  targetR = constrain(targetR, ROLL_MIN, ROLL_MAX);
  targetP = constrain(targetP, PITCH_MIN, PITCH_MAX);
  targetG = constrain(targetG, GRIPPER_MIN, GRIPPER_MAX);

  int startS = curShoulder;
  int startE = curElbow;
  int startR = curRoll;
  int startP = curPitch;
  int startG = curGripper;

  int dS = targetS - startS;
  int dE = targetE - startE;
  int dR = targetR - startR;
  int dP = targetP - startP;
  int dG = targetG - startG;

  int maxDelta = max(abs(dS), max(abs(dE), max(abs(dR), max(abs(dP), abs(dG)))));
  if (maxDelta == 0) return;

  // Minimum 140 interpolation steps for silky-smooth motion
  int totalSteps = max(maxDelta, 140);

  for (int step = 1; step <= totalSteps; step++) {
    float t = (float)step / (float)totalSteps; // 0.0 to 1.0
    // Cosine S-Curve Easing: 0 acceleration at start and 0 deceleration at end!
    float ease = (1.0f - cosf(t * 3.14159265f)) * 0.5f;

    curShoulder = startS + round(dS * ease);
    curElbow    = startE + round(dE * ease);
    curRoll     = startR + round(dR * ease);
    curPitch    = startP + round(dP * ease);
    curGripper  = startG + round(dG * ease);

    pwm.setPWM(SHOULDER_CH, 0, curShoulder);
    pwm.setPWM(ELBOW_CH,    0, curElbow);
    pwm.setPWM(ROLL_CH,     0, curRoll);
    pwm.setPWM(PITCH_CH,    0, curPitch);
    pwm.setPWM(GRIPPER_CH,  0, curGripper);

    delay(stepDelayMs);
  }

  // Final exact lock
  curShoulder = targetS;
  curElbow    = targetE;
  curRoll     = targetR;
  curPitch    = targetP;
  curGripper  = targetG;

  pwm.setPWM(SHOULDER_CH, 0, curShoulder);
  pwm.setPWM(ELBOW_CH,    0, curElbow);
  pwm.setPWM(ROLL_CH,     0, curRoll);
  pwm.setPWM(PITCH_CH,    0, curPitch);
  pwm.setPWM(GRIPPER_CH,  0, curGripper);

  Serial.printf("Synchronized S-Curve Move Done -> S:%d E:%d R:%d P:%d G:%d\n", curShoulder, curElbow, curRoll, curPitch, curGripper);
}

void cutAllPower() {
  for (int ch = 0; ch < 16; ch++) {
    pwm.setPWM(ch, 0, 0);
  }
  Serial.println("⚠️ All PWM Cut! Arm Relaxed.");
}

// ─────────────────────────────────────────
//  Web UI HTML / CSS / JavaScript
// ─────────────────────────────────────────
const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1.0"/>
<title>RoboArm Studio — 5-DOF Master Studio</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800&display=swap');
  *{margin:0;padding:0;box-sizing:border-box;}
  body{
    font-family:'Inter',sans-serif;
    background:#08090e;
    color:#e2e8f0;
    min-height:100vh;
    padding:16px 12px;
    display:flex;
    flex-direction:column;
    align-items:center;
  }
  .container{width:100%;max-width:480px;}
  .header{text-align:center;margin-bottom:14px;}
  .header h1{font-size:1.4rem;font-weight:800;color:#38bdf8;letter-spacing:0.5px;}
  .header p{font-size:0.75rem;color:#94a3b8;margin-top:2px;}

  .card{
    background:#0f1322;
    border:1px solid #1e2640;
    border-radius:18px;
    padding:14px;
    margin-bottom:12px;
    box-shadow:0 8px 24px rgba(0,0,0,0.4);
  }

  .joint-title-row{
    display:flex;
    justify-content:space-between;
    align-items:center;
    margin-bottom:8px;
  }
  .joint-name{font-size:0.88rem;font-weight:700;color:#f1f5f9;display:flex;align-items:center;gap:6px;}
  .joint-tag{font-size:0.68rem;padding:2px 8px;border-radius:8px;font-weight:600;}
  .tag-pos{background:#0284c725;color:#38bdf8;border:1px solid #0284c750;}
  .tag-360{background:#d9770625;color:#f59e0b;border:1px solid #d9770650;}

  .joint-val{
    font-size:1.15rem;
    font-weight:800;
    color:#38bdf8;
    background:#141b2d;
    padding:3px 12px;
    border-radius:10px;
    border:1px solid #1f2a48;
    min-width:60px;
    text-align:center;
  }

  input[type=range]{
    width:100%;
    height:8px;
    border-radius:4px;
    background:#1c2438;
    outline:none;
    margin:8px 0 10px;
    cursor:pointer;
  }

  .step-grid{
    display:grid;
    grid-template-columns:repeat(4,1fr);
    gap:6px;
  }
  .btn{
    padding:9px 4px;
    font-size:0.78rem;
    font-weight:700;
    border:none;
    border-radius:10px;
    cursor:pointer;
    transition:all 0.1s ease;
    text-align:center;
  }
  .btn:active{transform:scale(0.96);}
  .btn-dec{background:#1e293b;color:#cbd5e1;border:1px solid #334155;}
  .btn-dec:hover{background:#334155;color:#fff;}
  .btn-inc{background:#312e81;color:#c7d2fe;border:1px solid #4338ca;}
  .btn-inc:hover{background:#4338ca;color:#fff;}

  /* Poses grid */
  .pose-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:6px;margin-top:6px;}
  .btn-pose{
    padding:10px 4px;background:#1e1b4b;border:1px solid #4f46e5;
    color:#e0e7ff;font-size:0.75rem;font-weight:700;border-radius:10px;cursor:pointer;
  }
  .btn-pose:hover{background:#4338ca;}

  /* Base buttons */
  .base-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:6px;}
  .btn-base{
    padding:12px;background:#1e293b;border:1px solid #475569;
    color:#f8fafc;font-size:0.82rem;font-weight:700;border-radius:10px;cursor:pointer;
  }
  .btn-base:hover{background:#334155;}

  /* Gripper quick bar */
  .grip-grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px;margin-top:8px;}
  .btn-grip{
    padding:9px;font-size:0.78rem;font-weight:700;border-radius:10px;border:none;cursor:pointer;
  }
  .btn-close{background:#b91c1c;color:#fef2f2;}
  .btn-mid{background:#334155;color:#e2e8f0;}
  .btn-open{background:#059669;color:#ecfdf5;}

  /* Voice Assistant Card */
  .voice-card{
    background:linear-gradient(135deg, #0b1120 0%, #171133 100%);
    border:1px solid #6366f166;
    box-shadow:0 8px 30px rgba(99,102,241,0.22);
  }
  .voice-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;}
  .voice-title{font-size:0.92rem;font-weight:800;color:#c7d2fe;display:flex;align-items:center;gap:6px;}
  .voice-toggle-btn{
    padding:8px 14px;border-radius:12px;font-size:0.78rem;font-weight:800;
    border:none;cursor:pointer;transition:all 0.2s ease;
  }
  .v-btn-start{background:#4f46e5;color:#fff;box-shadow:0 4px 14px #4f46e550;}
  .v-btn-stop{background:#dc2626;color:#fff;box-shadow:0 4px 14px #dc262650;}
  .voice-badge{
    display:inline-block;padding:4px 10px;border-radius:10px;font-size:0.72rem;font-weight:700;margin-bottom:8px;
  }
  .v-idle{background:#1e293b;color:#94a3b8;border:1px solid #334155;}
  .v-listen{background:#05966925;color:#34d399;border:1px solid #10b98150;animation:pulse 1.5s infinite;}
  .v-active{background:#4f46e530;color:#c7d2fe;border:1px solid #6366f180;animation:pulse 0.8s infinite;}
  @keyframes pulse{
    0%{box-shadow:0 0 0 0 rgba(99,102,241,0.4);}
    70%{box-shadow:0 0 0 8px rgba(99,102,241,0);}
    100%{box-shadow:0 0 0 0 rgba(99,102,241,0);}
  }
  .transcript-box{
    background:#080b14;border:1px solid #1e293b;border-radius:12px;
    padding:10px;font-size:0.82rem;min-height:38px;color:#38bdf8;
    display:flex;align-items:center;justify-content:space-between;margin-bottom:8px;
  }
  .chips-container{display:flex;flex-wrap:wrap;gap:4px;margin-top:4px;}
  .chip{
    font-size:0.68rem;padding:3px 7px;background:#1e1b4b;border:1px solid #4338ca44;
    color:#c7d2fe;border-radius:6px;
  }

  /* Cut power */
  .btn-cut{
    width:100%;padding:13px;background:#7f1d1d;border:1px solid #ef444455;
    color:#fca5a5;font-size:0.88rem;font-weight:800;border-radius:12px;cursor:pointer;margin-top:6px;
  }
  .status{
    text-align:center;margin-top:10px;font-size:0.78rem;color:#10b981;font-weight:600;min-height:16px;
  }
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <h1>🦾 ROBOARM STUDIO</h1>
    <p>5-DOF Calibrated Positional Control</p>
  </div>

  <!-- ── 🎙️ VOICE ASSISTANT CARD (Hands-Free) ── -->
  <div class="card voice-card">
    <div class="voice-header">
      <div class="voice-title">🎙️ ROBO VOICE ASSISTANT</div>
      <button class="voice-toggle-btn v-btn-start" id="voiceToggle" onclick="toggleVoice()">🎙️ Enable Voice</button>
    </div>
    <div style="display:flex;justify-content:space-between;align-items:center;">
      <span class="voice-badge v-idle" id="voiceBadge">💤 Voice Off (Tap Enable)</span>
      <span style="font-size:0.7rem;color:#818cf8;">Wake Word: <b>"ROBO"</b></span>
    </div>
    <div class="transcript-box">
      <span id="transcriptText">Say "Robo" + Command (or tap Enable Voice)...</span>
      <span id="voiceExecIcon" style="font-size:0.9rem;">🤖</span>
    </div>
    <div class="chips-container">
      <span class="chip">Robo Wake Up</span>
      <span class="chip">Robo Sleep</span>
      <span class="chip">Base Left / Right</span>
      <span class="chip" style="background:#14532d;color:#86efac;">Shoulder Reverse</span>
      <span class="chip">Shoulder Front</span>
      <span class="chip">Elbow Down / Up</span>
      <span class="chip" style="background:#1e1b4b;color:#a5b4fc;">Up / Down</span>
      <span class="chip">Gripper Open / Close</span>
    </div>
  </div>

  <!-- ── 1. Preset Poses (Gliding Multi-Joint) ── -->
  <div class="card">
    <div class="joint-name" style="margin-bottom:8px;">🎯 Intelligent Poses (Smooth Gliding)</div>
    <div class="pose-grid">
      <button class="btn-pose" style="background:#14532d;border-color:#22c55e;" onclick="sendPose('wake')">🌅 WAKE UP</button>
      <button class="btn-pose" style="background:#311042;border-color:#a855f7;" onclick="sendPose('relax')">💤 PARK / RELAX</button>
      <button class="btn-pose" onclick="sendPose('reach')">🎯 REACH</button>
      <button class="btn-pose" onclick="sendPose('pick')">📦 PICK</button>
    </div>
  </div>

  <!-- ── 2. SHOULDER (CH 2 - 180° Positional) ── -->
  <div class="card">
    <div class="joint-title-row">
      <div class="joint-name">
        <span>💪 Shoulder (CH 2)</span>
        <span class="joint-tag tag-pos">180° (180-500)</span>
      </div>
      <div class="joint-val" id="val-shoulder">480</div>
    </div>
    <input type="range" min="180" max="500" value="480" id="slide-shoulder"
           oninput="showVal('shoulder',this.value)" onchange="sendJoint('shoulder',this.value)">
    <div class="step-grid">
      <button class="btn btn-dec" onclick="stepJoint('shoulder',-15)">-15</button>
      <button class="btn btn-dec" onclick="stepJoint('shoulder',-5)">-5</button>
      <button class="btn btn-inc" onclick="stepJoint('shoulder',+5)">+5</button>
      <button class="btn btn-inc" onclick="stepJoint('shoulder',+15)">+15</button>
    </div>
  </div>

  <!-- ── 3. ELBOW (CH 4 - 180° Positional) ── -->
  <div class="card">
    <div class="joint-title-row">
      <div class="joint-name">
        <span>🦾 Elbow (CH 4)</span>
        <span class="joint-tag tag-pos">180° (85-505)</span>
      </div>
      <div class="joint-val" id="val-elbow">505</div>
    </div>
    <input type="range" min="85" max="505" value="505" id="slide-elbow"
           oninput="showVal('elbow',this.value)" onchange="sendJoint('elbow',this.value)">
    <div class="step-grid">
      <button class="btn btn-dec" onclick="stepJoint('elbow',-15)">-15</button>
      <button class="btn btn-dec" onclick="stepJoint('elbow',-5)">-5</button>
      <button class="btn btn-inc" onclick="stepJoint('elbow',+5)">+5</button>
      <button class="btn btn-inc" onclick="stepJoint('elbow',+15)">+15</button>
    </div>
  </div>

  <!-- ── 4. BASE ROTATION (CH 0 - 360° Continuous) ── -->
  <div class="card">
    <div class="joint-title-row">
      <div class="joint-name">
        <span>🔄 Base Pan (CH 0)</span>
        <span class="joint-tag tag-360">360° Continuous</span>
      </div>
      <div class="joint-tag" style="background:#1e293b;color:#94a3b8;">Neutral: 290</div>
    </div>
    <div class="base-grid">
      <button class="btn-base" onclick="nudgeBase('left', 200)">↺ Turn Left (200ms)</button>
      <button class="btn-base" onclick="nudgeBase('right', 200)">↻ Turn Right (200ms)</button>
    </div>
    <div class="base-grid" style="margin-top:6px;">
      <button class="btn-base" style="font-size:0.75rem;" onclick="nudgeBase('left', 80)">↶ Micro Left</button>
      <button class="btn-base" style="font-size:0.75rem;" onclick="nudgeBase('right', 80)">↷ Micro Right</button>
    </div>
  </div>

  <!-- ── 5. WRIST PITCH (CH 8 - 180° Positional) ── -->
  <div class="card">
    <div class="joint-title-row">
      <div class="joint-name">
        <span>↕ Wrist Pitch (CH 8)</span>
        <span class="joint-tag tag-pos">180° (95-460)</span>
      </div>
      <div class="joint-val" id="val-pitch">460</div>
    </div>
    <input type="range" min="95" max="460" value="460" id="slide-pitch"
           oninput="showVal('pitch',this.value)" onchange="sendJoint('pitch',this.value)">
    <div class="step-grid">
      <button class="btn btn-dec" onclick="stepJoint('pitch',-15)">-15</button>
      <button class="btn btn-dec" onclick="stepJoint('pitch',-5)">-5</button>
      <button class="btn btn-inc" onclick="stepJoint('pitch',+5)">+5</button>
      <button class="btn btn-inc" onclick="stepJoint('pitch',+15)">+15</button>
    </div>
  </div>

  <!-- ── 6. WRIST ROLL (CH 6 - 180° Positional) ── -->
  <div class="card">
    <div class="joint-title-row">
      <div class="joint-name">
        <span>🔄 Wrist Roll (CH 6)</span>
        <span class="joint-tag tag-pos">180° (100-472)</span>
      </div>
      <div class="joint-val" id="val-roll">472</div>
    </div>
    <input type="range" min="100" max="472" value="472" id="slide-roll"
           oninput="showVal('roll',this.value)" onchange="sendJoint('roll',this.value)">
    <div class="step-grid">
      <button class="btn btn-dec" onclick="stepJoint('roll',-20)">-20</button>
      <button class="btn btn-dec" onclick="stepJoint('roll',-5)">-5</button>
      <button class="btn btn-inc" onclick="stepJoint('roll',+5)">+5</button>
      <button class="btn btn-inc" onclick="stepJoint('roll',+20)">+20</button>
    </div>
  </div>

  <!-- ── 7. GRIPPER (CH 10 - 180° Positional) ── -->
  <div class="card">
    <div class="joint-title-row">
      <div class="joint-name">
        <span>🤏 Gripper Claw (CH 10)</span>
        <span class="joint-tag tag-pos">180° (180-450)</span>
      </div>
      <div class="joint-val" id="val-gripper">246</div>
    </div>
    <input type="range" min="180" max="450" value="246" id="slide-gripper"
           oninput="showVal('gripper',this.value)" onchange="sendJoint('gripper',this.value)">
    <div class="step-grid">
      <button class="btn btn-dec" onclick="stepJoint('gripper',-15)">-15</button>
      <button class="btn btn-dec" onclick="stepJoint('gripper',-5)">-5</button>
      <button class="btn btn-inc" onclick="stepJoint('gripper',+5)">+5</button>
      <button class="btn btn-inc" onclick="stepJoint('gripper',+15)">+15</button>
    </div>
    <div class="grip-grid">
      <button class="btn-grip btn-close" onclick="sendJoint('gripper', 246)">✊ CLOSE (246)</button>
      <button class="btn-grip btn-mid"   onclick="sendJoint('gripper', 350)">HALF (350)</button>
      <button class="btn-grip btn-open"  onclick="sendJoint('gripper', 430)">✋ OPEN (430)</button>
    </div>
  </div>

  <!-- ── Emergency Stop ── -->
  <button class="btn-cut" onclick="cutPower()">⚠️ EMERGENCY CUT POWER (PWM 0)</button>
  <div class="status" id="status">Studio Ready. Booted at Relax Pose (Folded Rest).</div>
</div>

<script>
let state = {
  shoulder: 480,
  elbow: 505,
  pitch: 460,
  roll: 472,
  gripper: 246
};

const limits = {
  shoulder: [180, 500],
  elbow:    [85, 505],
  pitch:    [95, 460],
  roll:     [100, 472],
  gripper:  [180, 450]
};

function setStatus(msg){ document.getElementById('status').innerText = msg; }

function showVal(j, v){
  document.getElementById('val-' + j).innerText = v;
}

function updateUI(j, v){
  state[j] = parseInt(v);
  document.getElementById('val-' + j).innerText = state[j];
  document.getElementById('slide-' + j).value = state[j];
}

function sendJoint(j, pulse){
  pulse = parseInt(pulse);
  let [minP, maxP] = limits[j];
  if(pulse < minP) pulse = minP;
  if(pulse > maxP) pulse = maxP;
  updateUI(j, pulse);

  setStatus(j + ' holding at: ' + pulse);
  fetch('/set?joint=' + j + '&pulse=' + pulse)
    .then(r => r.text())
    .then(m => setStatus(m))
    .catch(() => setStatus('Network Error'));
}

function stepJoint(j, delta){
  let target = state[j] + delta;
  sendJoint(j, target);
}

function nudgeBase(dir, ms){
  setStatus('Base turning ' + dir + ' for ' + ms + 'ms');
  fetch('/base?dir=' + dir + '&ms=' + ms)
    .then(r => r.text())
    .then(m => setStatus(m))
    .catch(() => setStatus('Network Error'));
}

function sendPose(name){
  setStatus('Gliding to ' + name.toUpperCase() + ' pose...');
  fetch('/pose?name=' + name)
    .then(r => r.json())
    .then(d => {
      updateUI('shoulder', d.shoulder);
      updateUI('elbow',    d.elbow);
      updateUI('pitch',    d.pitch);
      updateUI('roll',     d.roll);
      updateUI('gripper',  d.gripper);
      setStatus('Pose ' + name.toUpperCase() + ' active.');
    })
    .catch(() => setStatus('Network Error on Pose'));
}

function cutPower(){
  fetch('/cut').then(() => setStatus('⚠️ All PWM Cut! Motor Relaxed.'));
}

// ─────────────────────────────────────────
//  Voice Recognition Engine (Web Speech API)
// ─────────────────────────────────────────
let recognition = null;
let isVoiceActive = false;      // User intent
let isEngineRunning = false;    // Actual browser state
let isRoboActive = false;
let roboActiveTimer = null;
let restartTimer = null;

function speak(text) {
  if ('speechSynthesis' in window) {
    let u = new SpeechSynthesisUtterance(text);
    u.rate = 1.05;
    u.pitch = 1.0;
    window.speechSynthesis.speak(u);
  }
}

function initVoice() {
  const SpeechRec = window.SpeechRecognition || window.webkitSpeechRecognition;
  if (!SpeechRec) {
    document.getElementById('transcriptText').innerText = "⚠️ Speech API not available. Use Chrome or Edge.";
    alert("SpeechRecognition API is not available on this browser/connection. On mobile Chrome, HTTP websites restrict microphone access.");
    return false;
  }
  if (recognition) return true;

  try {
    recognition = new SpeechRec();
    recognition.continuous = true;
    recognition.interimResults = true;
    recognition.lang = 'en-IN';

    recognition.onstart = function() {
      isEngineRunning = true;
      document.getElementById('voiceToggle').className = "voice-toggle-btn v-btn-stop";
      document.getElementById('voiceToggle').innerText = "🛑 Stop Voice";
      setVoiceBadge('listening');
      document.getElementById('transcriptText').innerText = "🎙️ Listening... Say 'ROBO' + Command";
    };

    recognition.onerror = function(event) {
      console.warn("Speech Error:", event.error);
      if (event.error === 'no-speech') return; // Ignore silent pauses
      let errMsg = event.error;
      if (errMsg === 'not-allowed') {
        errMsg = "Mic Blocked on HTTP (allow in chrome://flags)";
      } else if (errMsg === 'network') {
        isVoiceActive = false; // Stop loop!
        clearTimeout(restartTimer);
        document.getElementById('voiceToggle').className = "voice-toggle-btn v-btn-start";
        document.getElementById('voiceToggle').innerText = "🎙️ Enable Voice";
        setVoiceBadge('off');
        document.getElementById('transcriptText').innerHTML = 
          `<div style="background:rgba(239,68,68,0.18);border:1px solid #ef4444;border-radius:12px;padding:10px;text-align:left;margin:4px 0;">
            <div style="color:#f87171;font-weight:800;font-size:0.88rem;margin-bottom:4px;">⚠️ Network Offline (Internet Required)</div>
            <div style="font-size:0.72rem;color:#e2e8f0;line-height:1.4;">
              Chrome ka Speech Recognition Google server se connect hota hai, isliye Internet chahiye!<br/><br/>
              <b>Solution:</b><br/>
              • <b>Tareeqa 1 (Best):</b> Phone ko ghar ke Wi-Fi <b>"Yash"</b> se connect karein aur <code>http://roboarm.local</code> kholein.<br/>
              • <b>Tareeqa 2:</b> Wi-Fi ke saath-saath <b>Mobile Data (Internet)</b> bhi ON rakhein.<br/>
              • <b>Tareeqa 3:</b> Phone Settings &gt; Google &gt; Voice &gt; Offline speech download karein.
            </div>
            <button onclick="toggleVoice()" style="margin-top:8px;width:100%;padding:7px;background:#38bdf8;border:none;border-radius:8px;color:#020617;font-weight:700;cursor:pointer;font-size:0.78rem;">🔄 Try Again / Retry</button>
          </div>`;
        setStatus("Voice: Network Offline");
        return;
      }
      document.getElementById('transcriptText').innerHTML = "<span style='color:#ef4444;font-weight:700;'>⚠️ " + errMsg + "</span>";
      setStatus("Voice: " + errMsg);
    };

    recognition.onend = function() {
      isEngineRunning = false;
      clearTimeout(restartTimer);
      if (isVoiceActive) {
        // Automatically restart if user still wants voice active
        restartTimer = setTimeout(() => {
          if (isVoiceActive && !isEngineRunning) {
            try { recognition.start(); } catch(e){}
          }
        }, 150);
      } else {
        document.getElementById('voiceToggle').className = "voice-toggle-btn v-btn-start";
        document.getElementById('voiceToggle').innerText = "🎙️ Enable Voice";
        setVoiceBadge('off');
      }
    };

    recognition.onresult = function(event) {
      let interimStr = "";
      let finalStr = "";
      for (let i = event.resultIndex; i < event.results.length; i++) {
        let t = event.results[i][0].transcript;
        if (event.results[i].isFinal) finalStr += t;
        else interimStr += t;
      }
      let spoken = (finalStr.length > 0 ? finalStr : interimStr).toLowerCase().trim();
      document.getElementById('transcriptText').innerText = '"' + spoken + '"';
      processSpokenText(spoken);
    };
    return true;
  } catch(err) {
    document.getElementById('transcriptText').innerText = "Init Failed: " + err.message;
    return false;
  }
}

function setVoiceBadge(st, detail) {
  let b = document.getElementById('voiceBadge');
  if (st === 'off') {
    b.className = "voice-badge v-idle";
    b.innerText = "💤 Voice Off (Tap Enable)";
  } else if (st === 'listening') {
    b.className = "voice-badge v-listen";
    b.innerText = "👂 Listening... Say 'ROBO'";
  } else if (st === 'active') {
    b.className = "voice-badge v-active";
    b.innerText = "🟢 Ready! Say Command (" + (detail || "Waiting...") + ")";
  }
}

function triggerRoboAwake() {
  isRoboActive = true;
  setVoiceBadge('active', 'Active');
  speak("Yes Boss?");
  clearTimeout(roboActiveTimer);
  roboActiveTimer = setTimeout(() => {
    isRoboActive = false;
    setVoiceBadge('listening');
  }, 9000);
}

function processSpokenText(text) {
  if (!text || text.length === 0) return;

  // If user says "robo" or "wake", awake the robot!
  if (text.includes('robo') || text.includes('robot') || text.includes('wake') || text.includes('utho')) {
    triggerRoboAwake();
    let clean = text.replace(/robo|robot|jarvis|wake up|wake/g, '').trim();
    if (clean.length > 0) {
      executeVoiceAction(clean);
    } else {
      executeVoiceAction('wake');
    }
    return;
  }

  // Execute ANY valid command (reverse, front, grab, open, up, down, etc.) IMMEDIATELY!
  executeVoiceAction(text);
}

function executeVoiceAction(phrase) {
  let act = null;
  let reply = "";

  if (phrase.includes('wake') || phrase.includes('ready') || phrase.includes('utho')) {
    act = 'wake'; reply = "Waking up";
  } else if (phrase.includes('sleep') || phrase.includes('park') || phrase.includes('rest') || phrase.includes('so jao')) {
    act = 'sleep'; reply = "Sleeping";
  } else if (phrase.includes('base left') || (phrase.includes('left') && !phrase.includes('roll'))) {
    act = 'base_left'; reply = "Left";
  } else if (phrase.includes('base right') || (phrase.includes('right') && !phrase.includes('roll'))) {
    act = 'base_right'; reply = "Right";
  } else if (phrase.includes('shoulder front') || phrase.includes('shoulder forward') || phrase.includes('forward') || phrase.includes('front') || phrase.includes('push') || phrase.includes('aage')) {
    act = 'shoulder_front'; reply = "Shoulder front";
  } else if (phrase.includes('shoulder reverse') || phrase.includes('reverse') || phrase.includes('shoulder pull') || phrase.includes('pull') || phrase.includes('shoulder bag') || phrase.includes('shoulder pack') || phrase.includes('shoulder back') || phrase.includes('back') || phrase.includes('peeche')) {
    act = 'shoulder_back'; reply = "Shoulder reverse";
  } else if (phrase.includes('elbow down') || phrase.includes('elbow lower') || phrase.includes('lower') || phrase.includes('haath neeche')) {
    act = 'elbow_down'; reply = "Elbow down";
  } else if (phrase.includes('elbow up') || phrase.includes('elbow lift') || phrase.includes('lift') || phrase.includes('haath upar')) {
    act = 'elbow_up'; reply = "Elbow up";
  } else if (phrase.includes('wrist up') || phrase.includes('pitch up') || phrase.includes('patch up') || phrase === 'up' || phrase.endsWith(' up') || phrase.includes('upar')) {
    act = 'wrist_up'; reply = "Wrist up";
  } else if (phrase.includes('wrist down') || phrase.includes('pitch down') || phrase.includes('patch down') || phrase === 'down' || phrase.endsWith(' down') || phrase.includes('neeche')) {
    act = 'wrist_down'; reply = "Wrist down";
  } else if (phrase.includes('roll left')) {
    act = 'roll_left'; reply = "Roll left";
  } else if (phrase.includes('roll right')) {
    act = 'roll_right'; reply = "Roll right";
  } else if (phrase.includes('grab') || phrase.includes('close') || phrase.includes('catch') || phrase.includes('pakad')) {
    act = 'grab'; reply = "Closing gripper";
  } else if (phrase.includes('open') || phrase.includes('release') || phrase.includes('khol')) {
    act = 'open'; reply = "Opening gripper";
  } else if (phrase.includes('stop') || phrase.includes('halt') || phrase.includes('freeze') || phrase.includes('ruko')) {
    act = 'stop'; reply = "Stopped";
  }

  if (act) {
    setStatus('Voice: ' + reply);
    speak(reply);
    fetch('/cmd?action=' + act)
      .then(r => r.json())
      .then(d => {
        if (d.shoulder) {
          updateUI('shoulder', d.shoulder);
          updateUI('elbow',    d.elbow);
          updateUI('pitch',    d.pitch);
          updateUI('roll',     d.roll);
          updateUI('gripper',  d.gripper);
        }
        setStatus('Voice Executed: ' + d.msg);
      })
      .catch(() => setStatus('Voice Command Error'));

    if (isRoboActive) {
      clearTimeout(roboActiveTimer);
      roboActiveTimer = setTimeout(() => {
        isRoboActive = false;
        setVoiceBadge('listening');
      }, 7000);
    }
  }
}

function toggleVoice() {
  if (!recognition) {
    let ok = initVoice();
    if (!ok) return;
  }

  if (!isVoiceActive) {
    // Turn Voice ON
    isVoiceActive = true;
    clearTimeout(restartTimer);
    document.getElementById('voiceToggle').className = "voice-toggle-btn v-btn-stop";
    document.getElementById('voiceToggle').innerText = "🛑 Stop Voice";
    setVoiceBadge('listening');

    if (!isEngineRunning) {
      try {
        recognition.start();
      } catch(e) {
        console.warn("Recognition start catch:", e);
        try { recognition.abort(); } catch(err){}
        setTimeout(() => {
          if (isVoiceActive && !isEngineRunning) {
            try { recognition.start(); } catch(err2){}
          }
        }, 200);
      }
    }
  } else {
    // Turn Voice OFF
    isVoiceActive = false;
    isRoboActive = false;
    clearTimeout(roboActiveTimer);
    clearTimeout(restartTimer);

    document.getElementById('voiceToggle').className = "voice-toggle-btn v-btn-start";
    document.getElementById('voiceToggle').innerText = "🎙️ Enable Voice";
    setVoiceBadge('off');
    document.getElementById('transcriptText').innerText = "Voice disabled. Tap Enable to turn back on.";

    try {
      recognition.abort();
    } catch(e) {
      try { recognition.stop(); } catch(err){}
    }
  }
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
  if (server.hasArg("joint") && server.hasArg("pulse")) {
    String j = server.arg("joint");
    int p    = server.arg("pulse").toInt();

    if (j == "shoulder")      smoothJointTo(SHOULDER_CH, curShoulder, p, SHOULDER_MIN, SHOULDER_MAX, 8);
    else if (j == "elbow")    smoothJointTo(ELBOW_CH,    curElbow,    p, ELBOW_MIN,    ELBOW_MAX,    8);
    else if (j == "roll")     smoothJointTo(ROLL_CH,     curRoll,     p, ROLL_MIN,     ROLL_MAX,     8);
    else if (j == "pitch")    smoothJointTo(PITCH_CH,    curPitch,    p, PITCH_MIN,    PITCH_MAX,    8);
    else if (j == "gripper")  setGripper(p); // Gripper remains fast!

    server.send(200, "text/plain", j + " locked & held at " + String(p));
  } else {
    server.send(400, "text/plain", "Missing args");
  }
}

void handleBase() {
  if (server.hasArg("dir") && server.hasArg("ms")) {
    String dir = server.arg("dir");
    int ms     = server.arg("ms").toInt();
    int delta  = (dir == "left") ? -BASE_SPEED_STEP : BASE_SPEED_STEP;
    smoothBaseNudge(delta, ms);
    server.send(200, "text/plain", "Base turned " + dir);
  } else {
    baseStop();
    server.send(200, "text/plain", "Base stopped");
  }
}

void handlePose() {
  if (server.hasArg("name")) {
    String name = server.arg("name");
    
    // Preset Poses with calibrated values
    if (name == "wake" || name == "home") {
      // Elevated Ready Working Stance
      smoothMoveTo(WORK_SHOULDER, WORK_ELBOW, WORK_ROLL, WORK_PITCH, WORK_GRIPPER, 12);
    } 
    else if (name == "relax" || name == "sleep" || name == "rest") {
      // Official Folded Low-Torque Relax Pose
      smoothMoveTo(RELAX_SHOULDER, RELAX_ELBOW, RELAX_ROLL, RELAX_PITCH, RELAX_GRIPPER, 16);
    }
    else if (name == "reach") {
      // Forward reach
      smoothMoveTo(260, 420, WORK_ROLL, 200, GRIPPER_OPEN, 12);
    } 
    else if (name == "pick") {
      // Lower ground pick stance
      smoothMoveTo(220, 200, WORK_ROLL, 140, GRIPPER_CLOSE, 12);
    }

    String json = "{\"shoulder\":" + String(curShoulder) +
                  ",\"elbow\":"    + String(curElbow) +
                  ",\"roll\":"     + String(curRoll) +
                  ",\"pitch\":"    + String(curPitch) +
                  ",\"gripper\":"  + String(curGripper) + "}";
    server.send(200, "application/json", json);
  } else {
    server.send(400, "text/plain", "Missing pose name");
  }
}

void handleCommand() {
  if (server.hasArg("action")) {
    String act = server.arg("action");
    act.toLowerCase();
    String msg = "";

    if (act == "wake" || act == "wakeup") {
      smoothMoveTo(WORK_SHOULDER, WORK_ELBOW, WORK_ROLL, WORK_PITCH, WORK_GRIPPER, 12);
      msg = "Awake Ready";
    }
    else if (act == "sleep" || act == "park" || act == "rest") {
      smoothMoveTo(RELAX_SHOULDER, RELAX_ELBOW, RELAX_ROLL, RELAX_PITCH, RELAX_GRIPPER, 16);
      msg = "Sleeping / Relaxed";
    }
    else if (act == "base_left" || act == "left") {
      smoothBaseNudge(-BASE_SPEED_STEP, 300);
      msg = "Base Left";
    }
    else if (act == "base_right" || act == "right") {
      smoothBaseNudge(BASE_SPEED_STEP, 300);
      msg = "Base Right";
    }
    else if (act == "shoulder_front" || act == "shoulder_forward" || act == "forward" || act == "front" || act == "push") {
      smoothJointTo(SHOULDER_CH, curShoulder, curShoulder - 28, SHOULDER_MIN, SHOULDER_MAX, 14);
      msg = "Shoulder Front (" + String(curShoulder) + ")";
    }
    else if (act == "shoulder_back" || act == "shoulder_reverse" || act == "reverse" || act == "pull" || act == "shoulder_pull" || act == "shoulder_bag" || act == "shoulder_pack" || act == "bag") {
      smoothJointTo(SHOULDER_CH, curShoulder, curShoulder + 28, SHOULDER_MIN, SHOULDER_MAX, 14);
      msg = "Shoulder Reverse (" + String(curShoulder) + ")";
    }
    else if (act == "elbow_down" || act == "elbow_lower" || act == "lower") {
      smoothJointTo(ELBOW_CH, curElbow, curElbow + 28, ELBOW_MIN, ELBOW_MAX, 14); // Inverted to move down physically
      msg = "Elbow Down (" + String(curElbow) + ")";
    }
    else if (act == "elbow_up" || act == "elbow_lift" || act == "lift") {
      smoothJointTo(ELBOW_CH, curElbow, curElbow - 28, ELBOW_MIN, ELBOW_MAX, 14); // Inverted to move up physically
      msg = "Elbow Up (" + String(curElbow) + ")";
    }
    else if (act == "wrist_up" || act == "pitch_up" || act == "up" || act == "patch_up") {
      smoothJointTo(PITCH_CH, curPitch, curPitch - 28, PITCH_MIN, PITCH_MAX, 14); // Inverted to move up physically
      msg = "Wrist Up (" + String(curPitch) + ")";
    }
    else if (act == "wrist_down" || act == "pitch_down" || act == "down" || act == "patch_down") {
      smoothJointTo(PITCH_CH, curPitch, curPitch + 28, PITCH_MIN, PITCH_MAX, 14); // Inverted to move down physically
      msg = "Wrist Down (" + String(curPitch) + ")";
    }
    else if (act == "roll_left") {
      smoothJointTo(ROLL_CH, curRoll, curRoll - 30, ROLL_MIN, ROLL_MAX, 14);
      msg = "Roll Left (" + String(curRoll) + ")";
    }
    else if (act == "roll_right") {
      smoothJointTo(ROLL_CH, curRoll, curRoll + 30, ROLL_MIN, ROLL_MAX, 14);
      msg = "Roll Right (" + String(curRoll) + ")";
    }
    else if (act == "grab" || act == "close" || act == "grip") {
      setGripper(GRIPPER_CLOSE);
      msg = "Gripper Closed (" + String(GRIPPER_CLOSE) + ")";
    }
    else if (act == "open" || act == "release") {
      setGripper(GRIPPER_OPEN);
      msg = "Gripper Opened (" + String(GRIPPER_OPEN) + ")";
    }
    else if (act == "stop" || act == "halt") {
      baseStop();
      msg = "Halted";
    }
    else {
      server.send(400, "text/plain", "Unknown action");
      return;
    }

    String json = "{\"status\":\"ok\",\"msg\":\"" + msg + "\"" +
                  ",\"shoulder\":" + String(curShoulder) +
                  ",\"elbow\":"    + String(curElbow) +
                  ",\"roll\":"     + String(curRoll) +
                  ",\"pitch\":"    + String(curPitch) +
                  ",\"gripper\":"  + String(curGripper) + "}";
    server.send(200, "application/json", json);
  } else {
    server.send(400, "text/plain", "Missing action");
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
  Serial.println("\n=== RoboArm 5-DOF Master Studio (180° Positional Upgrade) ===");

  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(SERVO_FREQ);
  delay(50);

  // Initialize Base 360 at neutral 290
  baseStop();
  delay(50);

  // Soft staggered power-up into Relax Pose (prevents inrush current spikes)
  setGripper(curGripper);
  delay(60);
  setRoll(curRoll);
  delay(60);
  setPitch(curPitch);
  delay(60);
  setElbow(curElbow);
  delay(60);
  setShoulder(curShoulder);

  // Dual-mode Wi-Fi: Standalone AP (Direct connection) + Station (Home Wi-Fi for Internet)
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("\n[AP Mode]  SSID: %s | URL: http://%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  WiFi.begin(HOME_SSID, HOME_PASS);
  Serial.printf("[STA Mode] Connecting to Home Wi-Fi '%s'...", HOME_SSID);
  unsigned long startWifi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWifi < 7000) {
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[STA Mode] Connected! IP: http://%s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[STA Mode] Home Wi-Fi not found or not connected. Using Standalone AP only.");
  }

  if (MDNS.begin("roboarm")) {
    Serial.println("[mDNS] Responder active at http://roboarm.local");
  }

  server.on("/",     handleRoot);
  server.on("/set",  handleSet);
  server.on("/base", handleBase);
  server.on("/pose", handlePose);
  server.on("/cmd",  handleCommand);
  server.on("/cut",  handleCut);
  server.begin();

  Serial.println("RoboArm Master Studio is LIVE!");
  Serial.printf("Direct AP: http://192.168.4.1/  |  Local Name: http://roboarm.local\n");
}

void loop() {
  server.handleClient();
}
