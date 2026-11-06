#include <Arduino.h>
#include <driver/i2s.h>

// ─────────────────────────────────────────
//  INMP441 I2S Pin Configuration
// ─────────────────────────────────────────
#define I2S_WS   25   // Word Select (LRCLK)
#define I2S_SCK  26   // Serial Clock (BCLK)
#define I2S_SD   34   // Serial Data (DIN from mic)
#define I2S_PORT I2S_NUM_0

#define SAMPLE_RATE     16000
#define BUFFER_LEN      512

int32_t raw_samples[BUFFER_LEN];

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

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Failed to install I2S driver: %d\n", err);
    while (1);
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Failed to set I2S pins: %d\n", err);
    while (1);
  }

  Serial.println("I2S Driver installed successfully on pins (WS: 25, SCK: 26, SD: 34)");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=============================================");
  Serial.println("   INMP441 Hardware Diagnostic & Audio Test  ");
  Serial.println("=============================================");

  setupI2S();
  Serial.println("Listening... Speak or Clap near the mic!\n");
}

void loop() {
  size_t bytes_read = 0;
  esp_err_t result = i2s_read(I2S_PORT, raw_samples, sizeof(raw_samples), &bytes_read, portMAX_DELAY);

  if (result == ESP_OK && bytes_read > 0) {
    int samples_count = bytes_read / sizeof(int32_t);

    int64_t sum_squares = 0;
    int32_t peak = 0;

    for (int i = 0; i < samples_count; i++) {
      // INMP441 produces 24-bit audio inside 32-bit word, shifted left by 8 bits
      int32_t val = raw_samples[i] >> 14; // Scaled down for stable RMS calculation
      sum_squares += (int64_t)val * val;
      if (abs(val) > peak) {
        peak = abs(val);
      }
    }

    // Root Mean Square (RMS) represents volume/energy
    int rms = sqrt(sum_squares / samples_count);

    // Visual Sound Bar in Serial Monitor (e.g. 0 to 40 bars)
    int bars = constrain(rms / 120, 0, 30);
    String meter = "[";
    for (int b = 0; b < 30; b++) {
      if (b < bars) meter += "=";
      else meter += " ";
    }
    meter += "]";

    // Status message based on volume
    String status = "Quiet";
    if (rms > 2500) status = "🗣️ VOICE / CLAP DETECTED!";
    else if (rms > 800) status = "Listening...";

    Serial.printf("%s  RMS: %-5d | Peak: %-6d | %s\n", meter.c_str(), rms, peak, status.c_str());
  }

  delay(40);
}
