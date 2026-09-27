#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class RiderNetwork {
 public:
  static constexpr uint8_t MAX_RIDERS = 20;
  static constexpr uint32_t RIDER_TIMEOUT_MS = 15000;
  static constexpr uint8_t ICON_WIDTH = 11;
  static constexpr uint8_t ICON_HEIGHT = 11;
  static constexpr uint16_t ICON_PIXELS = ICON_WIDTH * ICON_HEIGHT;

  struct Rider {
    uint32_t id = 0;
    char callSign[5] = {};
    int32_t latitudeE7 = 0;
    int32_t longitudeE7 = 0;
    uint32_t lastSeenMs = 0;
    int8_t rssi = 0;
    uint32_t iconChecksum = 0;
    bool iconAvailable = false;
    uint8_t iconPixels[ICON_PIXELS] = {};
  };

  void setEnabled(bool enabled) { enabled_ = enabled; }
  bool enabled() const { return enabled_; }
  bool active() const { return active_; }
  bool service(bool wifiClaimed, bool locationValid, int32_t latitudeE7,
               int32_t longitudeE7, const String &nickname,
               const uint16_t *iconPixels, uint8_t iconWidth,
               uint8_t iconHeight, uint32_t iconChecksum, uint32_t now);
  void pause();
  size_t snapshot(Rider *output, size_t capacity, uint32_t now) const;

 private:
#pragma pack(push, 1)
  struct Packet {
    uint16_t magic;
    uint8_t version;
    uint8_t flags;
    uint32_t riderId;
    char callSign[4];
    uint16_t sequence;
    int32_t latitudeE7;
    int32_t longitudeE7;
  };
  struct IconPacket {
    uint16_t magic;
    uint8_t version;
    uint8_t flags;
    uint32_t riderId;
    uint32_t checksum;
    uint8_t width;
    uint8_t height;
    uint8_t pixels[ICON_PIXELS];
  };
#pragma pack(pop)
  static_assert(sizeof(Packet) == 22,
                "RiDar location packet must remain compact");
  static_assert(sizeof(IconPacket) <= 250,
                "RiDar icon packet must fit legacy ESP-NOW payloads");

  struct ReceivedPacket {
    uint16_t length;
    int8_t rssi;
    uint8_t data[sizeof(IconPacket)];
  };

  static RiderNetwork *instance_;
  static void receiveCallback(const esp_now_recv_info_t *info,
                              const uint8_t *data, int length);
  bool start();
  void stop(bool preserveWifi = false);
  void makeCallSign(const String &nickname, char output[4]) const;
  bool accept(const ReceivedPacket &received, uint32_t now);
  bool sendIcon(const uint16_t *pixels, uint8_t width, uint8_t height,
                uint32_t checksum, uint32_t now);

  QueueHandle_t receiveQueue_ = nullptr;
  Rider riders_[MAX_RIDERS] = {};
  bool enabled_ = false;
  bool active_ = false;
  uint32_t riderId_ = 0;
  uint16_t nextSequence_ = 0;
  uint32_t lastSendMs_ = 0;
  uint32_t lastIconSendMs_ = 0;
  uint32_t lastIconChecksum_ = 0;
};
