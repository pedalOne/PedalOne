#pragma once

#include <Arduino.h>
#include <BLEDevice.h>

class OtaUpdate {
 public:
  using StartAllowedCallback = bool (*)();
  static constexpr char SERVICE_UUID[] = "8E400010-F315-4F60-9FB8-838830DAEA50";
  static constexpr char CONTROL_UUID[] = "8E400011-F315-4F60-9FB8-838830DAEA50";
  static constexpr char DATA_UUID[] = "8E400012-F315-4F60-9FB8-838830DAEA50";
  static constexpr char STATUS_UUID[] = "8E400013-F315-4F60-9FB8-838830DAEA50";

  void begin(BLEServer *server, const char *firmwareVersion,
             StartAllowedCallback startAllowed);
  void loop(bool connected);
  bool active() const { return active_; }
  bool rebootPending() const { return rebootAtMs_ != 0; }
  uint8_t progressPercent() const;

 private:
  class ControlCallbacks;
  class DataCallbacks;
  void handleControl(const String &command);
  void handleData(const uint8_t *data, size_t length);
  void publish(const String &status, bool notify = true);
  void fail(const char *reason);
  void abortUpdate(const char *status);
  bool validMd5(const String &md5) const;

  BLECharacteristic *statusCharacteristic_ = nullptr;
  String firmwareVersion_;
  StartAllowedCallback startAllowed_ = nullptr;
  volatile bool active_ = false;
  bool connected_ = false;
  size_t expectedSize_ = 0;
  volatile size_t bytesWritten_ = 0;
  size_t nextProgressNotification_ = 0;
  uint32_t rebootAtMs_ = 0;
};

