#pragma once

#include <Arduino.h>
#include <Wire.h>

struct OnboardGpsFix {
  uint16_t sequence = 0;
  uint32_t timestamp = 0;
  int32_t latitudeE7 = 0;
  int32_t longitudeE7 = 0;
  int16_t altitudeDm = 0;
  uint16_t speedCms = 0;
  uint16_t courseDeg100 = 0;
  uint16_t accuracyCm = 0;
  uint8_t flags = 0;
};

// Production LC76G driver for the Waveshare GPS accessory. The receiver is
// reached through its I2C bridge and supplies standard NMEA sentences.
class OnboardGps {
 public:
  bool begin(TwoWire &wire, uint32_t now);
  void service(uint32_t now);
  bool takeFix(uint16_t &lastSequence, OnboardGpsFix &fix) const;
  bool detected() const { return present_; }
  bool fixFresh(uint32_t now) const;
  bool everHadFix() const { return everHadFix_; }
  uint8_t satellites() const { return satellites_; }
  void prepareForSleep();
  bool prepareForHardPowerOff();
  void resumeAfterLightSleep(uint32_t now);

 private:
  enum ConfigStage : uint8_t {
    CONFIG_WAKE,
    CONFIG_RATE,
    CONFIG_REBOOT_WAIT,
    CONFIG_OUTPUTS,
    CONFIG_QUERY,
    CONFIG_QUERY_WAIT,
    CONFIG_DONE
  };

  bool commandWrite(const uint8_t *data, size_t length);
  bool readRegister(uint16_t offset, uint8_t *data, size_t length,
                    uint8_t attempts = 3);
  bool writeReceiver(const uint8_t *data, size_t length);
  bool sendPair(const char *sentence);
  bool sendPairBody(const char *body);
  bool pollReceiver(uint32_t now);
  void serviceConfiguration(uint32_t now);
  void serviceWatchdog(uint32_t now);
  void serviceAdaptiveRate(uint32_t now);
  bool setFixInterval(uint16_t intervalMs, uint32_t now);
  void feedNmea(char value);
  void parseNmea(char *line, uint32_t now);
  void parseGga(char *fields[], int count, uint32_t now);
  void parseRmc(char *fields[], int count, uint32_t now);
  void parseGsa(char *fields[], int count, uint32_t now);
  void publishFix(uint32_t now);
  void setClockFromGps(const char *utc, const char *date);

  TwoWire *wire_ = nullptr;
  bool present_ = false;
  bool hasPosition_ = false;
  bool everHadFix_ = false;
  bool hasTime_ = false;
  bool rateReplySeen_ = false;
  uint8_t fixType_ = 1;
  uint8_t fixQuality_ = 0;
  uint8_t satellites_ = 0;
  uint32_t lastDiagnosticMs_ = 0;
  double latitude_ = NAN;
  double longitude_ = NAN;
  float altitudeM_ = NAN;
  float speedKnots_ = NAN;
  float courseDegrees_ = NAN;
  float hdop_ = NAN;
  uint32_t lastPositionMs_ = 0;
  uint32_t lastFixSentenceMs_ = 0;
  uint32_t lastNmeaMs_ = 0;
  uint32_t lastSpeedMs_ = 0;
  uint32_t lastAltitudeMs_ = 0;
  uint32_t acquisitionStartMs_ = 0;
  uint32_t lastPollMs_ = 0;
  uint32_t lastRecoveryMs_ = 0;
  uint32_t stationarySinceMs_ = 0;
  uint32_t lastRateChangeMs_ = 0;
  uint16_t fixIntervalMs_ = 1000;
  uint8_t readFailures_ = 0;
  uint8_t recoveryAttempts_ = 0;
  char nmeaLine_[192] = {};
  size_t nmeaLength_ = 0;
  ConfigStage configStage_ = CONFIG_WAKE;
  uint32_t configDeadlineMs_ = 0;
  uint8_t outputCommand_ = 0;
  OnboardGpsFix latestFix_;
};
