#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <driver/i2s.h>
#include "web_studio.h"

// ─────────────────────────────────────────────────────────────
//  Wi-Fi Dual Mode: Home Wi-Fi (STA) + ESP32's 2.4GHz Hotspot (AP)
// ─────────────────────────────────────────────────────────────
const char* HOME_SSID = "YOUR_WIFI_SSID";
const char* HOME_PASS = "YOUR_WIFI_PASSWORD";  // Set your Wi-Fi password or leave empty ("") if open network

const char* AP_SSID   = "RoboArm-AP";
const char* AP_PASS   = "12345678";

// Target phone server IP
// Default on RoboArm-AP is 192.168.4.2
String phoneServerIP = "192.168.4.2";
const int PHONE_SERVER_PORT = 5000;

// ─────────────────────────────────────────────────────────────
//  PCA9685 Config & Calibrated Joint Profiles
// ─────────────────────────────────────────────────────────────
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
#define SERVO_FREQ 50

// Channel Map
const int BASE_CH               = 0;   // MG996R 360° Continuous Rotation
const int BASE_STOP             = 290; // Calibrated dead stop at 27MHz oscillator
const int BASE_SPEED_STEP       = 18;  // Buttery soft industrial turn
const int BASE_NUDGE_MS         = 700; // ~16°-18° slow turn
const int MICRO_BASE_SPEED_STEP = 13;  // Whisper-quiet micro turn
const int MICRO_BASE_NUDGE_MS   = 280; // ~5°-6° micro nudge

const int JOINT_STEP            = 30;  // Normal step (~14°-15°), ultra smooth
const int MICRO_JOINT_STEP      = 12;  // Micro precision step (~5.5°-6.5°)

const int SHOULDER_CH     = 2;   // MG996R 180° Positional
const int SHOULDER_MIN    = 180;
const int SHOULDER_MAX    = 500;

const int ELBOW_CH        = 4;   // MG996R 180° Positional
const int ELBOW_MIN       = 85;
const int ELBOW_MAX       = 505;

const int ROLL_CH         = 6;   // MG90S 180° Positional
const int ROLL_MIN        = 100;
const int ROLL_MAX        = 472;

const int PITCH_CH        = 8;   // MG90S 180° Positional
const int PITCH_MIN       = 95;
const int PITCH_MAX       = 460;

const int GRIPPER_CH      = 10;  // MG90S 180° Positional
const int GRIPPER_MIN     = 180;
const int GRIPPER_MAX     = 500;
const int GRIPPER_CLOSE   = 246; // Calibrated close (no stall, no heating)
const int GRIPPER_OPEN    = 485; // Calibrated 100% full wide open aperture

// ─────────────────────────────────────────────────────────────
//  Calibrated Master Poses
// ─────────────────────────────────────────────────────────────
// 1. SLEEP / RELAX POSE (Folded, Low Load, Safe Rest)
const int RELAX_SHOULDER = 480;
const int RELAX_ELBOW    = 505;
const int RELAX_ROLL     = 472;
const int RELAX_PITCH    = 460;
const int RELAX_GRIPPER  = 246;

// 2. WORKING / WAKE READY POSE (Active Cobot Working Stance)
const int WORK_SHOULDER  = 410;
const int WORK_ELBOW     = 320;
const int WORK_ROLL      = 100;
const int WORK_PITCH     = 95;
const int WORK_GRIPPER   = 246;

// Live Joint States (Boot at RELAX pose)
int curShoulder = RELAX_SHOULDER;
int curElbow    = RELAX_ELBOW;
int curRoll     = RELAX_ROLL;
int curPitch    = RELAX_PITCH;
int curGripper  = RELAX_GRIPPER;

// ─────────────────────────────────────────────────────────────
//  Industrial Cobot State Machine & 60s (1 Minute) Window
// ─────────────────────────────────────────────────────────────
enum RobotState {
  STATE_SLEEP,
  STATE_AWAKE
};

RobotState robotState = STATE_SLEEP;
unsigned long awakeTimerEnd = 0;
const unsigned long AWAKE_WINDOW_MS = 60000; // 60-second (1 min) sliding voice window

unsigned long lastMotionTime = 0;
const unsigned long SERVO_COOLDOWN_MS = 1000; // Brief 1s quiet window after servo motion
bool isMoving = false;

// When using Phone Offline Voice in Chrome, onboard hardware mic recording to Python
// blocks loop() for 2.5s record + 3.5s POST timeout, causing a 4-6 second freeze!
// Setting this to false guarantees instantaneous sub-10ms response to Phone Voice commands!
const bool ENABLE_ONBOARD_MIC_STREAM = false;

// ─────────────────────────────────────────────────────────────
//  INMP441 Microphone (I2S Config)
// ─────────────────────────────────────────────────────────────
#define I2S_WS          25   // Word Select (LRCLK)
#define I2S_SCK         26   // Continuous Serial Clock (BCLK)
#define I2S_SD          34   // Serial Data Input (DIN)
#define I2S_PORT        I2S_NUM_0

#define SAMPLE_RATE     16000
#define RECORD_SECONDS  2.5
#define TOTAL_SAMPLES   ((int)(SAMPLE_RATE * RECORD_SECONDS)) // 40,000 samples
#define WAV_DATA_SIZE   (TOTAL_SAMPLES * sizeof(int16_t))     // 80,000 bytes
#define WAV_HEADER_SIZE 44
#define WAV_TOTAL_SIZE  (WAV_HEADER_SIZE + WAV_DATA_SIZE)     // 80,044 bytes

float ambientNoiseRMS = 120.0f;
float triggerThreshold = 350.0f;

uint8_t* wavBuffer = nullptr;
WebServer server(80);

// ─────────────────────────────────────────────────────────────
//  3-Buzzer Polyphonic Chime & Limit Alert System
// ─────────────────────────────────────────────────────────────
#define BUZZER_1_PIN    27  // High-tone / Lead Buzzer
#define BUZZER_2_PIN    14  // Mid-tone / Harmony Buzzer
#define BUZZER_3_PIN    13  // Low-tone / Bass & Alert Buzzer

void stopAllBuzzers() {
  noTone(BUZZER_1_PIN);
  noTone(BUZZER_2_PIN);
  noTone(BUZZER_3_PIN);
  digitalWrite(BUZZER_1_PIN, LOW);
  digitalWrite(BUZZER_2_PIN, LOW);
  digitalWrite(BUZZER_3_PIN, LOW);
}

// 1. Command Acknowledged / Start Tune (~0.5s)
void playStartTune() {
  tone(BUZZER_3_PIN, 523); // C5
  delay(80);
  noTone(BUZZER_3_PIN);

  tone(BUZZER_2_PIN, 659); // E5
  delay(80);
  noTone(BUZZER_2_PIN);

  tone(BUZZER_1_PIN, 880); // A5
  delay(100);
  noTone(BUZZER_1_PIN);

  // Harmonized chord
  tone(BUZZER_1_PIN, 1046); // C6
  tone(BUZZER_2_PIN, 784);  // G5
  tone(BUZZER_3_PIN, 523);  // C5
  delay(200);
  stopAllBuzzers();
}

// 2. Motion Completed / Work Done Tune (~0.5s)
void playDoneTune() {
  tone(BUZZER_1_PIN, 988); // B5
  delay(90);
  noTone(BUZZER_1_PIN);

  tone(BUZZER_2_PIN, 784); // G5
  delay(90);
  noTone(BUZZER_2_PIN);

  // Resolving major chord
  tone(BUZZER_1_PIN, 1046); // C6
  tone(BUZZER_2_PIN, 659);  // E5
  tone(BUZZER_3_PIN, 523);  // C5
  delay(220);
  stopAllBuzzers();
}

// 3. Limit Reached Warning Alarm (~1.4s)
void playLimitAlert() {
  Serial.println("🚨 [LIMIT ALERT] Joint reached software endstop! Movement blocked.");
  for (int i = 0; i < 4; i++) {
    tone(BUZZER_1_PIN, 1800);
    tone(BUZZER_2_PIN, 1200);
    tone(BUZZER_3_PIN, 600);
    delay(180);
    stopAllBuzzers();
    delay(120);
  }
  stopAllBuzzers();
}

// ─────────────────────────────────────────────────────────────
//  Ultra-Smooth Industrial S-Curve Motion Primitives
// ─────────────────────────────────────────────────────────────
void baseStop() {
  pwm.setPWM(BASE_CH, 0, BASE_STOP);
}

// Continuous 360 base smooth sinusoidal velocity envelope (Zero-Jerk acceleration & deceleration)
void smoothBaseNudge(int peakDelta, int durationMs = 650) {
  isMoving = true;
  const int steps = 32;
  int stepDelay = max(durationMs / steps, 18);

  for (int i = 1; i <= steps; i++) {
    float t = (float)i / (float)steps;
    // Sinusoidal bell speed: 0 at start -> peakDelta at 50% -> 0 at end
    float bell = sinf(t * 3.14159265f);
    int currentSpeed = round(peakDelta * bell);
    pwm.setPWM(BASE_CH, 0, BASE_STOP + currentSpeed);
    delay(stepDelay);
  }
  baseStop();
  lastMotionTime = millis();
  isMoving = false;
}

void setGripper(int p, bool autoRelax = false, int travelDelayMs = 500) {
  curGripper = constrain(p, GRIPPER_MIN, GRIPPER_MAX);
  pwm.setPWM(GRIPPER_CH, 0, curGripper);
  lastMotionTime = millis();
  if (autoRelax) {
    delay(travelDelayMs); // Allow sufficient physical travel before cutting PWM
    pwm.setPWM(GRIPPER_CH, 0, 0); // Cut PWM signal -> stops 100% of stall vibration, buzzing, and heating!
    Serial.println("✊ Gripper PWM Relaxed (No Vibration / No Stall)");
  }
}

void setShoulder(int p) {
  curShoulder = constrain(p, SHOULDER_MIN, SHOULDER_MAX);
  pwm.setPWM(SHOULDER_CH, 0, curShoulder);
  lastMotionTime = millis();
}

void setElbow(int p) {
  curElbow = constrain(p, ELBOW_MIN, ELBOW_MAX);
  pwm.setPWM(ELBOW_CH, 0, curElbow);
  lastMotionTime = millis();
}

void setRoll(int p) {
  curRoll = constrain(p, ROLL_MIN, ROLL_MAX);
  pwm.setPWM(ROLL_CH, 0, curRoll);
  lastMotionTime = millis();
}

void setPitch(int p) {
  curPitch = constrain(p, PITCH_MIN, PITCH_MAX);
  pwm.setPWM(PITCH_CH, 0, curPitch);
  lastMotionTime = millis();
}

// Zero-Jerk Perlin Quintic Smootherstep (6t^5 - 15t^4 + 10t^3)
// 1st & 2nd derivatives are strictly ZERO at start and finish -> Ultra-slow, buttery industrial cobot glide!
void smoothJointTo(int ch, int &currentVal, int targetVal, int minLimit, int maxLimit, int totalSteps = 90, int stepDelayMs = 24) {
  targetVal = constrain(targetVal, minLimit, maxLimit);
  if (currentVal == targetVal) return;

  isMoving = true;
  float startVal = (float)currentVal;
  float delta = (float)(targetVal - startVal);
  int lastSent = currentVal;

  for (int step = 1; step <= totalSteps; step++) {
    float t = (float)step / (float)totalSteps;
    // Quintic Smootherstep: Continuous 2nd derivative (Zero Jerk)
    float ease = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    int pwmVal = round(startVal + delta * ease);
    if (pwmVal != lastSent || step == totalSteps) {
      pwm.setPWM(ch, 0, pwmVal);
      lastSent = pwmVal;
    }
    delay(stepDelayMs);
  }
  currentVal = targetVal;
  pwm.setPWM(ch, 0, currentVal);

  lastMotionTime = millis();
  isMoving = false;
}

// Multi-axis synchronous Zero-Jerk glide for Master Poses (Wake / Sleep)
// 180 steps * 24ms = 4.32 seconds of ultra-slow, buttery cinematic glide!
void smoothMoveTo(int targetS, int targetE, int targetR, int targetP, int targetG, int totalSteps = 180, int stepDelayMs = 24) {
  targetS = constrain(targetS, SHOULDER_MIN, SHOULDER_MAX);
  targetE = constrain(targetE, ELBOW_MIN, ELBOW_MAX);
  targetR = constrain(targetR, ROLL_MIN, ROLL_MAX);
  targetP = constrain(targetP, PITCH_MIN, PITCH_MAX);
  targetG = constrain(targetG, GRIPPER_MIN, GRIPPER_MAX);

  float startS = (float)curShoulder;
  float startE = (float)curElbow;
  float startR = (float)curRoll;
  float startP = (float)curPitch;
  float startG = (float)curGripper;

  float dS = (float)(targetS - curShoulder);
  float dE = (float)(targetE - curElbow);
  float dR = (float)(targetR - curRoll);
  float dP = (float)(targetP - curPitch);
  float dG = (float)(targetG - curGripper);

  float maxDelta = max(abs(dS), max(abs(dE), max(abs(dR), max(abs(dP), abs(dG)))));
  if (maxDelta < 1.0f) return;

  isMoving = true;

  for (int step = 1; step <= totalSteps; step++) {
    float t = (float)step / (float)totalSteps;
    // Zero-Jerk Quintic Smootherstep
    float ease = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);

    curShoulder = round(startS + dS * ease);
    curElbow    = round(startE + dE * ease);
    curRoll     = round(startR + dR * ease);
    curPitch    = round(startP + dP * ease);
    curGripper  = round(startG + dG * ease);

    pwm.setPWM(SHOULDER_CH, 0, curShoulder);
    pwm.setPWM(ELBOW_CH,    0, curElbow);
    pwm.setPWM(ROLL_CH,     0, curRoll);
    pwm.setPWM(PITCH_CH,    0, curPitch);
    pwm.setPWM(GRIPPER_CH,  0, curGripper);

    delay(stepDelayMs);
  }

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

  lastMotionTime = millis();
  isMoving = false;
}

// ─────────────────────────────────────────────────────────────
//  WAV File Creation & I2S Audio Capture
// ─────────────────────────────────────────────────────────────
void writeWavHeader(uint8_t* header, int totalDataLen) {
  int byteRate = SAMPLE_RATE * 1 * sizeof(int16_t);
  int totalFileLen = totalDataLen + 36;

  header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
  header[4] = (uint8_t)(totalFileLen & 0xff);
  header[5] = (uint8_t)((totalFileLen >> 8) & 0xff);
  header[6] = (uint8_t)((totalFileLen >> 16) & 0xff);
  header[7] = (uint8_t)((totalFileLen >> 24) & 0xff);
  header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';

  header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
  header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
  header[20] = 1; header[21] = 0;
  header[22] = 1; header[23] = 0;
  header[24] = (uint8_t)(SAMPLE_RATE & 0xff);
  header[25] = (uint8_t)((SAMPLE_RATE >> 8) & 0xff);
  header[26] = (uint8_t)((SAMPLE_RATE >> 16) & 0xff);
  header[27] = (uint8_t)((SAMPLE_RATE >> 24) & 0xff);
  header[28] = (uint8_t)(byteRate & 0xff);
  header[29] = (uint8_t)((byteRate >> 8) & 0xff);
  header[30] = (uint8_t)((byteRate >> 16) & 0xff);
  header[31] = (uint8_t)((byteRate >> 24) & 0xff);
  header[32] = 2; header[33] = 0;
  header[34] = 16; header[35] = 0;

  header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
  header[40] = (uint8_t)(totalDataLen & 0xff);
  header[41] = (uint8_t)((totalDataLen >> 8) & 0xff);
  header[42] = (uint8_t)((totalDataLen >> 16) & 0xff);
  header[43] = (uint8_t)((totalDataLen >> 24) & 0xff);
}

void initI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_zero_dma_buffer(I2S_PORT);
}

float measureMicRMS(int checkSamples = 256) {
  int32_t rawBuffer[256];
  size_t bytesRead = 0;
  i2s_read(I2S_PORT, (char*)rawBuffer, sizeof(rawBuffer), &bytesRead, 80 / portTICK_PERIOD_MS);

  int samplesRead = bytesRead / sizeof(int32_t);
  if (samplesRead <= 0) return 0.0f;

  // 1. Calculate DC mean bias
  int64_t sum = 0;
  for (int i = 0; i < samplesRead; i++) {
    sum += (rawBuffer[i] >> 12);
  }
  int32_t mean = sum / samplesRead;

  // 2. Measure true AC acoustic energy (DC removed)
  int64_t sumSq = 0;
  for (int i = 0; i < samplesRead; i++) {
    int32_t ac = (rawBuffer[i] >> 12) - mean;
    sumSq += ac * ac;
  }
  return sqrtf((float)sumSq / samplesRead);
}

void recordAudio() {
  Serial.println("🔴 [RECORDING] Speak now into INMP441 (2.5 seconds)...");
  i2s_zero_dma_buffer(I2S_PORT); // Flush any stale audio/clicks
  writeWavHeader(wavBuffer, WAV_DATA_SIZE);
  int16_t* pcmSamples = (int16_t*)(wavBuffer + WAV_HEADER_SIZE);

  int samplesReadTotal = 0;
  const int CHUNK_SAMPLES = 256;
  int32_t rawChunk[CHUNK_SAMPLES];

  // DSP DC-Blocking Filter states
  float prev_x = 0.0f;
  float prev_y = 0.0f;
  const float R = 0.995f; // High-pass cutoff ~15 Hz

  while (samplesReadTotal < TOTAL_SAMPLES) {
    size_t bytesRead = 0;
    int samplesToRead = min(CHUNK_SAMPLES, TOTAL_SAMPLES - samplesReadTotal);
    i2s_read(I2S_PORT, (char*)rawChunk, samplesToRead * sizeof(int32_t), &bytesRead, portMAX_DELAY);

    int samplesActual = bytesRead / sizeof(int32_t);
    for (int i = 0; i < samplesActual; i++) {
      // INMP441 24-bit audio shifted by 13 to avoid digital clipping when speaking close
      float x = (float)(rawChunk[i] >> 13);
      float y = x - prev_x + R * prev_y;
      prev_x = x;
      prev_y = y;

      int32_t val = (int32_t)y;
      if (val > 32767) val = 32767;
      if (val < -32768) val = -32768;
      pcmSamples[samplesReadTotal++] = (int16_t)val;
    }
  }

  // Automatic Gain Control: Only boost real human voice, NEVER amplify background room silence
  int32_t maxPeak = 0;
  for (int i = 0; i < TOTAL_SAMPLES; i++) {
    int32_t v = abs(pcmSamples[i]);
    if (v > maxPeak) maxPeak = v;
  }

  // Voice threshold: Real spoken voice creates peaks > 450. Below 450 is just room hiss/fan noise.
  if (maxPeak > 450 && maxPeak < 14000) {
    float gain = 14000.0f / (float)maxPeak;
    if (gain > 4.5f) gain = 4.5f; // Max 4.5x boost
    for (int i = 0; i < TOTAL_SAMPLES; i++) {
      int32_t scaled = (int32_t)(pcmSamples[i] * gain);
      if (scaled > 32767) scaled = 32767;
      if (scaled < -32768) scaled = -32768;
      pcmSamples[i] = (int16_t)scaled;
    }
    Serial.printf("🔊 AGC: Voice Peak %d -> Boosted x%.1f\n", maxPeak, gain);
  } else {
    Serial.printf("🔊 Audio Peak: %d (Silence/Background, no boost)\n", maxPeak);
  }

  Serial.println("⏹️ [RECORDING DONE] 2.5 seconds audio captured!");
}

// ─────────────────────────────────────────────────────────────
//  Send Audio to Phone (Termux Whisper Server)
// ─────────────────────────────────────────────────────────────
bool tryPostAudioTo(String ip) {
  String targetUrl = "http://" + ip + ":" + String(PHONE_SERVER_PORT) + "/audio";
  Serial.println("📱 Streaming audio to Phone: " + targetUrl);

  HTTPClient http;
  http.begin(targetUrl);
  http.setTimeout(3500);
  http.addHeader("Content-Type", "audio/wav");

  int httpCode = http.POST(wavBuffer, WAV_TOTAL_SIZE);
  bool success = false;
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.printf("✅ Phone Response [%d]: %s\n", httpCode, payload.c_str());
    success = true;
  } else {
    Serial.printf("⚠️ POST to %s failed! (Code: %d, %s)\n", ip.c_str(), httpCode, http.errorToString(httpCode).c_str());
  }
  http.end();
  return success;
}

void sendAudioToPhone() {
  bool netReady = (WiFi.status() == WL_CONNECTED) || (WiFi.getMode() & WIFI_AP);
  if (!netReady) {
    Serial.println("❌ No Wi-Fi or AP active!");
    return;
  }

  // 1. Try currently configured phoneServerIP
  if (tryPostAudioTo(phoneServerIP)) {
    return;
  }

  // 2. If connected to phone's hotspot, try Gateway IP
  if (WiFi.status() == WL_CONNECTED) {
    String gw = WiFi.gatewayIP().toString();
    if (gw != "0.0.0.0" && gw != phoneServerIP) {
      Serial.println("🔄 Retrying on Phone Hotspot Gateway: " + gw + "...");
      if (tryPostAudioTo(gw)) {
        phoneServerIP = gw;
        return;
      }
    }
  }

  // 3. If in AP mode and phone connected to RoboArm-AP
  if (phoneServerIP != "192.168.4.2") {
    Serial.println("🔄 Retrying on Hotspot default IP: 192.168.4.2...");
    if (tryPostAudioTo("192.168.4.2")) {
      phoneServerIP = "192.168.4.2";
      return;
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  HTTP Command Handler (Executed when phone calls back)
// ─────────────────────────────────────────────────────────────
void handleCmd() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");

  // Auto-learn phone IP from incoming request
  IPAddress callerIP = server.client().remoteIP();
  if (callerIP.toString() != "0.0.0.0") {
    phoneServerIP = callerIP.toString();
  }

  if (!server.hasArg("action")) {
    server.send(400, "text/plain", "Missing action");
    return;
  }
  String action = server.arg("action");
  action.toLowerCase();
  action.trim();
  Serial.println("⚡ Incoming Action: " + action);

  // ── WAKE COMMAND (Only action allowed from Sleep) ──
  if (action == "wake") {
    server.send(200, "text/plain", "Waking up"); // Reply immediately so phone doesn't time out
    Serial.println("\n✨ [WAKE COMMAND] Waking up with smooth industrial S-curve...");
    playStartTune();
    smoothMoveTo(WORK_SHOULDER, WORK_ELBOW, WORK_ROLL, WORK_PITCH, WORK_GRIPPER);
    playDoneTune();
    robotState = STATE_AWAKE;
    awakeTimerEnd = millis() + AWAKE_WINDOW_MS;
    Serial.println("🟢 [ROBO IS AWAKE] 1-Minute Active Voice Window Opened! Ready for commands...\n");
    return;
  }

  // ── Auto-Wake if arm is Asleep ──
  if (robotState != STATE_AWAKE) {
    if (action != "sleep") {
      Serial.println("⚡ [AUTO-WAKE] Arm is Asleep. Auto-waking up before executing: " + action);
      playStartTune();
      smoothMoveTo(WORK_SHOULDER, WORK_ELBOW, WORK_ROLL, WORK_PITCH, WORK_GRIPPER);
      playDoneTune();
      robotState = STATE_AWAKE;
      awakeTimerEnd = millis() + AWAKE_WINDOW_MS;
      delay(20);
    } else {
      server.send(200, "text/plain", "Already asleep");
      return;
    }
  }

  // ── ACTIVE STATE: Execute requested command ──
  if (action == "sleep") {
    server.send(200, "text/plain", "Going to sleep"); // Reply immediately
    Serial.println("💤 Command: SLEEP -> Returning smoothly to Sleep Pose...");
    playStartTune();
    smoothMoveTo(RELAX_SHOULDER, RELAX_ELBOW, RELAX_ROLL, RELAX_PITCH, RELAX_GRIPPER);
    playDoneTune();
    robotState = STATE_SLEEP;
    Serial.println("🔒 [ARM ASLEEP] Resting in Sleep Pose. Say 'ROBO' to wake up.\n");
    return;
  }

  // ── GRIPPER (Smart Instant Grip: Clamps firmly, then cuts PWM to eliminate stall vibration & heating) ──
  // Gripper and Base are EXCLUDED from limit alert per user instruction
  else if (action == "grab" || action == "close") {
    server.send(200, "text/plain", "Grabbed");
    playStartTune();
    setGripper(GRIPPER_CLOSE, true, 450); // Instant fast clamp + auto-cut PWM to silence servo motor!
    playDoneTune();
  }
  else if (action == "open" || action == "release") {
    server.send(200, "text/plain", "Opened");
    playStartTune();
    setGripper(GRIPPER_OPEN, true, 850);  // 100% full wide open aperture + auto-relax
    playDoneTune();
  }

  // ── BASE ROTATION (CH 0 - 360° Continuous: Clockwise & Anticlockwise) ──
  // Inverted per user confirmation: anticlockwise -> (+), clockwise -> (-)
  else if (action == "anticlockwise" || action == "anti_clockwise" || action == "ccw" || action == "base_anticlockwise" || action == "base_left" || action == "left") {
    server.send(200, "text/plain", "Anticlockwise");
    playStartTune();
    smoothBaseNudge(BASE_SPEED_STEP, BASE_NUDGE_MS);
    playDoneTune();
  }
  else if (action == "clockwise" || action == "cw" || action == "base_clockwise" || action == "base_right" || action == "right") {
    server.send(200, "text/plain", "Clockwise");
    playStartTune();
    smoothBaseNudge(-BASE_SPEED_STEP, BASE_NUDGE_MS);
    playDoneTune();
  }
  else if (action == "micro_anticlockwise" || action == "micro_ccw" || action == "micro_base_left" || action == "micro_left") {
    server.send(200, "text/plain", "Micro Anticlockwise (5°)");
    playStartTune();
    smoothBaseNudge(MICRO_BASE_SPEED_STEP, MICRO_BASE_NUDGE_MS);
    playDoneTune();
  }
  else if (action == "micro_clockwise" || action == "micro_cw" || action == "micro_base_right" || action == "micro_right") {
    server.send(200, "text/plain", "Micro Clockwise (5°)");
    playStartTune();
    smoothBaseNudge(-MICRO_BASE_SPEED_STEP, MICRO_BASE_NUDGE_MS);
    playDoneTune();
  }

  // ── SHOULDER (CH 2 - Forward Reach & Pull Back / Reverse / Rivers) ──
  // Limit Check: SHOULDER_MIN = 180 (Frontmost), SHOULDER_MAX = 500 (Backmost)
  else if (action == "shoulder_front" || action == "shoulder_forward" || action == "forward" || action == "front" || action == "push") {
    server.send(200, "text/plain", "Shoulder Front");
    if (curShoulder <= SHOULDER_MIN) {
      playLimitAlert();
    } else {
      playStartTune();
      smoothJointTo(SHOULDER_CH, curShoulder, curShoulder - JOINT_STEP, SHOULDER_MIN, SHOULDER_MAX, 90, 24);
      playDoneTune();
    }
  }
  else if (action == "micro_shoulder_front" || action == "micro_front" || action == "micro_forward") {
    server.send(200, "text/plain", "Micro Shoulder Front (5°)");
    if (curShoulder <= SHOULDER_MIN) {
      playLimitAlert();
    } else {
      playStartTune();
      smoothJointTo(SHOULDER_CH, curShoulder, curShoulder - MICRO_JOINT_STEP, SHOULDER_MIN, SHOULDER_MAX, 50, 22);
      playDoneTune();
    }
  }
  else if (action == "shoulder_back" || action == "back" || action == "pull" || action == "shoulder_pull" || action == "shoulder_reverse" || action == "reverse" || action == "rivers" || action == "river" || action == "shoulder_rivers" || action == "peeche") {
    server.send(200, "text/plain", "Shoulder Back");
    if (curShoulder >= SHOULDER_MAX) {
      playLimitAlert();
    } else {
      playStartTune();
      smoothJointTo(SHOULDER_CH, curShoulder, curShoulder + JOINT_STEP, SHOULDER_MIN, SHOULDER_MAX, 90, 24);
      playDoneTune();
    }
  }
  else if (action == "micro_shoulder_back" || action == "micro_back" || action == "micro_pull" || action == "micro_reverse" || action == "micro_rivers") {
    server.send(200, "text/plain", "Micro Shoulder Back (5°)");
    if (curShoulder >= SHOULDER_MAX) {
      playLimitAlert();
    } else {
      playStartTune();
      smoothJointTo(SHOULDER_CH, curShoulder, curShoulder + MICRO_JOINT_STEP, SHOULDER_MIN, SHOULDER_MAX, 50, 22);
      playDoneTune();
    }
  }

  // ── ELBOW (CH 4 - Explicit Elbow Raise / Lower / Up / Down) ──
  // Limit Check: ELBOW_MAX = 505 (Lowest), ELBOW_MIN = 85 (Highest)
  else if (action == "elbow_down" || action == "elbow_lower" || action == "elbow_niche" || action == "elbow_drop") {
    server.send(200, "text/plain", "Elbow Lower");
    if (curElbow >= ELBOW_MAX) {
      playLimitAlert();
    } else {
      playStartTune();
      smoothJointTo(ELBOW_CH, curElbow, curElbow + JOINT_STEP, ELBOW_MIN, ELBOW_MAX, 90, 24);
      playDoneTune();
    }
  }
  else if (action == "micro_elbow_down" || action == "micro_elbow_lower") {
    server.send(200, "text/plain", "Micro Elbow Lower (5°)");
    if (curElbow >= ELBOW_MAX) {
      playLimitAlert();
    } else {
      playStartTune();
      smoothJointTo(ELBOW_CH, curElbow, curElbow + MICRO_JOINT_STEP, ELBOW_MIN, ELBOW_MAX, 50, 22);
      playDoneTune();
    }
  }
  else if (action == "elbow_up" || action == "elbow_raise" || action == "elbow_upar" || action == "elbow_lift" || action == "elbow_high") {
    server.send(200, "text/plain", "Elbow Raise");
    if (curElbow <= ELBOW_MIN) {
      playLimitAlert();
    } else {
      playStartTune();
      smoothJointTo(ELBOW_CH, curElbow, curElbow - JOINT_STEP, ELBOW_MIN, ELBOW_MAX, 90, 24);
      playDoneTune();
    }
  }
  else if (action == "micro_elbow_up" || action == "micro_elbow_raise") {
    server.send(200, "text/plain", "Micro Elbow Raise (5°)");
    if (curElbow <= ELBOW_MIN) {
      playLimitAlert();
    } else {
      playStartTune();
      smoothJointTo(ELBOW_CH, curElbow, curElbow - MICRO_JOINT_STEP, ELBOW_MIN, ELBOW_MAX, 50, 22);
      playDoneTune();
    }
  }

  // ── VERTICAL TILT (CH 8 - Claw Raise / Upar & Lower / Niche) ──
  // Limit Check: PITCH_MAX = 460 (Highest / Upar), PITCH_MIN = 95 (Lowest / Niche)
  else if (action == "wrist_up" || action == "raise" || action == "upar" || action == "lift" || action == "high" || action == "up" || action == "pitch_up" || action == "patch_up") {
    server.send(200, "text/plain", "Raise / Upar");
    if (curPitch >= PITCH_MAX) {
      playLimitAlert();
    } else {
      playStartTune();
      smoothJointTo(PITCH_CH, curPitch, curPitch + JOINT_STEP, PITCH_MIN, PITCH_MAX, 90, 24);
      playDoneTune();
    }
  }
  else if (action == "micro_wrist_up" || action == "micro_raise" || action == "micro_upar" || action == "micro_lift" || action == "micro_up") {
    server.send(200, "text/plain", "Micro Raise (5°)");
    if (curPitch >= PITCH_MAX) {
      playLimitAlert();
    } else {
      playStartTune();
      smoothJointTo(PITCH_CH, curPitch, curPitch + MICRO_JOINT_STEP, PITCH_MIN, PITCH_MAX, 50, 22);
      playDoneTune();
    }
  }
  else if (action == "wrist_down" || action == "lower" || action == "niche" || action == "drop" || action == "down" || action == "pitch_down" || action == "patch_down") {
    server.send(200, "text/plain", "Lower / Niche");
    if (curPitch <= PITCH_MIN) {
      playLimitAlert();
    } else {
      playStartTune();
      smoothJointTo(PITCH_CH, curPitch, curPitch - JOINT_STEP, PITCH_MIN, PITCH_MAX, 90, 24);
      playDoneTune();
    }
  }
  else if (action == "micro_wrist_down" || action == "micro_lower" || action == "micro_niche" || action == "micro_down") {
    server.send(200, "text/plain", "Micro Lower (5°)");
    if (curPitch <= PITCH_MIN) {
      playLimitAlert();
    } else {
      playStartTune();
      smoothJointTo(PITCH_CH, curPitch, curPitch - MICRO_JOINT_STEP, PITCH_MIN, PITCH_MAX, 50, 22);
      playDoneTune();
    }
  }

  // ── WRIST ROLL (CH 6 - Axial Hand Twist) ──
  // Limit Check: ROLL_MIN = 100 (Leftmost), ROLL_MAX = 472 (Rightmost)
  else if (action == "roll_left") {
    server.send(200, "text/plain", "Roll Left");
    if (curRoll <= ROLL_MIN) {
      playLimitAlert();
    } else {
      playStartTune();
      smoothJointTo(ROLL_CH, curRoll, curRoll - JOINT_STEP, ROLL_MIN, ROLL_MAX, 90, 24);
      playDoneTune();
    }
  }
  else if (action == "micro_roll_left") {
    server.send(200, "text/plain", "Micro Roll Left (5°)");
    if (curRoll <= ROLL_MIN) {
      playLimitAlert();
    } else {
      playStartTune();
      smoothJointTo(ROLL_CH, curRoll, curRoll - MICRO_JOINT_STEP, ROLL_MIN, ROLL_MAX, 50, 22);
      playDoneTune();
    }
  }
  else if (action == "roll_right") {
    server.send(200, "text/plain", "Roll Right");
    if (curRoll >= ROLL_MAX) {
      playLimitAlert();
    } else {
      playStartTune();
      smoothJointTo(ROLL_CH, curRoll, curRoll + JOINT_STEP, ROLL_MIN, ROLL_MAX, 90, 24);
      playDoneTune();
    }
  }
  else if (action == "micro_roll_right") {
    server.send(200, "text/plain", "Micro Roll Right (5°)");
    if (curRoll >= ROLL_MAX) {
      playLimitAlert();
    } else {
      playStartTune();
      smoothJointTo(ROLL_CH, curRoll, curRoll + MICRO_JOINT_STEP, ROLL_MIN, ROLL_MAX, 50, 22);
      playDoneTune();
    }
  }

  else if (action == "stop") {
    server.send(200, "text/plain", "Stopped");
    baseStop();
    stopAllBuzzers();
  }
  else {
    server.send(400, "text/plain", "Unknown action");
    return;
  }

  // ── REFRESH 60-SECOND (1-MINUTE) WINDOW ON EVERY VALID COMMAND! ──
  awakeTimerEnd = millis() + AWAKE_WINDOW_MS;
  int secsLeft = (awakeTimerEnd - millis()) / 1000;
  Serial.printf("⏱️ [1-MINUTE TIMER REFRESHED] Active voice window extended! (%d sec remaining)\n\n", secsLeft);
}

void handleSetPhone() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (server.hasArg("ip")) {
    phoneServerIP = server.arg("ip");
    Serial.println("📱 Phone IP updated to: " + phoneServerIP);
    server.send(200, "text/plain", "Phone IP set to " + phoneServerIP);
  } else {
    server.send(400, "text/plain", "Missing ip arg");
  }
}

void handlePose() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (server.hasArg("name")) {
    String name = server.arg("name");
    name.toLowerCase();
    if (name == "wake") {
      smoothMoveTo(WORK_SHOULDER, WORK_ELBOW, WORK_ROLL, WORK_PITCH, WORK_GRIPPER);
      robotState = STATE_AWAKE;
      awakeTimerEnd = millis() + AWAKE_WINDOW_MS;
    } else if (name == "relax" || name == "sleep") {
      smoothMoveTo(RELAX_SHOULDER, RELAX_ELBOW, RELAX_ROLL, RELAX_PITCH, RELAX_GRIPPER);
      robotState = STATE_SLEEP;
    } else if (name == "reach") {
      smoothMoveTo(380, 260, 100, 95, curGripper);
    } else if (name == "pick") {
      smoothMoveTo(340, 210, 100, 130, curGripper);
    }
    String res = "{\"shoulder\":" + String(curShoulder) + ",\"elbow\":" + String(curElbow) + ",\"pitch\":" + String(curPitch) + ",\"roll\":" + String(curRoll) + ",\"gripper\":" + String(curGripper) + "}";
    server.send(200, "application/json", res);
  } else {
    server.send(400, "text/plain", "Missing name");
  }
}

void handleSet() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (server.hasArg("joint") && server.hasArg("pulse")) {
    String j = server.arg("joint");
    int p = server.arg("pulse").toInt();
    if (j == "shoulder") setShoulder(p);
    else if (j == "elbow") setElbow(p);
    else if (j == "roll") setRoll(p);
    else if (j == "pitch") setPitch(p);
    else if (j == "gripper") setGripper(p);
    server.send(200, "text/plain", j + " set to " + String(p));
  } else {
    server.send(400, "text/plain", "Missing params");
  }
}

void handleCut() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  for (int ch = 0; ch < 16; ch++) {
    pwm.setPWM(ch, 0, 0);
  }
  robotState = STATE_SLEEP;
  server.send(200, "text/plain", "Power cut");
  Serial.println("⚠️ All PWM Cut! Arm Relaxed.");
}

void handleRoot() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send_P(200, "text/html", INDEX_HTML);
}

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n==============================================");
  Serial.println("🤖 5-DOF RoboArm Offline Voice Controller");
  Serial.println("Wake Word: 'ROBO' | 60s (1-Min) Sliding Window");
  Serial.println("==============================================");

  // Initialize 3-Buzzer Pins
  pinMode(BUZZER_1_PIN, OUTPUT);
  pinMode(BUZZER_2_PIN, OUTPUT);
  pinMode(BUZZER_3_PIN, OUTPUT);
  stopAllBuzzers();

  // 1. Allocate Audio Buffer in RAM
  wavBuffer = (uint8_t*)psramFound() ? (uint8_t*)ps_malloc(WAV_TOTAL_SIZE) : (uint8_t*)malloc(WAV_TOTAL_SIZE);
  if (!wavBuffer) {
    Serial.println("❌ FATAL: Memory allocation failed for audio buffer!");
    while (1) delay(1000);
  }
  Serial.printf("✅ Pre-allocated %d bytes WAV audio buffer\n", WAV_TOTAL_SIZE);

  // 2. Initialize PCA9685 with exact 27MHz Oscillator calibration
  Wire.begin(21, 22);
  pwm.begin();
  pwm.setOscillatorFrequency(27000000); // Prevents timing drift & base creep
  pwm.setPWMFreq(SERVO_FREQ);
  delay(50);

  // Initialize Base 360 at exact dead stop
  baseStop();
  delay(50);

  // Staggered soft power-up into calibrated RELAX (Sleep) Pose
  setGripper(curGripper);
  delay(60);
  setRoll(curRoll);
  delay(60);
  setPitch(curPitch);
  delay(60);
  setElbow(curElbow);
  delay(60);
  setShoulder(curShoulder);
  delay(50);
  Serial.println("✅ Servos Locked in TRUE RELAX (Sleep) Pose.");

  // 3. Connect Wi-Fi: Auto-discover and connect directly to Phone Hotspot 'Warrior'
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(150);
  WiFi.setSleep(false); // Max RF power

  Serial.println("🔍 Scanning for Phone Hotspot 'Warrior'...");
  int n = WiFi.scanNetworks(false, true);
  String targetSSID = "";
  int targetChannel = 1;

  for (int i = 0; i < n; ++i) {
    String s = WiFi.SSID(i);
    String sTrim = s;
    sTrim.trim();
    if (sTrim.equalsIgnoreCase("Warrior")) {
      targetSSID = s; // Grab exact SSID including any trailing spaces!
      targetChannel = WiFi.channel(i);
      Serial.printf("🎯 MATCH FOUND! SSID: '%s' (Length: %d chars) on Channel %d (%d dBm)\n",
                    targetSSID.c_str(), targetSSID.length(), targetChannel, WiFi.RSSI(i));
      break;
    }
  }

  if (targetSSID.length() > 0) {
    Serial.printf("📱 Connecting to '%s' on Channel %d...\n", targetSSID.c_str(), targetChannel);
    if (strlen(HOME_PASS) > 0) {
      WiFi.begin(targetSSID.c_str(), HOME_PASS, targetChannel);
    } else {
      WiFi.begin(targetSSID.c_str(), (const char*)NULL, targetChannel);
    }

    unsigned long startWifi = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startWifi < 15000) {
      delay(400);
      Serial.print(".");
    }
  } else {
    Serial.println("❌ Hotspot 'Warrior' not found in initial scan. Trying direct begin...");
    WiFi.begin(HOME_SSID, (const char*)NULL, 1);
    unsigned long startWifi = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startWifi < 8000) {
      delay(400);
      Serial.print(".");
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n🎉 CONNECTED SUCCESSFULLY TO PHONE HOTSPOT ('Warrior')!");
    Serial.print("👉 OPEN THIS IN CHROME ON YOUR PHONE: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n⚠️ Could not connect to 'Warrior'.");
    // Start fallback SoftAP
    Serial.println("\n🚀 Starting standalone fallback hotspot: 'RoboArm-AP'");
    WiFi.mode(WIFI_AP);
    IPAddress apIP(192, 168, 4, 1);
    IPAddress apGateway(192, 168, 4, 1);
    IPAddress apSubnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apGateway, apSubnet);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print("📶 [HOTSPOT ACTIVE] SSID: RoboArm-AP | IP: ");
    Serial.println(WiFi.softAPIP());
    phoneServerIP = "192.168.4.2";
  }

  // Start mDNS responder so phone can access via http://roboarm.local
  if (MDNS.begin("roboarm")) {
    Serial.println("🌐 mDNS responder active: http://roboarm.local");
  }

  // 4. Setup HTTP Server
  server.on("/", handleRoot);
  server.on("/cmd", handleCmd);
  server.on("/pose", handlePose);
  server.on("/set", handleSet);
  server.on("/cut", handleCut);
  server.on("/set_phone", handleSetPhone);
  server.enableCORS(true);
  server.begin();
  Serial.println("🌐 Web Dashboard running on port 80");

  // 5. Initialize INMP441 Microphone & Calibrate Room Noise
  initI2S();
  Serial.println("🎤 INMP441 Microphone Initialized (16kHz 16-bit Mono).");
  
  // Measure ambient room background noise (fans, hum) for 1 second
  Serial.print("🎙️ Calibrating room noise floor... ");
  float sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += measureMicRMS(128);
    delay(40);
  }
  ambientNoiseRMS = sum / 20.0f;
  if (ambientNoiseRMS < 50.0f) ambientNoiseRMS = 50.0f;
  // Trigger threshold must be safely above room noise floor (+200 RMS, minimum 320)
  triggerThreshold = ambientNoiseRMS + 200.0f;
  if (triggerThreshold < 320.0f) triggerThreshold = 320.0f;
  Serial.printf("Base Room Noise: %.0f RMS | Trigger Threshold: %.0f RMS\n", ambientNoiseRMS, triggerThreshold);

  Serial.println("🔒 Arm is ASLEEP. Say 'ROBO' to wake up!\n");
}

// ─────────────────────────────────────────────────────────────
//  MAIN LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
  server.handleClient();

  // ── Auto-Sleep 60-Second (1-Minute) Timeout ──
  if (robotState == STATE_AWAKE && !isMoving) {
    if (millis() > awakeTimerEnd) {
      Serial.println("\n💤 [60s TIMEOUT] No command received within 1 minute. Moving smoothly to SLEEP pose...");
      smoothMoveTo(RELAX_SHOULDER, RELAX_ELBOW, RELAX_ROLL, RELAX_PITCH, RELAX_GRIPPER);
      robotState = STATE_SLEEP;
      Serial.println("🔒 [ARM ASLEEP] Resting in Sleep Pose. Say 'ROBO' to wake up!\n");
      lastMotionTime = millis();
    }
  }

  // Check post-motion settling cooldown
  bool inCooldown = (millis() - lastMotionTime < SERVO_COOLDOWN_MS) || isMoving;

  // Sound Level Detection (Only active if ENABLE_ONBOARD_MIC_STREAM is enabled)
  // Kept false by default to guarantee instantaneous 0ms latency for Phone Voice Control!
  if (ENABLE_ONBOARD_MIC_STREAM && !inCooldown) {
    float rms = measureMicRMS(128);
    if (rms > triggerThreshold) {
      Serial.printf("🎤 Voice detected (RMS: %.0f > %.0f) -> Recording 2.5s...\n", rms, triggerThreshold);
      recordAudio();
      sendAudioToPhone();
      lastMotionTime = millis();
      delay(400); // Prevent echo feedback re-trigger
      Serial.println("🟢 Listening ready...\n");
    }
  }

  delay(2);
}
