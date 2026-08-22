#include "ota_update.h"

#include <BLE2902.h>
#include <Update.h>
#include <esp_system.h>

namespace {
constexpr size_t PROGRESS_INTERVAL_BYTES = 64 * 1024;
constexpr uint32_t REBOOT_DELAY_MS = 1200;
}

class OtaUpdate::ControlCallbacks : public BLECharacteristicCallbacks {
 public:
  explicit ControlCallbacks(OtaUpdate *owner) : owner_(owner) {}
  void onWrite(BLECharacteristic *characteristic) override {
    String command = characteristic->getValue();
    command.trim();
    owner_->handleControl(command);
  }
 private:
  OtaUpdate *owner_;
};

class OtaUpdate::DataCallbacks : public BLECharacteristicCallbacks {
 public:
  explicit DataCallbacks(OtaUpdate *owner) : owner_(owner) {}
  void onWrite(BLECharacteristic *characteristic) override {
    String value = characteristic->getValue();
    owner_->handleData(reinterpret_cast<const uint8_t *>(value.c_str()),
                       value.length());
  }
 private:
  OtaUpdate *owner_;
};

void OtaUpdate::begin(BLEServer *server, const char *firmwareVersion,
                      StartAllowedCallback startAllowed) {
  firmwareVersion_ = firmwareVersion;
  startAllowed_ = startAllowed;
  BLEService *service = server->createService(SERVICE_UUID);
  BLECharacteristic *control = service->createCharacteristic(
      CONTROL_UUID, BLECharacteristic::PROPERTY_WRITE);
  BLECharacteristic *data = service->createCharacteristic(
      DATA_UUID, BLECharacteristic::PROPERTY_WRITE);
  statusCharacteristic_ = service->createCharacteristic(
      STATUS_UUID, BLECharacteristic::PROPERTY_READ |
                       BLECharacteristic::PROPERTY_NOTIFY);
  statusCharacteristic_->addDescriptor(new BLE2902());
  control->setCallbacks(new ControlCallbacks(this));
  data->setCallbacks(new DataCallbacks(this));
  publish("IDLE " + firmwareVersion_, false);
  service->start();
}

void OtaUpdate::loop(bool connected) {
  if (active_ && connected_ && !connected) abortUpdate("ERROR DISCONNECTED");
  connected_ = connected;
  if (rebootAtMs_ && int32_t(millis() - rebootAtMs_) >= 0) {
    delay(40);
    ESP.restart();
  }
}

uint8_t OtaUpdate::progressPercent() const {
  if (!expectedSize_) return 0;
  const size_t percent = (bytesWritten_ * 100) / expectedSize_;
  return percent > 100 ? 100 : static_cast<uint8_t>(percent);
}

bool OtaUpdate::validMd5(const String &md5) const {
  if (md5.length() != 32) return false;
  for (size_t i = 0; i < md5.length(); ++i) {
    const char c = md5[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

void OtaUpdate::handleControl(const String &command) {
  if (command == "INFO") {
    publish(String(active_ ? "BUSY " : "IDLE ") + firmwareVersion_ + " " +
            String(bytesWritten_) + "/" + String(expectedSize_));
    return;
  }
  if (command == "ABORT") return abortUpdate("ABORTED");
  if (command == "END") {
    if (!active_) return publish("ERROR NOT_STARTED");
    if (bytesWritten_ != expectedSize_) return fail("SIZE_MISMATCH");
    if (!Update.end(false) || !Update.isFinished()) return fail("VERIFY_FAILED");
    active_ = false;
    publish("OK REBOOTING");
    rebootAtMs_ = millis() + REBOOT_DELAY_MS;
    return;
  }
  if (!command.startsWith("BEGIN ")) return publish("ERROR BAD_COMMAND");
  if (active_ || rebootAtMs_) return publish("ERROR BUSY");
  if (startAllowed_ && !startAllowed_()) return publish("ERROR NOT_SAFE");

  const int separator = command.indexOf(' ', 6);
  if (separator < 0) return publish("ERROR BAD_BEGIN");
  const String sizeText = command.substring(6, separator);
  String md5 = command.substring(separator + 1);
  md5.toLowerCase();
  char *end = nullptr;
  const unsigned long parsedSize = strtoul(sizeText.c_str(), &end, 10);
  if (!parsedSize || *end != '\0' || !validMd5(md5))
    return publish("ERROR BAD_BEGIN");

  expectedSize_ = parsedSize;
  bytesWritten_ = 0;
  nextProgressNotification_ = PROGRESS_INTERVAL_BYTES;
  if (!Update.begin(expectedSize_, U_FLASH)) return fail("NO_OTA_SPACE");
  if (!Update.setMD5(md5.c_str())) {
    Update.abort();
    return fail("BAD_MD5");
  }
  active_ = true;
  publish("READY 0");
}

void OtaUpdate::handleData(const uint8_t *data, size_t length) {
  if (!active_ || !length) return;
  if (bytesWritten_ > expectedSize_ || length > expectedSize_ - bytesWritten_)
    return fail("TOO_MUCH_DATA");
  const size_t written = Update.write(const_cast<uint8_t *>(data), length);
  if (written != length) return fail("FLASH_WRITE");
  bytesWritten_ += written;
  if (bytesWritten_ >= nextProgressNotification_ || bytesWritten_ == expectedSize_) {
    publish("PROGRESS " + String(bytesWritten_));
    nextProgressNotification_ = bytesWritten_ + PROGRESS_INTERVAL_BYTES;
  }
}

void OtaUpdate::publish(const String &status, bool notify) {
  if (!statusCharacteristic_) return;
  statusCharacteristic_->setValue(status);
  if (notify && connected_) statusCharacteristic_->notify();
}

void OtaUpdate::fail(const char *reason) {
  if (Update.isRunning()) Update.abort();
  active_ = false;
  publish(String("ERROR ") + reason);
}

void OtaUpdate::abortUpdate(const char *status) {
  if (Update.isRunning()) Update.abort();
  active_ = false;
  expectedSize_ = 0;
  bytesWritten_ = 0;
  publish(status);
}
