#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <WiFi.h>
#include <driver/i2s_std.h>
#include <lvgl.h>
#include <math.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "Innovation Lab"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "replace-me"
#endif

#ifndef WIFI_FALLBACK_SSID
#define WIFI_FALLBACK_SSID "TP-Link_ABD8"
#endif

#ifndef WIFI_FALLBACK_PASSWORD
#define WIFI_FALLBACK_PASSWORD "28892168"
#endif

#define Serial Serial0

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kScanDelayMs = 250;
constexpr uint32_t kConnectTimeoutMs = 30000;

constexpr int16_t kScreenWidth = 320;
constexpr int16_t kScreenHeight = 240;
constexpr uint16_t kDrawBufferRows = 24;

constexpr int8_t kTftSck = 6;
constexpr int8_t kTftMiso = 2;
constexpr int8_t kTftMosi = 7;
constexpr int8_t kTftCs = 23;
constexpr int8_t kTftDc = 24;
constexpr int8_t kTftRst = -1;
constexpr int8_t kTftBacklight = 25;
constexpr int8_t kTouchCs = 1;
constexpr int8_t kSdCs = 10;
constexpr uint32_t kAudioSampleRate = 44100;
constexpr uint32_t kAudioToneHz = 880;
constexpr uint8_t kAudioVolumePercent = 20;
constexpr int16_t kAudioTonePeakAmplitude = 5000;
constexpr int16_t kAudioToneAmplitude = (kAudioTonePeakAmplitude * kAudioVolumePercent) / 100;
constexpr size_t kAudioFramesPerBuffer = 512;
constexpr size_t kAudioSineTableSize = 256;
constexpr uint32_t kAudioPreviewMs = 2000;
constexpr uint32_t kAudioPinTestGapMs = 250;
constexpr uint32_t kTouchSpiHz = 2500000;
constexpr int16_t kTouchRawMinX = 250;
constexpr int16_t kTouchRawMaxX = 3800;
constexpr int16_t kTouchRawMinY = 250;
constexpr int16_t kTouchRawMaxY = 3800;
constexpr int16_t kTouchMinPressure = 300;
constexpr uint8_t kTouchRotation = 1;
constexpr uint32_t kTouchDiagnosticIntervalMs = 1200;

Arduino_DataBus *bus = new Arduino_ESP32SPI(kTftDc, kTftCs, kTftSck, kTftMosi, kTftMiso);
Arduino_GFX *gfx = new Arduino_ST7789(bus, kTftRst, 1, false, 240, 320);

lv_disp_draw_buf_t drawBuffer;
lv_color_t drawBuffer1[kScreenWidth * kDrawBufferRows];
lv_color_t drawBuffer2[kScreenWidth * kDrawBufferRows];
lv_disp_drv_t displayDriver;
lv_indev_drv_t inputDriver;
uint32_t lastLvTickMs = 0;
uint32_t nextTouchDiagnosticMs = 0;

lv_obj_t *statusDot = nullptr;
lv_obj_t *titleLabel = nullptr;
lv_obj_t *stateLabel = nullptr;
lv_obj_t *ssidLabel = nullptr;
lv_obj_t *bssidLabel = nullptr;
lv_obj_t *detailLabel = nullptr;

lv_style_t styleScreen;
lv_style_t styleCard;
lv_style_t styleTitle;
lv_style_t styleValue;
lv_style_t styleMuted;
lv_style_t styleDot;
lv_style_t styleStationRow;
lv_style_t styleStationSelected;
lv_style_t styleStationText;
lv_style_t styleStationSelectedText;
lv_style_t styleHero;
lv_style_t styleControl;
lv_style_t styleControlPrimary;

i2s_chan_handle_t audioTxChannel = nullptr;
bool audioReady = false;
bool audioTonePlaying = false;
uint32_t audioPhase = 0;
int16_t audioSineTable[kAudioSineTableSize] = {};
int16_t audioSampleBuffer[kAudioFramesPerBuffer * 2] = {};
TaskHandle_t audioTaskHandle = nullptr;

struct AudioPinProfile {
  const char *name;
  int8_t bclk;
  int8_t lrc;
  int8_t dout;
};

const AudioPinProfile kAudioPinProfiles[] = {
    {"Dev note IO8/IO9/IO26", 8, 9, 26},
    {"P1 header IO8/IO4/IO26", 8, 4, 26},
    {"Clock swap IO9/IO8/IO26", 9, 8, 26},
};

constexpr uint8_t kAudioPinProfileCount = sizeof(kAudioPinProfiles) / sizeof(kAudioPinProfiles[0]);
uint8_t activeAudioPinProfile = 0;

struct Station {
  const char *name;
  const char *tagline;
  const char *codec;
  const char *url;
};

const Station kStations[] = {
    {"KEXP", "Seattle music discovery", "AAC 160k", "https://kexp.streamguys1.com/kexp160.aac"},
    {"SomaFM Groove Salad", "Downtempo ambient", "MP3 128k", "https://ice5.somafm.com/groovesalad-128-mp3"},
    {"Radio Paradise", "Eclectic human-curated mix", "MP3", "https://stream.radioparadise.com/mp3-128"},
    {"BBC World Service", "Global news", "MP3", "http://stream.live.vc.bbcmedia.co.uk/bbc_world_service"},
    {"SomaFM Live", "Independent live sets", "MP3 128k", "https://ice5.somafm.com/live-128-mp3"},
};

constexpr uint8_t kStationCount = sizeof(kStations) / sizeof(kStations[0]);
uint8_t selectedStation = 0;
bool isPlaying = false;
lv_obj_t *stationTile = nullptr;
lv_obj_t *stationNameLabel = nullptr;
lv_obj_t *stationTaglineLabel = nullptr;
lv_obj_t *stationCodecLabel = nullptr;
lv_obj_t *stationIndexLabel = nullptr;
lv_obj_t *prevButton = nullptr;
lv_obj_t *playButton = nullptr;
lv_obj_t *nextButton = nullptr;
lv_obj_t *prevButtonLabel = nullptr;
lv_obj_t *playButtonLabel = nullptr;
lv_obj_t *nextButtonLabel = nullptr;

struct CandidateAp {
  int index = -1;
  int32_t rssi = -1000;
  int32_t channel = 0;
  uint8_t bssid[6] = {};
  const char *ssid = nullptr;
  const char *password = nullptr;
};

struct WifiProfile {
  const char *ssid;
  const char *password;
  bool requireFiveGhz;
};

const WifiProfile kWifiProfiles[] = {
    {WIFI_SSID, WIFI_PASSWORD, true},
    {WIFI_FALLBACK_SSID, WIFI_FALLBACK_PASSWORD, false},
};

constexpr uint8_t kWifiProfileCount = sizeof(kWifiProfiles) / sizeof(kWifiProfiles[0]);

bool isFiveGhzChannel(int32_t channel) {
  return channel > 14;
}

String macToString(const uint8_t *mac) {
  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buffer);
}

int16_t clampCoordinate(int32_t value, int16_t low, int16_t high) {
  if (value < low) {
    return low;
  }

  if (value > high) {
    return high;
  }

  return static_cast<int16_t>(value);
}

int16_t bestTwoAverage(int16_t a, int16_t b, int16_t c) {
  const int16_t ab = abs(a - b);
  const int16_t ac = abs(a - c);
  const int16_t bc = abs(b - c);

  if (ab <= ac && ab <= bc) {
    return (a + b) / 2;
  }

  if (ac <= ab && ac <= bc) {
    return (a + c) / 2;
  }

  return (b + c) / 2;
}

int16_t mapTouchAxis(int16_t value, int16_t rawMin, int16_t rawMax, int16_t outMin, int16_t outMax) {
  const int32_t mapped = (static_cast<int32_t>(value - rawMin) * (outMax - outMin)) / (rawMax - rawMin) + outMin;
  return clampCoordinate(mapped, min(outMin, outMax), max(outMin, outMax));
}

bool readTouchPoint(int16_t &x, int16_t &y, int16_t &pressure) {
  int16_t samples[6] = {};

  SPI.beginTransaction(SPISettings(kTouchSpiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(kTftCs, HIGH);
  digitalWrite(kSdCs, HIGH);
  digitalWrite(kTouchCs, LOW);

  SPI.transfer(0xB1);
  const int16_t z1 = SPI.transfer16(0xC1) >> 3;
  pressure = z1 + 4095;
  const int16_t z2 = SPI.transfer16(0x91) >> 3;
  pressure -= z2;

  if (pressure >= kTouchMinPressure) {
    SPI.transfer16(0x91);
    samples[0] = SPI.transfer16(0xD1) >> 3;
    samples[1] = SPI.transfer16(0x91) >> 3;
    samples[2] = SPI.transfer16(0xD1) >> 3;
    samples[3] = SPI.transfer16(0x91) >> 3;
  }

  samples[4] = SPI.transfer16(0xD0) >> 3;
  samples[5] = SPI.transfer16(0x0000) >> 3;
  digitalWrite(kTouchCs, HIGH);
  SPI.endTransaction();

  if (pressure < kTouchMinPressure) {
    x = 0;
    y = 0;
    pressure = 0;
    return false;
  }

  const int16_t rawA = bestTwoAverage(samples[0], samples[2], samples[4]);
  const int16_t rawB = bestTwoAverage(samples[1], samples[3], samples[5]);
  int16_t orientedX = rawA;
  int16_t orientedY = rawB;

  switch (kTouchRotation) {
    case 0:
      orientedX = 4095 - rawB;
      orientedY = rawA;
      break;
    case 1:
      orientedX = rawA;
      orientedY = rawB;
      break;
    case 2:
      orientedX = rawB;
      orientedY = 4095 - rawA;
      break;
    default:
      orientedX = 4095 - rawA;
      orientedY = 4095 - rawB;
      break;
  }

  x = mapTouchAxis(orientedX, kTouchRawMinX, kTouchRawMaxX, 0, kScreenWidth - 1);
  y = mapTouchAxis(orientedY, kTouchRawMinY, kTouchRawMaxY, 0, kScreenHeight - 1);

  const uint32_t now = millis();
  if (now >= nextTouchDiagnosticMs) {
    nextTouchDiagnosticMs = now + kTouchDiagnosticIntervalMs;
    Serial.print("Touch raw=(");
    Serial.print(rawA);
    Serial.print(",");
    Serial.print(rawB);
    Serial.print(") oriented=(");
    Serial.print(orientedX);
    Serial.print(",");
    Serial.print(orientedY);
    Serial.print(") xy=(");
    Serial.print(x);
    Serial.print(",");
    Serial.print(y);
    Serial.print(") pressure=");
    Serial.println(pressure);
    if (detailLabel) {
      lv_label_set_text_fmt(detailLabel, "Touch: %d,%d", x, y);
    }
  }

  return true;
}

void readTouch(lv_indev_drv_t *driver, lv_indev_data_t *data) {
  (void)driver;

  int16_t x = 0;
  int16_t y = 0;
  int16_t pressure = 0;
  if (!readTouchPoint(x, y, pressure)) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  data->state = LV_INDEV_STATE_PR;
  data->point.x = x;
  data->point.y = y;
}

void lvglFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color) {
  const int32_t width = area->x2 - area->x1 + 1;
  const int32_t height = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t *>(&color->full), width, height);
  lv_disp_flush_ready(disp);
}

void renderNow() {
  for (int i = 0; i < 4; ++i) {
    const uint32_t now = millis();
    lv_tick_inc(now - lastLvTickMs);
    lastLvTickMs = now;
    lv_timer_handler();
    delay(5);
  }
}

void setDotColor(lv_color_t color) {
  lv_style_set_bg_color(&styleDot, color);
  lv_obj_report_style_change(&styleDot);
}

void setProgress(int value) {
  (void)value;
}

void setStatus(const char *state, const char *detail, lv_color_t dotColor, int progress) {
  lv_label_set_text(stateLabel, state);
  lv_label_set_text(detailLabel, detail);
  setDotColor(dotColor);
  setProgress(progress);
  renderNow();
}

bool checkEsp(esp_err_t result, const char *step) {
  if (result == ESP_OK) {
    return true;
  }

  Serial.print("I2S ");
  Serial.print(step);
  Serial.print(" failed: ");
  Serial.println(esp_err_to_name(result));
  return false;
}

void buildAudioSineTable() {
  constexpr float kTwoPi = 6.28318530718f;

  for (size_t i = 0; i < kAudioSineTableSize; ++i) {
    const float phase = (static_cast<float>(i) * kTwoPi) / static_cast<float>(kAudioSineTableSize);
    audioSineTable[i] = static_cast<int16_t>(sinf(phase) * kAudioToneAmplitude);
  }
}

void deinitAudioOutput() {
  audioTonePlaying = false;

  if (audioTxChannel) {
    i2s_channel_disable(audioTxChannel);
    i2s_del_channel(audioTxChannel);
    audioTxChannel = nullptr;
  }

  audioReady = false;
  audioPhase = 0;
}

bool initAudioOutput(uint8_t profileIndex = activeAudioPinProfile) {
  if (profileIndex >= kAudioPinProfileCount) {
    profileIndex = 0;
  }

  if (audioReady && profileIndex == activeAudioPinProfile) {
    return true;
  }

  if (audioReady || audioTxChannel) {
    deinitAudioOutput();
  }

  activeAudioPinProfile = profileIndex;
  const AudioPinProfile &profile = kAudioPinProfiles[activeAudioPinProfile];

  Serial.println();
  Serial.println("Initializing MAX98357A I2S output.");
  Serial.print("Profile: ");
  Serial.println(profile.name);
  Serial.print("BCLK=IO");
  Serial.print(profile.bclk);
  Serial.print(" LRC=IO");
  Serial.print(profile.lrc);
  Serial.print(" DIN=IO");
  Serial.println(profile.dout);
  buildAudioSineTable();

  i2s_chan_config_t channelConfig = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  channelConfig.dma_desc_num = 4;
  channelConfig.dma_frame_num = 256;

  if (!checkEsp(i2s_new_channel(&channelConfig, &audioTxChannel, nullptr), "new channel")) {
    setStatus("Audio", "I2S setup failed", lv_color_hex(0xff5a5f), 12);
    return false;
  }

  i2s_std_config_t stdConfig = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kAudioSampleRate),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = static_cast<gpio_num_t>(profile.bclk),
          .ws = static_cast<gpio_num_t>(profile.lrc),
          .dout = static_cast<gpio_num_t>(profile.dout),
          .din = I2S_GPIO_UNUSED,
          .invert_flags = {
              .mclk_inv = false,
              .bclk_inv = false,
              .ws_inv = false,
          },
      },
  };

  if (!checkEsp(i2s_channel_init_std_mode(audioTxChannel, &stdConfig), "init std mode")) {
    i2s_del_channel(audioTxChannel);
    audioTxChannel = nullptr;
    setStatus("Audio", "I2S setup failed", lv_color_hex(0xff5a5f), 12);
    return false;
  }

  audioReady = true;
  Serial.println("I2S output ready.");
  return true;
}

void fillToneBuffer(int16_t *samples, size_t frameCount) {
  const uint32_t phaseStep = static_cast<uint32_t>((static_cast<uint64_t>(kAudioToneHz) << 32) / kAudioSampleRate);

  for (size_t frame = 0; frame < frameCount; ++frame) {
    audioPhase += phaseStep;
    const uint8_t tableIndex = static_cast<uint8_t>(audioPhase >> 24);
    const int16_t sample = audioSineTable[tableIndex];
    samples[frame * 2] = sample;
    samples[frame * 2 + 1] = sample;
  }
}

void audioTask(void *parameter) {
  (void)parameter;

  for (;;) {
    if (!audioReady || !audioTonePlaying) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    fillToneBuffer(audioSampleBuffer, kAudioFramesPerBuffer);

    size_t bytesWritten = 0;
    const esp_err_t writeResult =
        i2s_channel_write(audioTxChannel, audioSampleBuffer, sizeof(audioSampleBuffer), &bytesWritten, 100);
    if (writeResult != ESP_OK) {
      checkEsp(writeResult, "write");
      audioTonePlaying = false;
      i2s_channel_disable(audioTxChannel);
    }
  }
}

void ensureAudioTask() {
  if (audioTaskHandle) {
    return;
  }

  xTaskCreate(audioTask, "audio", 4096, nullptr, 3, &audioTaskHandle);
}

void writeSilence() {
  if (!audioTxChannel) {
    return;
  }

  memset(audioSampleBuffer, 0, sizeof(audioSampleBuffer));
  size_t bytesWritten = 0;
  i2s_channel_write(audioTxChannel, audioSampleBuffer, sizeof(audioSampleBuffer), &bytesWritten, 100);
}

bool playBlockingToneOnProfile(uint8_t profileIndex, uint32_t durationMs) {
  if (!initAudioOutput(profileIndex)) {
    return false;
  }

  if (!checkEsp(i2s_channel_enable(audioTxChannel), "enable")) {
    return false;
  }

  audioPhase = 0;
  const uint32_t startedAt = millis();
  while (millis() - startedAt < durationMs) {
    fillToneBuffer(audioSampleBuffer, kAudioFramesPerBuffer);

    size_t bytesWritten = 0;
    const esp_err_t writeResult =
        i2s_channel_write(audioTxChannel, audioSampleBuffer, sizeof(audioSampleBuffer), &bytesWritten, 1000);
    if (!checkEsp(writeResult, "write")) {
      break;
    }

    renderNow();
  }

  writeSilence();
  i2s_channel_disable(audioTxChannel);
  return true;
}

bool playBlockingTone(uint32_t durationMs) {
  return playBlockingToneOnProfile(activeAudioPinProfile, durationMs);
}

bool playAudioDiagnosticSweep(uint32_t durationMsPerProfile) {
  bool allWritesOk = true;

  for (uint8_t i = 0; i < kAudioPinProfileCount; ++i) {
    const AudioPinProfile &profile = kAudioPinProfiles[i];
    Serial.println();
    Serial.print("Audio pin test ");
    Serial.print(i + 1);
    Serial.print("/");
    Serial.print(kAudioPinProfileCount);
    Serial.print(": ");
    Serial.println(profile.name);

    setStatus("Audio", profile.name, lv_color_hex(0xffc857), 40 + (i * 15));
    allWritesOk = playBlockingToneOnProfile(i, durationMsPerProfile) && allWritesOk;
    delay(kAudioPinTestGapMs);
    renderNow();
  }

  return allWritesOk;
}

bool startAudioTone() {
  audioTonePlaying = true;
  Serial.println("Audio diagnostic sweep started.");
  const bool played = playAudioDiagnosticSweep(kAudioPreviewMs);
  audioTonePlaying = false;
  Serial.println("Audio diagnostic sweep finished.");
  return played;
}

bool startContinuousAudioTone() {
  if (!initAudioOutput()) {
    return false;
  }

  if (!audioTonePlaying && !checkEsp(i2s_channel_enable(audioTxChannel), "enable")) {
    return false;
  }

  audioTonePlaying = true;
  audioPhase = 0;
  ensureAudioTask();
  Serial.println("Audio continuous preview tone started.");
  return true;
}

void stopAudioTone() {
  if (!audioReady || !audioTonePlaying) {
    return;
  }

  audioTonePlaying = false;
  vTaskDelay(pdMS_TO_TICKS(25));
  writeSilence();
  i2s_channel_disable(audioTxChannel);
  Serial.println("Audio preview tone stopped.");
}

void pumpAudio() {
  if (!audioReady || !audioTonePlaying) {
    return;
  }

  fillToneBuffer(audioSampleBuffer, kAudioFramesPerBuffer);

  size_t bytesWritten = 0;
  const esp_err_t writeResult = i2s_channel_write(audioTxChannel, audioSampleBuffer, sizeof(audioSampleBuffer), &bytesWritten, 50);
  if (writeResult != ESP_OK && writeResult != ESP_ERR_TIMEOUT) {
    checkEsp(writeResult, "write");
    stopAudioTone();
  }
}

void setMetric(lv_obj_t *label, const char *name, const String &value) {
  lv_label_set_text_fmt(label, "%s: %s", name, value.c_str());
}

void setMetric(lv_obj_t *label, const char *name, int32_t value) {
  lv_label_set_text_fmt(label, "%s: %ld", name, static_cast<long>(value));
}

void updateStationUi() {
  const Station &station = kStations[selectedStation];
  lv_label_set_text_fmt(ssidLabel, "Now: %s", station.name);
  lv_label_set_text_fmt(bssidLabel, "%s | %s", station.codec, station.tagline);
  lv_label_set_text(stationNameLabel, station.name);
  lv_label_set_text(stationTaglineLabel, station.tagline);
  lv_label_set_text(stationCodecLabel, station.codec);
  lv_label_set_text_fmt(stationIndexLabel, "%u / %u", selectedStation + 1, kStationCount);
  lv_label_set_text(playButtonLabel, isPlaying ? "Pause" : "Play");
  lv_label_set_text(detailLabel, isPlaying ? "Playing audio preview" : "Selected stream ready");
}

void selectStation(uint8_t index) {
  if (index >= kStationCount) {
    return;
  }

  selectedStation = index;
  const Station &station = kStations[selectedStation];
  Serial.print("Selected station: ");
  Serial.print(station.name);
  Serial.print(" -> ");
  Serial.println(station.url);
  lv_label_set_text(stateLabel, "Selected");
  setDotColor(lv_color_hex(0x56cfe1));
  setProgress(82);
  updateStationUi();
  renderNow();
}

void selectRelativeStation(int8_t delta) {
  const int next = (static_cast<int>(selectedStation) + delta + kStationCount) % kStationCount;
  stopAudioTone();
  isPlaying = false;
  selectStation(static_cast<uint8_t>(next));
}

void togglePlay() {
  if (isPlaying) {
    stopAudioTone();
    isPlaying = false;
  } else {
    isPlaying = true;
    lv_label_set_text(stateLabel, "Playing");
    setDotColor(lv_color_hex(0x57cc99));
    updateStationUi();
    renderNow();

    startAudioTone();
    isPlaying = false;
  }

  Serial.print("Preview test: ");
  Serial.println(kStations[selectedStation].name);
  lv_label_set_text(stateLabel, "Ready");
  setDotColor(lv_color_hex(0x56cfe1));
  updateStationUi();
  renderNow();
}

void onPrevClicked(lv_event_t *event) {
  (void)event;
  selectRelativeStation(-1);
}

void onPlayClicked(lv_event_t *event) {
  (void)event;
  togglePlay();
}

void onNextClicked(lv_event_t *event) {
  (void)event;
  selectRelativeStation(1);
}

void initDisplay() {
  pinMode(kTftCs, OUTPUT);
  pinMode(kTouchCs, OUTPUT);
  pinMode(kSdCs, OUTPUT);
  digitalWrite(kTftCs, HIGH);
  digitalWrite(kTouchCs, HIGH);
  digitalWrite(kSdCs, HIGH);

  pinMode(kTftBacklight, OUTPUT);
  digitalWrite(kTftBacklight, HIGH);

  gfx->begin(40000000);
  gfx->fillScreen(0x0000);

  lv_init();
  lv_disp_draw_buf_init(&drawBuffer, drawBuffer1, drawBuffer2, kScreenWidth * kDrawBufferRows);
  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = kScreenWidth;
  displayDriver.ver_res = kScreenHeight;
  displayDriver.flush_cb = lvglFlush;
  displayDriver.draw_buf = &drawBuffer;
  lv_disp_drv_register(&displayDriver);

  SPI.begin(kTftSck, kTftMiso, kTftMosi, kTouchCs);
  lv_indev_drv_init(&inputDriver);
  inputDriver.type = LV_INDEV_TYPE_POINTER;
  inputDriver.read_cb = readTouch;
  lv_indev_drv_register(&inputDriver);

  lastLvTickMs = millis();
}

lv_obj_t *makeLabel(lv_obj_t *parent, lv_style_t *style) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_add_style(label, style, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, 288);
  return label;
}

void initUi() {
  lv_style_init(&styleScreen);
  lv_style_set_bg_color(&styleScreen, lv_color_hex(0x101418));
  lv_style_set_pad_all(&styleScreen, 0);

  lv_style_init(&styleCard);
  lv_style_set_bg_color(&styleCard, lv_color_hex(0x182026));
  lv_style_set_border_color(&styleCard, lv_color_hex(0x2f3c45));
  lv_style_set_border_width(&styleCard, 1);
  lv_style_set_radius(&styleCard, 6);
  lv_style_set_pad_all(&styleCard, 14);

  lv_style_init(&styleTitle);
  lv_style_set_text_color(&styleTitle, lv_color_hex(0xf3f7fa));
  lv_style_set_text_font(&styleTitle, &lv_font_montserrat_20);

  lv_style_init(&styleValue);
  lv_style_set_text_color(&styleValue, lv_color_hex(0xdce7ee));
  lv_style_set_text_font(&styleValue, &lv_font_montserrat_14);

  lv_style_init(&styleMuted);
  lv_style_set_text_color(&styleMuted, lv_color_hex(0x9eb2bd));
  lv_style_set_text_font(&styleMuted, &lv_font_montserrat_14);

  lv_style_init(&styleDot);
  lv_style_set_radius(&styleDot, LV_RADIUS_CIRCLE);
  lv_style_set_bg_color(&styleDot, lv_color_hex(0xffc857));

  lv_style_init(&styleStationRow);
  lv_style_set_bg_color(&styleStationRow, lv_color_hex(0x22303a));
  lv_style_set_border_color(&styleStationRow, lv_color_hex(0x344651));
  lv_style_set_border_width(&styleStationRow, 1);
  lv_style_set_radius(&styleStationRow, 5);
  lv_style_set_pad_left(&styleStationRow, 8);
  lv_style_set_pad_right(&styleStationRow, 8);
  lv_style_set_pad_top(&styleStationRow, 4);
  lv_style_set_pad_bottom(&styleStationRow, 3);

  lv_style_init(&styleStationSelected);
  lv_style_set_bg_color(&styleStationSelected, lv_color_hex(0x1f6f8b));
  lv_style_set_border_color(&styleStationSelected, lv_color_hex(0x56cfe1));

  lv_style_init(&styleStationText);
  lv_style_set_text_color(&styleStationText, lv_color_hex(0xdce7ee));
  lv_style_set_text_font(&styleStationText, &lv_font_montserrat_14);

  lv_style_init(&styleStationSelectedText);
  lv_style_set_text_color(&styleStationSelectedText, lv_color_hex(0xffffff));

  lv_style_init(&styleHero);
  lv_style_set_bg_color(&styleHero, lv_color_hex(0x22303a));
  lv_style_set_border_color(&styleHero, lv_color_hex(0x3f5966));
  lv_style_set_border_width(&styleHero, 1);
  lv_style_set_radius(&styleHero, 6);
  lv_style_set_pad_all(&styleHero, 12);

  lv_style_init(&styleControl);
  lv_style_set_bg_color(&styleControl, lv_color_hex(0x26333c));
  lv_style_set_border_color(&styleControl, lv_color_hex(0x3b4d58));
  lv_style_set_border_width(&styleControl, 1);
  lv_style_set_radius(&styleControl, 6);
  lv_style_set_text_color(&styleControl, lv_color_hex(0xe8f1f5));
  lv_style_set_text_font(&styleControl, &lv_font_montserrat_14);

  lv_style_init(&styleControlPrimary);
  lv_style_set_bg_color(&styleControlPrimary, lv_color_hex(0x1f6f8b));
  lv_style_set_border_color(&styleControlPrimary, lv_color_hex(0x56cfe1));

  lv_obj_t *screen = lv_scr_act();
  lv_obj_add_style(screen, &styleScreen, 0);

  lv_obj_t *card = lv_obj_create(screen);
  lv_obj_remove_style_all(card);
  lv_obj_add_style(card, &styleCard, 0);
  lv_obj_set_size(card, 304, 224);
  lv_obj_center(card);

  statusDot = lv_obj_create(card);
  lv_obj_remove_style_all(statusDot);
  lv_obj_add_style(statusDot, &styleDot, 0);
  lv_obj_set_size(statusDot, 14, 14);
  lv_obj_align(statusDot, LV_ALIGN_TOP_LEFT, 0, 3);

  titleLabel = makeLabel(card, &styleTitle);
  lv_label_set_text(titleLabel, "Internet Radio");
  lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 22, 0);

  stateLabel = makeLabel(card, &styleValue);
  lv_obj_set_width(stateLabel, 92);
  lv_label_set_text(stateLabel, "Booting");
  lv_obj_align(stateLabel, LV_ALIGN_TOP_RIGHT, 0, 2);

  ssidLabel = makeLabel(card, &styleValue);
  lv_label_set_text(ssidLabel, "Now: KEXP");
  lv_obj_align(ssidLabel, LV_ALIGN_TOP_LEFT, 0, 30);

  bssidLabel = makeLabel(card, &styleMuted);
  lv_label_set_text(bssidLabel, "AAC 160k | Seattle music discovery");
  lv_obj_align(bssidLabel, LV_ALIGN_TOP_LEFT, 0, 50);

  stationTile = lv_obj_create(card);
  lv_obj_remove_style_all(stationTile);
  lv_obj_add_style(stationTile, &styleHero, 0);
  lv_obj_set_size(stationTile, 276, 94);
  lv_obj_align(stationTile, LV_ALIGN_TOP_LEFT, 0, 74);

  stationIndexLabel = lv_label_create(stationTile);
  lv_obj_add_style(stationIndexLabel, &styleMuted, 0);
  lv_label_set_text(stationIndexLabel, "1 / 5");
  lv_obj_align(stationIndexLabel, LV_ALIGN_TOP_RIGHT, 0, 0);

  stationNameLabel = lv_label_create(stationTile);
  lv_obj_add_style(stationNameLabel, &styleTitle, 0);
  lv_label_set_long_mode(stationNameLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_width(stationNameLabel, 178);
  lv_label_set_text(stationNameLabel, "KEXP");
  lv_obj_align(stationNameLabel, LV_ALIGN_TOP_LEFT, 0, 0);

  stationCodecLabel = lv_label_create(stationTile);
  lv_obj_add_style(stationCodecLabel, &styleStationText, 0);
  lv_label_set_text(stationCodecLabel, "AAC 160k");
  lv_obj_align(stationCodecLabel, LV_ALIGN_TOP_LEFT, 0, 34);

  stationTaglineLabel = lv_label_create(stationTile);
  lv_obj_add_style(stationTaglineLabel, &styleMuted, 0);
  lv_label_set_long_mode(stationTaglineLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(stationTaglineLabel, 248);
  lv_label_set_text(stationTaglineLabel, "Seattle music discovery");
  lv_obj_align(stationTaglineLabel, LV_ALIGN_TOP_LEFT, 0, 58);

  prevButton = lv_btn_create(card);
  lv_obj_remove_style_all(prevButton);
  lv_obj_add_style(prevButton, &styleControl, 0);
  lv_obj_set_size(prevButton, 74, 34);
  lv_obj_align(prevButton, LV_ALIGN_BOTTOM_LEFT, 0, -24);
  lv_obj_add_event_cb(prevButton, onPrevClicked, LV_EVENT_CLICKED, nullptr);

  prevButtonLabel = lv_label_create(prevButton);
  lv_label_set_text(prevButtonLabel, "<");
  lv_obj_center(prevButtonLabel);

  playButton = lv_btn_create(card);
  lv_obj_remove_style_all(playButton);
  lv_obj_add_style(playButton, &styleControl, 0);
  lv_obj_add_style(playButton, &styleControlPrimary, 0);
  lv_obj_set_size(playButton, 112, 34);
  lv_obj_align(playButton, LV_ALIGN_BOTTOM_MID, 0, -24);
  lv_obj_add_event_cb(playButton, onPlayClicked, LV_EVENT_CLICKED, nullptr);

  playButtonLabel = lv_label_create(playButton);
  lv_label_set_text(playButtonLabel, "Play");
  lv_obj_center(playButtonLabel);

  nextButton = lv_btn_create(card);
  lv_obj_remove_style_all(nextButton);
  lv_obj_add_style(nextButton, &styleControl, 0);
  lv_obj_set_size(nextButton, 74, 34);
  lv_obj_align(nextButton, LV_ALIGN_BOTTOM_RIGHT, 0, -24);
  lv_obj_add_event_cb(nextButton, onNextClicked, LV_EVENT_CLICKED, nullptr);

  nextButtonLabel = lv_label_create(nextButton);
  lv_label_set_text(nextButtonLabel, ">");
  lv_obj_center(nextButtonLabel);

  detailLabel = makeLabel(card, &styleMuted);
  lv_obj_set_width(detailLabel, 276);
  lv_label_set_long_mode(detailLabel, LV_LABEL_LONG_DOT);
  lv_label_set_text(detailLabel, "Starting display");
  lv_obj_align(detailLabel, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  updateStationUi();
  renderNow();
}

void printEncryption(wifi_auth_mode_t type) {
  switch (type) {
    case WIFI_AUTH_OPEN:
      Serial.print("open");
      break;
    case WIFI_AUTH_WEP:
      Serial.print("WEP");
      break;
    case WIFI_AUTH_WPA_PSK:
      Serial.print("WPA");
      break;
    case WIFI_AUTH_WPA2_PSK:
      Serial.print("WPA2");
      break;
    case WIFI_AUTH_WPA_WPA2_PSK:
      Serial.print("WPA/WPA2");
      break;
    case WIFI_AUTH_WPA2_ENTERPRISE:
      Serial.print("WPA2 Enterprise");
      break;
    case WIFI_AUTH_WPA3_PSK:
      Serial.print("WPA3");
      break;
    case WIFI_AUTH_WPA2_WPA3_PSK:
      Serial.print("WPA2/WPA3");
      break;
    default:
      Serial.print("unknown");
      break;
  }
}

CandidateAp scanForBestAp(const WifiProfile &profile) {
  CandidateAp best;
  best.ssid = profile.ssid;
  best.password = profile.password;

  Serial.println();
  Serial.print("Scanning for SSID: ");
  Serial.println(profile.ssid);
  setStatus("Loading", "Preparing stations", lv_color_hex(0xffc857), 22);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(kScanDelayMs);

  const int networkCount = WiFi.scanNetworks(false, true);
  if (networkCount < 0) {
    Serial.print("Wi-Fi scan failed: ");
    Serial.println(networkCount);
    setStatus("Offline", "Station list ready", lv_color_hex(0xff5a5f), 8);
    return best;
  }

  Serial.print("Networks found: ");
  Serial.println(networkCount);
  updateStationUi();
  setProgress(35);
  renderNow();

  for (int i = 0; i < networkCount; ++i) {
    const String ssid = WiFi.SSID(i);
    const int32_t channel = WiFi.channel(i);
    const int32_t rssi = WiFi.RSSI(i);
    const bool ssidMatches = ssid == profile.ssid;
    const bool isFiveGhz = isFiveGhzChannel(channel);

    Serial.print("  ");
    Serial.print(i);
    Serial.print(": ");
    Serial.print(ssid.length() ? ssid : String("<hidden>"));
    Serial.print("  BSSID=");
    Serial.print(WiFi.BSSIDstr(i));
    Serial.print("  channel=");
    Serial.print(channel);
    Serial.print(isFiveGhz ? " (5 GHz)" : " (2.4 GHz)");
    Serial.print("  RSSI=");
    Serial.print(rssi);
    Serial.print(" dBm  auth=");
    printEncryption(WiFi.encryptionType(i));
    Serial.println();

    if (ssidMatches && (!profile.requireFiveGhz || isFiveGhz) && rssi > best.rssi) {
      best.index = i;
      best.rssi = rssi;
      best.channel = channel;
      memcpy(best.bssid, WiFi.BSSID(i), sizeof(best.bssid));
    }
  }

  if (best.index < 0) {
    Serial.print("No ");
    Serial.print(profile.requireFiveGhz ? "5 GHz " : "");
    Serial.println("AP found for the configured SSID.");
    setStatus("Offline", "Station list ready", lv_color_hex(0xff5a5f), 12);
    updateStationUi();
  } else {
    const String bssid = macToString(best.bssid);
    Serial.print("Selected AP: SSID=");
    Serial.print(profile.ssid);
    Serial.print(" channel=");
    Serial.print(best.channel);
    Serial.print(" BSSID=");
    Serial.print(bssid);
    Serial.print(" RSSI=");
    Serial.print(best.rssi);
    Serial.println(" dBm");

    updateStationUi();
    setStatus("Loading", "Preparing station list", lv_color_hex(0x56cfe1), 55);
  }

  WiFi.scanDelete();
  return best;
}

bool connectToCandidate(const CandidateAp &ap) {
  if (ap.index < 0) {
    return false;
  }

  Serial.println();
  Serial.print("Connecting to selected AP for ");
  Serial.print(ap.ssid);
  Serial.println("...");
  setStatus("Loading", "Preparing stations", lv_color_hex(0x56cfe1), 65);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ap.ssid, ap.password, ap.channel, ap.bssid, true);

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < kConnectTimeoutMs) {
    delay(250);
    Serial.print(".");
    renderNow();
  }
  Serial.println();

  return WiFi.status() == WL_CONNECTED;
}

bool connectToAvailableWifi() {
  for (uint8_t i = 0; i < kWifiProfileCount; ++i) {
    const WifiProfile &profile = kWifiProfiles[i];
    const CandidateAp ap = scanForBestAp(profile);

    if (connectToCandidate(ap)) {
      return true;
    }

    Serial.println();
    Serial.print("Could not connect to ");
    Serial.print(profile.ssid);
    Serial.println(".");

    if (i + 1 < kWifiProfileCount) {
      Serial.print("Trying fallback SSID: ");
      Serial.println(kWifiProfiles[i + 1].ssid);
    }
  }

  return false;
}

void updateConnectionUi() {
  if (WiFi.status() != WL_CONNECTED) {
    setStatus("Offline", "Station list ready", lv_color_hex(0xff5a5f), 18);
    updateStationUi();
    return;
  }

  setStatus("Ready", "Station list ready", lv_color_hex(0x57cc99), 100);
  updateStationUi();
}

void printConnectionReport() {
  Serial.println();
  Serial.println("Wi-Fi connected.");
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("BSSID: ");
  Serial.println(WiFi.BSSIDstr());
  Serial.print("Channel: ");
  Serial.print(WiFi.channel());
  Serial.println(isFiveGhzChannel(WiFi.channel()) ? " (5 GHz)" : " (not 5 GHz)");
  Serial.print("RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  Serial.print("IPv4: ");
  Serial.println(WiFi.localIP());
  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("DNS: ");
  Serial.println(WiFi.dnsIP());
}
}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(1500);

  initDisplay();
  initUi();
  setStatus("Audio", "Boot pin test", lv_color_hex(0xffc857), 40);
  playAudioDiagnosticSweep(900);

  Serial.println();
  Serial.println("ESP32-C5 5 GHz Wi-Fi connection test");
  Serial.print("Target SSID: ");
  Serial.println(WIFI_SSID);
  Serial.print("Fallback SSID: ");
  Serial.println(WIFI_FALLBACK_SSID);
  setStatus("Starting", "Loading station list", lv_color_hex(0xffc857), 12);

  if (!connectToAvailableWifi()) {
    Serial.println();
    Serial.println("Connection failed.");
    Serial.println("Check that one configured AP is broadcasting, the passwords are correct, and the board is close enough to the AP.");
    setStatus("Offline", "Station list ready", lv_color_hex(0xff5a5f), 16);
    updateStationUi();
    return;
  }

  printConnectionReport();
  updateConnectionUi();
}

void loop() {
  static wl_status_t lastStatus = WL_IDLE_STATUS;
  static uint32_t lastReportAt = 0;

  const uint32_t now = millis();
  lv_tick_inc(now - lastLvTickMs);
  lastLvTickMs = now;
  lv_timer_handler();

  const wl_status_t status = WiFi.status();
  if (status != lastStatus) {
    Serial.print("Wi-Fi status changed: ");
    Serial.println(static_cast<int>(status));
    lastStatus = status;
    updateConnectionUi();
  }

  if (millis() - lastReportAt > 5000) {
    lastReportAt = millis();
    if (status == WL_CONNECTED) {
      Serial.print("Still connected on channel ");
      Serial.print(WiFi.channel());
      Serial.print(" with RSSI ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
      updateConnectionUi();
    } else {
      Serial.print("Heartbeat; Wi-Fi status=");
      Serial.println(static_cast<int>(status));
      updateStationUi();
      renderNow();
    }
  }

  delay(audioTonePlaying ? 1 : 10);
}
