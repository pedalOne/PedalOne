#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Provisioning only: map transfer is a separate, versioned storage protocol.
class WifiConfig {
 public:
  static constexpr char SERVICE_UUID[] = "8E400020-F315-4F60-9FB8-838830DAEA50";
  static constexpr char CONTROL_UUID[] = "8E400021-F315-4F60-9FB8-838830DAEA50";
  static constexpr char STATUS_UUID[] = "8E400022-F315-4F60-9FB8-838830DAEA50";
  void begin(BLEServer *server);
  void loop(bool allowed, bool bluetoothConnected);
  void stop(bool report = true);
 private:
  class Callbacks;
  struct Command { uint8_t size; uint8_t bytes[100]; };
  struct Credentials { char ssid[33]; char password[65]; };
  void publish(uint8_t state);
  void connect(const Credentials &credentials, bool save);
  QueueHandle_t queue_ = nullptr;
  BLECharacteristic *status_ = nullptr;
  Credentials pending_{};
  String savedSSID_;
  bool connecting_ = false, online_ = false, save_ = false, notify_ = false;
  uint32_t started_ = 0;
  uint8_t state_ = 0;
};
