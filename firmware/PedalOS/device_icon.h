#pragma once

#include <Arduino.h>
#include <BLECharacteristic.h>
#include <FFat.h>
#include "gpx_navigation.h"

constexpr char DEVICE_ICON_BLE_UUID[] =
    "8E400009-F315-4F60-9FB8-838830DAEA50";

#pragma pack(push, 1)
struct DeviceIconHeader {
  uint32_t magic = 0x314E4349u;  // "ICN1"
  uint8_t version = 1;
  uint8_t width = 0;
  uint8_t height = 0;
  uint8_t format = 1;            // Little-endian RGB565, black background.
  uint32_t pixelBytes = 0;
  uint32_t pixelChecksum = 0;
  uint32_t emojiChecksum = 0;
};
#pragma pack(pop)

static_assert(sizeof(DeviceIconHeader) == 20,
              "Device icon file layout changed");

class DeviceIconStore : public BLECharacteristicCallbacks {
 public:
  static constexpr uint8_t WIDTH = 44;
  static constexpr uint8_t HEIGHT = 44;
  static constexpr uint16_t PIXEL_BYTES = WIDTH * HEIGHT * 2;

  void begin(BLECharacteristic *characteristic, bool storageReady) {
    characteristic_ = characteristic;
    storageReady_ = storageReady;
    if (!characteristic_) return;
    characteristic_->setCallbacks(this);
    load();
    status("idle");
  }

  bool availableFor(const String &emoji) const {
    if (!ready_ || !emoji.length()) return false;
    return emojiChecksum_ == gpx::checksum(
        reinterpret_cast<const uint8_t *>(emoji.c_str()), emoji.length());
  }

  uint16_t *pixels() { return pixels_; }
  uint8_t width() const { return WIDTH; }
  uint8_t height() const { return HEIGHT; }
  uint32_t pixelChecksum() const { return ready_ ? pixelChecksum_ : 0; }

  void onWrite(BLECharacteristic *characteristic) override {
    const String value = characteristic->getValue();
    const uint8_t *data =
        reinterpret_cast<const uint8_t *>(value.c_str());
    const size_t bytes = value.length();
    if (!bytes) {
      status("error:Empty icon command");
      return;
    }
    if (data[0] == 4 && bytes == 1) {
      abortTransfer();
      if (storageReady_) {
        FFat.remove(FINAL_PATH);
        FFat.remove(TEMP_PATH);
      }
      ready_ = false;
      emojiChecksum_ = 0;
      pixelChecksum_ = 0;
      status("cleared");
      return;
    }
    if (!storageReady_) {
      status("error:Icon storage unavailable");
      return;
    }
    if (data[0] == 1) {
      beginTransfer(data, bytes);
      return;
    }
    if (data[0] == 2) {
      acceptChunk(data, bytes);
      return;
    }
    if (data[0] == 3 && bytes == 1) {
      finishTransfer();
      return;
    }
    status("error:Unexpected icon command");
  }

 private:
  static constexpr char FINAL_PATH[] = "/device_icon.rgb";
  static constexpr char TEMP_PATH[] = "/device_icon.tmp";
  static constexpr char BACKUP_PATH[] = "/device_icon.bak";

  void beginTransfer(const uint8_t *data, size_t bytes) {
    abortTransfer();
    if (bytes != 14 || data[1] != WIDTH || data[2] != HEIGHT ||
        data[3] != 1) {
      status("error:Invalid icon header");
      return;
    }
    uint16_t pixelBytes = 0;
    memcpy(&pixelBytes, data + 4, sizeof(pixelBytes));
    memcpy(&expectedPixelChecksum_, data + 6,
           sizeof(expectedPixelChecksum_));
    memcpy(&expectedEmojiChecksum_, data + 10,
           sizeof(expectedEmojiChecksum_));
    if (pixelBytes != PIXEL_BYTES || !expectedPixelChecksum_ ||
        !expectedEmojiChecksum_) {
      status("error:Invalid icon size or checksum");
      return;
    }
    FFat.remove(TEMP_PATH);
    transfer_ = FFat.open(TEMP_PATH, FILE_WRITE);
    if (!transfer_) {
      status("error:Could not create icon");
      return;
    }
    DeviceIconHeader header;
    header.width = WIDTH;
    header.height = HEIGHT;
    header.pixelBytes = PIXEL_BYTES;
    header.pixelChecksum = expectedPixelChecksum_;
    header.emojiChecksum = expectedEmojiChecksum_;
    if (transfer_.write(reinterpret_cast<const uint8_t *>(&header),
                        sizeof(header)) != sizeof(header)) {
      abortTransfer();
      status("error:Could not write icon header");
      return;
    }
    received_ = 0;
    runningChecksum_ = 2166136261u;
    status("ready:0");
  }

  void acceptChunk(const uint8_t *data, size_t bytes) {
    if (!transfer_ || bytes <= 3) {
      status("error:Icon transfer not ready");
      return;
    }
    uint16_t offset = 0;
    memcpy(&offset, data + 1, sizeof(offset));
    const size_t payload = bytes - 3;
    if (offset != received_ || received_ + payload > PIXEL_BYTES) {
      abortTransfer();
      status("error:Icon chunk sequence");
      return;
    }
    if (transfer_.write(data + 3, payload) != payload) {
      abortTransfer();
      status("error:Could not write icon pixels");
      return;
    }
    for (size_t index = 0; index < payload; ++index)
      runningChecksum_ =
          (runningChecksum_ ^ data[index + 3]) * 16777619u;
    received_ += payload;
    status(String("ready:") + received_);
  }

  void finishTransfer() {
    if (!transfer_ || received_ != PIXEL_BYTES ||
        runningChecksum_ != expectedPixelChecksum_) {
      abortTransfer();
      status("error:Incomplete icon or checksum mismatch");
      return;
    }
    transfer_.flush();
    transfer_.close();
    FFat.remove(BACKUP_PATH);
    const bool hadOld = FFat.exists(FINAL_PATH);
    bool installed = !hadOld || FFat.rename(FINAL_PATH, BACKUP_PATH);
    if (installed) installed = FFat.rename(TEMP_PATH, FINAL_PATH);
    if (installed && load()) {
      FFat.remove(BACKUP_PATH);
      status(String("saved:") + String(expectedPixelChecksum_, HEX));
    } else {
      FFat.remove(FINAL_PATH);
      if (hadOld) FFat.rename(BACKUP_PATH, FINAL_PATH);
      load();
      status("error:Could not install icon");
    }
    resetTransferState();
  }

  bool load() {
    if (!storageReady_) return false;
    File file = FFat.open(FINAL_PATH, FILE_READ);
    DeviceIconHeader header;
    if (!file ||
        file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) !=
            sizeof(header) ||
        header.magic != 0x314E4349u || header.version != 1 ||
        header.width != WIDTH || header.height != HEIGHT ||
        header.format != 1 || header.pixelBytes != PIXEL_BYTES ||
        file.size() != sizeof(header) + PIXEL_BYTES ||
        file.read(reinterpret_cast<uint8_t *>(pixels_), PIXEL_BYTES) !=
            PIXEL_BYTES ||
        gpx::checksum(reinterpret_cast<const uint8_t *>(pixels_),
                      PIXEL_BYTES) != header.pixelChecksum) {
      if (file) file.close();
      ready_ = false;
      pixelChecksum_ = 0;
      return false;
    }
    file.close();
    emojiChecksum_ = header.emojiChecksum;
    pixelChecksum_ = header.pixelChecksum;
    ready_ = true;
    Serial.printf("Device icon restored: %ux%u emoji=%08lx\n", WIDTH,
                  HEIGHT, static_cast<unsigned long>(emojiChecksum_));
    return true;
  }

  void abortTransfer() {
    if (transfer_) transfer_.close();
    if (storageReady_) FFat.remove(TEMP_PATH);
    resetTransferState();
  }

  void resetTransferState() {
    received_ = 0;
    runningChecksum_ = 0;
    expectedPixelChecksum_ = 0;
    expectedEmojiChecksum_ = 0;
  }

  void status(const String &message) {
    if (!characteristic_) return;
    characteristic_->setValue(message);
    characteristic_->notify();
    if (Serial && Serial.availableForWrite() >= 96 &&
        (message.startsWith("saved:") || message.startsWith("error:") ||
         message == "cleared"))
      Serial.println(String("Device icon ") + message);
  }

  BLECharacteristic *characteristic_ = nullptr;
  bool storageReady_ = false;
  volatile bool ready_ = false;
  uint32_t emojiChecksum_ = 0;
  uint32_t pixelChecksum_ = 0;
  File transfer_;
  uint16_t received_ = 0;
  uint32_t runningChecksum_ = 0;
  uint32_t expectedPixelChecksum_ = 0;
  uint32_t expectedEmojiChecksum_ = 0;
  uint16_t pixels_[WIDTH * HEIGHT] = {};
};
