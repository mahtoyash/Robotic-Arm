#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <driver/i2s.h>

// ─────────────────────────────────────────
//  WiFi & Groq Cloud API Credentials
// ─────────────────────────────────────────
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* GROQ_API_KEY  = "YOUR_GROQ_API_KEY_HERE";

const char* GROQ_HOST     = "api.groq.com";
const int   GROQ_PORT     = 443;
const char* GROQ_PATH     = "/openai/v1/audio/transcriptions";

// ─────────────────────────────────────────
//  PCA9685 Config & Joint Calibrated Points
// ─────────────────────────────────────────
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
#define SERVO_FREQ 50

// Channel Map
const int BASE_CH         = 0;   // MG996R 360° Continuous
const int BASE_STOP       = 290;
const int BASE_SPEED_STEP = 25;

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
const int GRIPPER_MAX     = 450;
const int GRIPPER_CLOSE   = 246; // User Calibrated Close
const int GRIPPER_OPEN    = 430; // User Calibrated Open

// ─────────────────────────────────────────
//  Calibrated Master Poses
// ─────────────────────────────────────────
// 1. SLEEP / RELAX POSE (Folded, Low Load Rest)
const int RELAX_SHOULDER = 480;
const int RELAX_ELBOW    = 505;
const int RELAX_ROLL     = 472;
const int RELAX_PITCH    = 460;
const int RELAX_GRIPPER  = 246;

// 2. WORKING / WAKE READY POSE (Active Working Stance)
const int WORK_SHOULDER  = 410;
const int WORK_ELBOW     = 320;
const int WORK_ROLL      = 100;
const int WORK_PITCH     = 95;
const int WORK_GRIPPER   = 246;

// Live Joint States (Boot at Sleep Pose)
int curShoulder = RELAX_SHOULDER;
int curElbow    = RELAX_ELBOW;
int curRoll     = RELAX_ROLL;
int curPitch    = RELAX_PITCH;
int curGripper  = RELAX_GRIPPER;

// ─────────────────────────────────────────
//  INMP441 Microphone (I2S Config)
// ─────────────────────────────────────────
#define I2S_WS          25   // Word Select (LRCLK)
#define I2S_SCK         26   // Continuous Serial Clock (BCLK)
#define I2S_SD          34   // Serial Data Input (DIN)
#define I2S_PORT        I2S_NUM_0

#define SAMPLE_RATE     16000
#define RECORD_SECONDS  1.8
#define TOTAL_SAMPLES   ((int)(SAMPLE_RATE * RECORD_SECONDS)) // 28,800 samples
#define WAV_DATA_SIZE   (TOTAL_SAMPLES * sizeof(int16_t))     // 57,600 bytes
#define WAV_HEADER_SIZE 44
#define WAV_TOTAL_SIZE  (WAV_HEADER_SIZE + WAV_DATA_SIZE)     // 57,644 bytes

#define SOUND_TRIGGER_THRESHOLD 1800  // RMS Energy required to start recording

// Pre-allocated Audio Buffer
uint8_t* wavBuffer = nullptr;

// ─────────────────────────────────────────
//  Motion Primitives
// ─────────────────────────────────────────
void baseStop() {
  pwm.setPWM(BASE_CH, 0, BASE_STOP);
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

void setGripper(int p) {
  curGripper = constrain(p, GRIPPER_MIN, GRIPPER_MAX);
  pwm.setPWM(GRIPPER_CH, 0, curGripper);
  Serial.printf("Gripper set: %d\n", curGripper);
}

void setShoulder(int p) {
  curShoulder = constrain(p, SHOULDER_MIN, SHOULDER_MAX);
  pwm.setPWM(SHOULDER_CH, 0, curShoulder);
}

void setElbow(int p) {
  curElbow = constrain(p, ELBOW_MIN, ELBOW_MAX);
  pwm.setPWM(ELBOW_CH, 0, curElbow);
}

void setRoll(int p) {
  curRoll = constrain(p, ROLL_MIN, ROLL_MAX);
  pwm.setPWM(ROLL_CH, 0, curRoll);
}

void setPitch(int p) {
  curPitch = constrain(p, PITCH_MIN, PITCH_MAX);
  pwm.setPWM(PITCH_CH, 0, curPitch);
}

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
  Serial.printf("Joint CH%d smoothly locked at: %d\n", ch, currentVal);
}

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

  int totalSteps = max(maxDelta, 70);

  for (int step = 1; step <= totalSteps; step++) {
    float t = (float)step / (float)totalSteps;
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

  Serial.printf("Synchronized Glide Done -> S:%d E:%d R:%d P:%d G:%d\n", curShoulder, curElbow, curRoll, curPitch, curGripper);
}

// ─────────────────────────────────────────
//  WAV File Header Generator
// ─────────────────────────────────────────
void createWavHeader(uint8_t* header, int totalDataLen) {
  int totalFileLen = totalDataLen + 36;
  int sampleRate = SAMPLE_RATE;
  int byteRate = sampleRate * 1 * sizeof(int16_t);

  header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
  header[4] = (uint8_t)(totalFileLen & 0xff);
  header[5] = (uint8_t)((totalFileLen >> 8) & 0xff);
  header[6] = (uint8_t)((totalFileLen >> 16) & 0xff);
  header[7] = (uint8_t)((totalFileLen >> 24) & 0xff);
  header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
  header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
  header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
  header[20] = 1; header[21] = 0; // PCM
  header[22] = 1; header[23] = 0; // Mono
  header[24] = (uint8_t)(sampleRate & 0xff);
  header[25] = (uint8_t)((sampleRate >> 8) & 0xff);
  header[26] = (uint8_t)((sampleRate >> 16) & 0xff);
  header[27] = (uint8_t)((sampleRate >> 24) & 0xff);
  header[28] = (uint8_t)(byteRate & 0xff);
  header[29] = (uint8_t)((byteRate >> 8) & 0xff);
  header[30] = (uint8_t)((byteRate >> 16) & 0xff);
  header[31] = (uint8_t)((byteRate >> 24) & 0xff);
  header[32] = 2; header[33] = 0; // Block align (1 * 16 / 8 = 2)
  header[34] = 16; header[35] = 0; // 16-bit
  header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
  header[40] = (uint8_t)(totalDataLen & 0xff);
  header[41] = (uint8_t)((totalDataLen >> 8) & 0xff);
  header[42] = (uint8_t)((totalDataLen >> 16) & 0xff);
  header[43] = (uint8_t)((totalDataLen >> 24) & 0xff);
}

// ─────────────────────────────────────────
//  I2S INMP441 Setup
// ─────────────────────────────────────────
void setupI2S() {
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  Serial.println("I2S Mic (INMP441) ready on GPIO 25, 26, 34");
}

// ─────────────────────────────────────────
//  Audio Record (1.8s into WAV Buffer)
// ─────────────────────────────────────────
void recordAudio() {
  Serial.println("🔴 [RECORDING] Speak now into INMP441...");
  createWavHeader(wavBuffer, WAV_DATA_SIZE);

  int16_t* pcmSamples = (int16_t*)(wavBuffer + WAV_HEADER_SIZE);
  int samplesReadTotal = 0;
  int32_t rawChunk[256];

  while (samplesReadTotal < TOTAL_SAMPLES) {
    size_t bytes_read = 0;
    i2s_read(I2S_PORT, rawChunk, sizeof(rawChunk), &bytes_read, portMAX_DELAY);
    int count = bytes_read / sizeof(int32_t);

    for (int i = 0; i < count && samplesReadTotal < TOTAL_SAMPLES; i++) {
      // 24-bit INMP441 shifted down to 16-bit PCM
      pcmSamples[samplesReadTotal++] = (int16_t)(rawChunk[i] >> 14);
    }
  }

  Serial.println("⏹️ [RECORDING DONE] 1.8 seconds audio captured!");
}

// ─────────────────────────────────────────
//  Groq Cloud Whisper API Transcription
// ─────────────────────────────────────────
String transcribeAudioWithGroq() {
  Serial.println("☁️ Connecting to Groq Cloud Whisper API...");
  WiFiClientSecure client;
  client.setInsecure(); // Skip certificate verification for speed & simplicity
  client.setTimeout(8000);

  if (!client.connect(GROQ_HOST, GROQ_PORT)) {
    Serial.println("❌ Connection to api.groq.com failed!");
    return "";
  }

  String boundary = "----RoboArmBoundary123456";

  // Build Multipart Form Data Parts
  String partModel = "--" + boundary + "\r\n" +
                     "Content-Disposition: form-data; name=\"model\"\r\n\r\n" +
                     "whisper-large-v3-turbo\r\n";

  String partFormat = "--" + boundary + "\r\n" +
                      "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n" +
                      "json\r\n";

  String partFileHeader = "--" + boundary + "\r\n" +
                          "Content-Disposition: form-data; name=\"file\"; filename=\"voice.wav\"\r\n" +
                          "Content-Type: audio/wav\r\n\r\n";

  String partClosing = "\r\n--" + boundary + "--\r\n";

  int totalContentLength = partModel.length() + partFormat.length() + partFileHeader.length() + WAV_TOTAL_SIZE + partClosing.length();

  // Send HTTP Headers
  client.print(String("POST ") + GROQ_PATH + " HTTP/1.1\r\n");
  client.print(String("Host: ") + GROQ_HOST + "\r\n");
  client.print(String("Authorization: Bearer ") + GROQ_API_KEY + "\r\n");
  client.print(String("Content-Type: multipart/form-data; boundary=") + boundary + "\r\n");
  client.print(String("Content-Length: ") + totalContentLength + "\r\n");
  client.print("Connection: close\r\n\r\n");

  // Send Multipart Form Data Body
  client.print(partModel);
  client.print(partFormat);
  client.print(partFileHeader);

  // Send Audio WAV Bytes in fast 1024-byte chunks
  int bytesLeft = WAV_TOTAL_SIZE;
  int offset = 0;
  while (bytesLeft > 0) {
    int chunkSize = min(bytesLeft, 1024);
    client.write(wavBuffer + offset, chunkSize);
    offset += chunkSize;
    bytesLeft -= chunkSize;
  }

  client.print(partClosing);
  Serial.println("🚀 Audio stream uploaded! Waiting for transcription...");

  // Read Response
  long timeout = millis() + 5000;
  while (client.connected() && !client.available()) {
    if (millis() > timeout) {
      Serial.println("❌ Groq API response timed out!");
      client.stop();
      return "";
    }
    delay(10);
  }

  // Parse HTTP Body
  String response = "";
  bool isBody = false;
  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      isBody = true;
      continue;
    }
    if (isBody) {
      response += line;
    }
  }
  client.stop();

  Serial.println("Raw Groq Response: " + response);

  // Extract "text": "..."
  int idx = response.indexOf("\"text\":");
  if (idx != -1) {
    int startQ = response.indexOf("\"", idx + 7);
    int endQ = response.indexOf("\"", startQ + 1);
    if (startQ != -1 && endQ != -1) {
      String transcribed = response.substring(startQ + 1, endQ);
      transcribed.toLowerCase();
      transcribed.trim();
      return transcribed;
    }
  }

  return "";
}

// ─────────────────────────────────────────
//  Command Dispatcher
// ─────────────────────────────────────────
void executeCommand(String text) {
  text.toLowerCase();
  text.trim();
  Serial.println("🤖 Analyzing command: \"" + text + "\"");

  if (text.indexOf("wake") != -1 || text.indexOf("ready") != -1 || text.indexOf("start") != -1) {
    Serial.println("⚡ Command: WAKE UP");
    smoothMoveTo(WORK_SHOULDER, WORK_ELBOW, WORK_ROLL, WORK_PITCH, WORK_GRIPPER, 12);
  }
  else if (text.indexOf("sleep") != -1 || text.indexOf("park") != -1 || text.indexOf("rest") != -1) {
    Serial.println("⚡ Command: SLEEP / PARK");
    smoothMoveTo(RELAX_SHOULDER, RELAX_ELBOW, RELAX_ROLL, RELAX_PITCH, RELAX_GRIPPER, 16);
  }
  else if (text.indexOf("left") != -1 && text.indexOf("roll") == -1) {
    Serial.println("⚡ Command: BASE LEFT");
    smoothBaseNudge(-BASE_SPEED_STEP, 320);
  }
  else if (text.indexOf("right") != -1 && text.indexOf("roll") == -1) {
    Serial.println("⚡ Command: BASE RIGHT");
    smoothBaseNudge(BASE_SPEED_STEP, 320);
  }
  else if (text.indexOf("shoulder front") != -1 || text.indexOf("shoulder down") != -1 || text.indexOf("shoulder forward") != -1) {
    Serial.println("⚡ Command: SHOULDER FRONT");
    smoothJointTo(SHOULDER_CH, curShoulder, curShoulder - 25, SHOULDER_MIN, SHOULDER_MAX, 12);
  }
  else if (text.indexOf("shoulder back") != -1 || text.indexOf("shoulder up") != -1 || text.indexOf("shoulder backward") != -1) {
    Serial.println("⚡ Command: SHOULDER BACK");
    smoothJointTo(SHOULDER_CH, curShoulder, curShoulder + 25, SHOULDER_MIN, SHOULDER_MAX, 12);
  }
  else if (text.indexOf("elbow down") != -1 || text.indexOf("elbow lower") != -1 || text.indexOf("elbow front") != -1) {
    Serial.println("⚡ Command: ELBOW DOWN");
    smoothJointTo(ELBOW_CH, curElbow, curElbow - 25, ELBOW_MIN, ELBOW_MAX, 12);
  }
  else if (text.indexOf("elbow up") != -1 || text.indexOf("elbow back") != -1 || text.indexOf("elbow lift") != -1) {
    Serial.println("⚡ Command: ELBOW UP");
    smoothJointTo(ELBOW_CH, curElbow, curElbow + 25, ELBOW_MIN, ELBOW_MAX, 12);
  }
  else if (text.indexOf("wrist up") != -1 || text.indexOf("pitch up") != -1) {
    Serial.println("⚡ Command: WRIST UP");
    smoothJointTo(PITCH_CH, curPitch, curPitch + 25, PITCH_MIN, PITCH_MAX, 12);
  }
  else if (text.indexOf("wrist down") != -1 || text.indexOf("pitch down") != -1) {
    Serial.println("⚡ Command: WRIST DOWN");
    smoothJointTo(PITCH_CH, curPitch, curPitch - 25, PITCH_MIN, PITCH_MAX, 12);
  }
  else if (text.indexOf("roll left") != -1) {
    Serial.println("⚡ Command: ROLL LEFT");
    smoothJointTo(ROLL_CH, curRoll, curRoll - 30, ROLL_MIN, ROLL_MAX, 12);
  }
  else if (text.indexOf("roll right") != -1) {
    Serial.println("⚡ Command: ROLL RIGHT");
    smoothJointTo(ROLL_CH, curRoll, curRoll + 30, ROLL_MIN, ROLL_MAX, 12);
  }
  else if (text.indexOf("grab") != -1 || text.indexOf("close") != -1 || text.indexOf("catch") != -1 || text.indexOf("grip") != -1) {
    Serial.println("⚡ Command: GRIPPER CLOSE");
    setGripper(GRIPPER_CLOSE);
  }
  else if (text.indexOf("open") != -1 || text.indexOf("release") != -1) {
    Serial.println("⚡ Command: GRIPPER OPEN");
    setGripper(GRIPPER_OPEN);
  }
  else if (text.indexOf("stop") != -1 || text.indexOf("halt") != -1) {
    Serial.println("⚡ Command: STOP");
    baseStop();
  }
  else {
    Serial.println("❓ Unrecognized Command: \"" + text + "\"");
  }
}

// ─────────────────────────────────────────
//  Setup & Loop
// ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=======================================================");
  Serial.println("   RoboArm Cloud Voice Control (INMP441 + Groq Whisper)  ");
  Serial.println("=======================================================");

  // Allocate 57.6 KB Audio RAM buffer
  wavBuffer = (uint8_t*)malloc(WAV_TOTAL_SIZE);
  if (!wavBuffer) {
    Serial.println("❌ Failed to allocate audio buffer!");
    while (1);
  }
  Serial.println("✅ Audio Buffer allocated in RAM (57.6 KB)");

  // Setup I2S Mic
  setupI2S();

  // Setup PCA9685 Servos
  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(SERVO_FREQ);
  delay(50);

  baseStop();
  delay(50);

  // Soft boot at Sleep Pose
  setGripper(curGripper);
  delay(60);
  setRoll(curRoll);
  delay(60);
  setPitch(curPitch);
  delay(60);
  setElbow(curElbow);
  delay(60);
  setShoulder(curShoulder);
  Serial.println("✅ Robot Arm initialized at SLEEP pose.");

  // Connect to Home WiFi
  Serial.printf("Connecting to WiFi: %s ", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Connected!");
    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n⚠️ WiFi Connection Failed! Check SSID/Password.");
  }

  Serial.println("\n🟢 SYSTEM READY! Speak commands into INMP441 mic near the arm...");
  Serial.println("Examples: \"Robo Wake Up\", \"Shoulder Front\", \"Base Left\", \"Grab\", \"Sleep\"\n");
}

void loop() {
  // Continuous Voice Activity Detection (VAD) via I2S
  int32_t sampleBuffer[128];
  size_t bytes_read = 0;
  i2s_read(I2S_PORT, sampleBuffer, sizeof(sampleBuffer), &bytes_read, portMAX_DELAY);

  int count = bytes_read / sizeof(int32_t);
  int64_t sumSq = 0;

  for (int i = 0; i < count; i++) {
    int32_t val = sampleBuffer[i] >> 14;
    sumSq += (int64_t)val * val;
  }

  int rms = sqrt(sumSq / count);

  // When speech energy exceeds threshold
  if (rms > SOUND_TRIGGER_THRESHOLD) {
    Serial.printf("\n🎤 Sound detected! RMS: %d -> Starting recording...\n", rms);

    // Record 1.8 seconds of audio
    recordAudio();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("⚠️ WiFi not connected! Retrying...");
      WiFi.reconnect();
      delay(1000);
      return;
    }

    // Send to Groq Whisper AI
    long t0 = millis();
    String transcribed = transcribeAudioWithGroq();
    long tTotal = millis() - t0;

    if (transcribed.length() > 0) {
      Serial.printf("✨ [CLOUD SPEECH IN %ld ms]: \"%s\"\n", tTotal, transcribed.c_str());
      executeCommand(transcribed);
    } else {
      Serial.println("⚠️ No clear speech recognized.");
    }

    Serial.println("\n🟢 Listening again for your voice...\n");
    delay(400); // Small cooldown before next listening loop
  }

  delay(20);
}
