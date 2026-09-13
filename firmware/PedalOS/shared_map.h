#pragma once

#include <Arduino.h>
#include <BLECharacteristic.h>
#include <FFat.h>

constexpr char SHARED_MAP_BLE_UUID[] = "8E400008-F315-4F60-9FB8-838830DAEA50";
constexpr char SHARED_MAP_PATH[] = "/shared_map.p1map";
constexpr char SHARED_MAP_TEMP_PATH[] = "/shared_map.tmp";

#pragma pack(push, 1)
struct SharedMapHeader {
  char magic[4];
  uint16_t version;
  uint8_t zoom;
  uint8_t flags;
  uint32_t corridorCentimeters;
  uint32_t fingerprint;
  uint16_t routeCount;
  uint16_t tileCount;
  uint32_t routeDirectoryBytes;
  uint32_t tileDirectoryBytes;
  uint32_t payloadBytes;
};

struct SharedMapTileEntry {
  uint32_t x;
  uint32_t y;
  uint32_t payloadOffset;
  uint32_t payloadLength;
  uint32_t payloadChecksum;
};

struct SharedVectorTileHeader {
  char magic[4];
  uint16_t version;
  uint16_t lineCount;
};

struct SharedVectorLineHeader {
  uint8_t kind;
  uint8_t reserved;
  uint16_t pointCount;
};

struct SharedVectorPoint {
  uint16_t x;
  uint16_t y;
};
#pragma pack(pop)

static_assert(sizeof(SharedMapHeader) == 32, "P1MP header layout changed");
static_assert(sizeof(SharedMapTileEntry) == 20, "P1MP tile entry layout changed");
static_assert(sizeof(SharedVectorTileHeader) == 8, "P1VT header layout changed");

class SharedMapLibrary : public BLECharacteristicCallbacks {
 public:
  static constexpr size_t kCommandCapacity = 247;
  static constexpr uint8_t kFastQueueCapacity = 32;

  void begin(BLECharacteristic *characteristic, bool storageReady) {
    characteristic_ = characteristic;
    storageReady_ = storageReady;
    abortTransfer();
    refresh();
    if (valid_) {
      File mapFile = FFat.open(SHARED_MAP_PATH, FILE_READ);
      const size_t mapBytes = mapFile ? mapFile.size() : 0;
      if (mapFile) mapFile.close();
      Serial.printf("Shared map restored: %u tiles, %u bytes, fingerprint=%08lx\n",
                    unsigned(header_.tileCount), unsigned(mapBytes),
                    (unsigned long)header_.fingerprint);
    } else {
      Serial.println("Shared map not installed");
    }
    if (characteristic_) {
      characteristic_->setCallbacks(this);
      publish("idle");
    }
  }

  void onWrite(BLECharacteristic *characteristic) override {
    const String value = characteristic->getValue();
    if (!value.length() || value.length() > sizeof(command_)) {
      publish("error:Invalid map command");
      return;
    }
    const uint8_t opcode = uint8_t(value[0]);
    if (opcode >= 0x10 && opcode <= 0x13) {
      portENTER_CRITICAL(&mux_);
      if (fastCount_ >= kFastQueueCapacity) {
        fastOverflow_ = true;
      } else {
        FastCommand &slot = fastQueue_[fastTail_];
        slot.bytes = value.length();
        memcpy(slot.data, value.c_str(), slot.bytes);
        fastTail_ = (fastTail_ + 1) % kFastQueueCapacity;
        ++fastCount_;
      }
      portEXIT_CRITICAL(&mux_);
      return;
    }
    portENTER_CRITICAL(&mux_);
    if (queued_) {
      portEXIT_CRITICAL(&mux_);
      return;
    }
    memcpy(command_, value.c_str(), value.length());
    commandBytes_ = value.length();
    queued_ = true;
    portEXIT_CRITICAL(&mux_);
    publish("busy");
  }

  void loop(bool canChange) {
    uint8_t data[kCommandCapacity];
    size_t bytes = 0;
    bool fastCommand = false;
    portENTER_CRITICAL(&mux_);
    if (fastOverflow_) {
      fastOverflow_ = false;
      portEXIT_CRITICAL(&mux_);
      abortTransfer();
      publish("error:Map receive queue overflow");
      return;
    }
    if (fastCount_) {
      const FastCommand &slot = fastQueue_[fastHead_];
      bytes = slot.bytes;
      memcpy(data, slot.data, bytes);
      fastHead_ = (fastHead_ + 1) % kFastQueueCapacity;
      --fastCount_;
      fastCommand = true;
    } else if (queued_) {
      bytes = commandBytes_;
      memcpy(data, command_, bytes);
      queued_ = false;
    }
    portEXIT_CRITICAL(&mux_);
    if (!bytes) {
      if (transfer_ && millis() - lastCommandMs_ > 30000) {
        abortTransfer();
        publish("error:Map transfer timed out");
      }
      return;
    }
    lastCommandMs_ = millis();

    uint8_t opcode = data[0];
    if (opcode == 0) {
      publishInventory();
      return;
    }
    if (!canChange) {
      abortTransfer();
      publish("error:End the ride before syncing maps");
      return;
    }
    if (!storageReady_) {
      abortTransfer();
      publish("error:Map storage unavailable");
      return;
    }
    if (opcode == 4 && bytes == 1) {
      abortTransfer();
      const bool removed = !FFat.exists(SHARED_MAP_PATH) || FFat.remove(SHARED_MAP_PATH);
      refresh();
      publish(removed ? "deleted" : "error:Could not delete map");
      return;
    }
    if (opcode == 0x10) opcode = 1;
    else if (opcode == 0x11) opcode = 2;
    else if (opcode == 0x13) opcode = 3;
    else if (opcode == 0x12) {
      if (bytes == 1 && transfer_) publish(String("ready:") + received_);
      else publish("error:Unexpected map checkpoint");
      return;
    }
    if (opcode == 1) {
      abortTransfer();
      if (bytes != 13) {
        publish("error:Invalid map header");
        return;
      }
      memcpy(&expectedSize_, data + 1, 4);
      memcpy(&expectedTransferChecksum_, data + 5, 4);
      memcpy(&expectedFingerprint_, data + 9, 4);
      if (expectedSize_ < sizeof(SharedMapHeader) + 4 || expectedSize_ > 6 * 1024 * 1024UL ||
          expectedSize_ > FFat.totalBytes() - FFat.usedBytes()) {
        publish("error:Not enough map storage");
        return;
      }
      FFat.remove(SHARED_MAP_TEMP_PATH);
      transfer_ = FFat.open(SHARED_MAP_TEMP_PATH, FILE_WRITE);
      received_ = 0;
      if (!transfer_) {
        publish("error:Could not create map file");
        return;
      }
      publish("ready:0");
      return;
    }
    if (opcode == 2 && transfer_) {
      uint32_t offset = 0;
      if (bytes >= 5) memcpy(&offset, data + 1, 4);
      const size_t payloadBytes = bytes >= 5 ? bytes - 5 : 0;
      if (!payloadBytes || offset != received_ || payloadBytes > expectedSize_ - received_ ||
          transfer_.write(data + 5, payloadBytes) != payloadBytes) {
        abortTransfer();
        publish("error:Map chunk sequence");
        return;
      }
      received_ += payloadBytes;
      if (!fastCommand) publish(String("ready:") + received_);
      return;
    }
    if (opcode == 3 && bytes == 1 && transfer_) {
      transfer_.flush();
      transfer_.close();
      if (received_ != expectedSize_ || !validateFile(SHARED_MAP_TEMP_PATH,
                                                       expectedTransferChecksum_,
                                                       expectedFingerprint_)) {
        abortTransfer();
        publish("error:Incomplete map or checksum mismatch");
        return;
      }
      // Keep the old package until the replacement has been fully validated.
      // FFat rename is then the only short commit step.
      FFat.remove(SHARED_MAP_PATH);
      if (!FFat.rename(SHARED_MAP_TEMP_PATH, SHARED_MAP_PATH)) {
        abortTransfer();
        publish("error:Could not install map");
        return;
      }
      const uint32_t savedFingerprint = expectedFingerprint_;
      clearTransferState();
      refresh();
      char reply[16];
      snprintf(reply, sizeof(reply), "saved:%08lx", (unsigned long)savedFingerprint);
      publish(reply);
      return;
    }
    publish("error:Unexpected map command");
  }

  bool active() const { return bool(transfer_) || queued_ || fastCount_; }
  bool valid() const { return valid_; }
  uint8_t zoom() const { return header_.zoom; }
  uint32_t fingerprint() const { return header_.fingerprint; }
  uint32_t revision() const { return revision_; }

  bool openTile(uint32_t x, uint32_t y, File &file,
                SharedMapTileEntry &entry) const {
    if (!valid_) return false;
    file = FFat.open(SHARED_MAP_PATH, FILE_READ);
    if (!file) return false;
    const uint32_t directoryStart = sizeof(SharedMapHeader) + header_.routeDirectoryBytes;
    int32_t low = 0, high = int32_t(header_.tileCount) - 1;
    while (low <= high) {
      const int32_t middle = low + (high - low) / 2;
      if (!file.seek(directoryStart + middle * sizeof(SharedMapTileEntry)) ||
          file.read(reinterpret_cast<uint8_t *>(&entry), sizeof(entry)) != sizeof(entry)) {
        file.close();
        return false;
      }
      if (entry.x == x && entry.y == y) {
        const uint32_t payloadStart = sizeof(SharedMapHeader) +
            header_.routeDirectoryBytes + header_.tileDirectoryBytes;
        if (!file.seek(payloadStart + entry.payloadOffset)) {
          file.close();
          return false;
        }
        return true;
      }
      if (entry.x < x || (entry.x == x && entry.y < y)) low = middle + 1;
      else high = middle - 1;
    }
    file.close();
    return false;
  }

  void refresh() {
    valid_ = validateFile(SHARED_MAP_PATH, 0, 0, &header_);
    if (!valid_) header_ = {};
    ++revision_;
  }

 private:
  BLECharacteristic *characteristic_ = nullptr;
  bool storageReady_ = false;
  bool valid_ = false;
  SharedMapHeader header_{};
  File transfer_;
  uint32_t expectedSize_ = 0;
  uint32_t expectedTransferChecksum_ = 0;
  uint32_t expectedFingerprint_ = 0;
  uint32_t received_ = 0;
  uint32_t revision_ = 0;
  uint32_t lastCommandMs_ = 0;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  bool queued_ = false;
  size_t commandBytes_ = 0;
  uint8_t command_[kCommandCapacity]{};
  struct FastCommand {
    uint16_t bytes = 0;
    uint8_t data[kCommandCapacity]{};
  };
  FastCommand fastQueue_[kFastQueueCapacity]{};
  volatile uint8_t fastHead_ = 0;
  volatile uint8_t fastTail_ = 0;
  volatile uint8_t fastCount_ = 0;
  volatile bool fastOverflow_ = false;

  static uint32_t updateFNV(uint32_t hash, const uint8_t *bytes, size_t count) {
    for (size_t i = 0; i < count; ++i) hash = (hash ^ bytes[i]) * 16777619UL;
    return hash;
  }

  static bool validateFile(const char *path, uint32_t transferChecksum,
                           uint32_t expectedFingerprint,
                           SharedMapHeader *outputHeader = nullptr) {
    File file = FFat.open(path, FILE_READ);
    if (!file || file.size() < sizeof(SharedMapHeader) + 4) {
      if (file) file.close();
      return false;
    }
    const size_t size = file.size();
    SharedMapHeader header{};
    if (file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) != sizeof(header) ||
        memcmp(header.magic, "P1MP", 4) || header.version != 1 || header.zoom > 22 ||
        !header.tileCount || header.tileDirectoryBytes !=
            uint32_t(header.tileCount) * sizeof(SharedMapTileEntry) ||
        size != sizeof(header) + header.routeDirectoryBytes +
            header.tileDirectoryBytes + header.payloadBytes + 4 ||
        (expectedFingerprint && header.fingerprint != expectedFingerprint)) {
      file.close();
      return false;
    }

    file.seek(0);
    uint8_t buffer[256];
    uint32_t packageHash = 2166136261UL;
    uint32_t transferHash = 2166136261UL;
    size_t remaining = size - 4;
    while (remaining) {
      const size_t wanted = min(sizeof(buffer), remaining);
      const size_t count = file.read(buffer, wanted);
      if (count != wanted) { file.close(); return false; }
      packageHash = updateFNV(packageHash, buffer, count);
      transferHash = updateFNV(transferHash, buffer, count);
      remaining -= count;
    }
    uint32_t storedPackageHash = 0;
    if (file.read(reinterpret_cast<uint8_t *>(&storedPackageHash), 4) != 4) {
      file.close();
      return false;
    }
    transferHash = updateFNV(transferHash,
                             reinterpret_cast<uint8_t *>(&storedPackageHash), 4);
    file.close();
    if (storedPackageHash != packageHash ||
        (transferChecksum && transferHash != transferChecksum)) return false;
    if (outputHeader) *outputHeader = header;
    return true;
  }

  void publishInventory() {
    if (!valid_) {
      publish("map:none");
      return;
    }
    File file = FFat.open(SHARED_MAP_PATH, FILE_READ);
    const size_t size = file ? file.size() : 0;
    uint32_t packageChecksum = 0;
    if (file && size >= 4) {
      file.seek(size - 4);
      file.read(reinterpret_cast<uint8_t *>(&packageChecksum), 4);
    }
    if (file) file.close();
    char reply[48];
    snprintf(reply, sizeof(reply), "map:%08lx:%lu:%08lx",
             (unsigned long)header_.fingerprint, (unsigned long)size,
             (unsigned long)packageChecksum);
    publish(reply);
  }

  void publish(const String &status) {
    if (characteristic_) {
      characteristic_->setValue(status);
      characteristic_->notify();
    }
    if (status.startsWith("saved:") || status.startsWith("error:"))
      Serial.println(String("MAP ") + status);
  }

  void clearTransferState() {
    expectedSize_ = expectedTransferChecksum_ = expectedFingerprint_ = received_ = 0;
  }

  void abortTransfer() {
    if (transfer_) transfer_.close();
    FFat.remove(SHARED_MAP_TEMP_PATH);
    portENTER_CRITICAL(&mux_);
    fastHead_ = fastTail_ = fastCount_ = 0;
    fastOverflow_ = false;
    portEXIT_CRITICAL(&mux_);
    clearTransferState();
  }
};
