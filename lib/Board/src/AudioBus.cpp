#include "AudioBus.h"

#include <Arduino.h>

#include "BoardConfig.h"
#include "I2cLock.h"

#if BOARD_AUDIO_SHARED_BUS
#include "codecs/es7210.h"
#include "codecs/es8311.h"
#endif

namespace audio_bus {

#if !BOARD_AUDIO_SHARED_BUS

namespace {
I2SClass out;
I2SClass in;
bool outOn = false;
bool inOn = false;
}  // namespace

I2SClass *acquireOutput() {
  if (outOn) return &out;
  out.setPins(pins::SPK_BCK, pins::SPK_LRCK, pins::SPK_DIN);
  if (!out.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    log_e("Speaker I2S init failed");
    return nullptr;
  }
  outOn = true;
  return &out;
}

I2SClass *acquireInput() {
  if (inOn) return &in;
  in.setPins(pins::MIC_SCK, pins::MIC_WS, -1, pins::MIC_SD);
  in.setTimeout(1000);
  // Stereo like Waveshare's reference code; the mic answers on one slot.
  if (!in.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    log_e("Microphone I2S init failed");
    return nullptr;
  }
  inOn = true;
  return &in;
}

void releaseOutput() {
  if (outOn) out.end();
  outOn = false;
}

void releaseInput() {
  if (inOn) in.end();
  inOn = false;
}

bool prepareExternalOutput() { return true; }

void setAmp(bool) {}

#else  // shared full-duplex bus with ES8311 + ES7210

namespace {
constexpr uint32_t MCLK_MULTIPLE = 256;
constexpr int DAC_VOLUME = 75;            // ES8311 0..100; software volume on top
constexpr int8_t ADC_VOLUME_DB = 0;       // ES7210 digital gain
I2SClass bus;
int users = 0;
bool dacReady = false;
bool adcReady = false;

bool initDac() {
  if (dacReady) return true;
  board::I2cGuard lock;
  es8311_handle_t es = es8311_create(I2C_NUM_0, i2c_addr::ES8311);
  if (!es) return false;
  const es8311_clock_config_t clk = {
      .mclk_inverted = false,
      .sclk_inverted = false,
      .mclk_from_mclk_pin = true,
      .mclk_frequency = (int)(SAMPLE_RATE * MCLK_MULTIPLE),
      .sample_frequency = (int)SAMPLE_RATE,
  };
  if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK ||
      es8311_voice_volume_set(es, DAC_VOLUME, nullptr) != ESP_OK) {
    log_e("ES8311 init failed");
    return false;
  }
  digitalWrite(pins::PA_EN, HIGH);
  dacReady = true;
  return true;
}

bool initAdc() {
  if (adcReady) return true;
  board::I2cGuard lock;
  es7210_i2c_config_t i2c = {.i2c_port = I2C_NUM_0, .i2c_addr = i2c_addr::ES7210};
  es7210_dev_handle_t es = nullptr;
  if (es7210_new_codec(&i2c, &es) != ESP_OK) return false;
  es7210_codec_config_t cfg = {};
  cfg.i2s_format = ES7210_I2S_FMT_I2S;
  cfg.mclk_ratio = MCLK_MULTIPLE;
  cfg.sample_rate_hz = SAMPLE_RATE;
  cfg.bit_width = ES7210_I2S_BITS_16B;
  cfg.mic_bias = ES7210_MIC_BIAS_2V87;
  cfg.mic_gain = ES7210_MIC_GAIN_30DB;
  cfg.flags.tdm_enable = false;
  if (es7210_config_codec(es, &cfg) != ESP_OK ||
      es7210_config_volume(es, ADC_VOLUME_DB) != ESP_OK) {
    log_e("ES7210 init failed");
    return false;
  }
  adcReady = true;
  return true;
}

I2SClass *acquire() {
  if (users == 0) {
    // Both directions are wired up at once; MCLK must run for either codec.
    bus.setPins(pins::I2S_BCK, pins::I2S_WS, pins::I2S_DOUT, pins::I2S_DIN, pins::I2S_MCLK);
    bus.setTimeout(1000);
    if (!bus.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
      log_e("I2S init failed");
      return nullptr;
    }
  }
  users++;
  return &bus;
}

void release() {
  if (users > 0 && --users == 0) bus.end();
}
}  // namespace

I2SClass *acquireOutput() {
  I2SClass *p = acquire();
  if (p && !initDac()) {
    release();
    return nullptr;
  }
  return p;
}

I2SClass *acquireInput() {
  I2SClass *p = acquire();
  if (p && !initAdc()) {
    release();
    return nullptr;
  }
  return p;
}

void releaseOutput() {
  digitalWrite(pins::PA_EN, LOW);
  dacReady = false;
  release();
}

void releaseInput() { release(); }

bool prepareExternalOutput() {
  if (!initDac()) return false;
  digitalWrite(pins::PA_EN, HIGH);
  return true;
}

void setAmp(bool on) {
  if (dacReady) digitalWrite(pins::PA_EN, on ? HIGH : LOW);
}

#endif

}  // namespace audio_bus
