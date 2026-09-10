#include "wifi_config.h"
#include <BLE2902.h>
#include <Preferences.h>
#include <WiFi.h>

class WifiConfig::Callbacks : public BLECharacteristicCallbacks {
 public:
  explicit Callbacks(WifiConfig *owner) : owner_(owner) {}
  void onWrite(BLECharacteristic *characteristic) override {
    String value = characteristic->getValue();
    characteristic->setValue(""); // Never retain credentials in the GATT value.
    Command command{};
    if (value.length() > sizeof(command.bytes)) command.size = 0;
    else {
      command.size = value.length();
      memcpy(command.bytes, value.c_str(), command.size);
    }
    if (owner_->queue_) xQueueSend(owner_->queue_, &command, 0);
    memset(&command, 0, sizeof(command));
    for (size_t i = 0; i < value.length(); ++i) value.setCharAt(i, 0);
  }
 private:
  WifiConfig *owner_;
};

void WifiConfig::begin(BLEServer *server) {
  if (!queue_) queue_ = xQueueCreate(4, sizeof(Command));
  Preferences storage;
  Credentials saved{};
  if (storage.begin("wifi_cfg", true)) {
    if (storage.getBytes("network", &saved, sizeof(saved)) == sizeof(saved)) {
      saved.ssid[32] = 0;
      savedSSID_ = saved.ssid;
    }
    storage.end();
  }
  memset(&saved, 0, sizeof(saved));
  auto *service = server->createService(SERVICE_UUID);
  auto *control = service->createCharacteristic(CONTROL_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_ENC);
#if defined(CONFIG_BLUEDROID_ENABLED)
  control->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED);
#endif
  control->setCallbacks(new Callbacks(this));
  status_ = service->createCharacteristic(STATUS_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  status_->addDescriptor(new BLE2902());
  publish(state_);
  service->start();
}

void WifiConfig::publish(uint8_t state) {
  state_ = state;
  if (!status_) return;
  // Binary: state, SSID byte length, SSID UTF-8, IPv4 UTF-8. Never a password.
  const String name = connecting_ ? String(pending_.ssid) : savedSSID_;
  String address = online_ ? WiFi.localIP().toString() : String();
  uint8_t packet[64]{};
  packet[0] = state;
  packet[1] = min(size_t(32), name.length());
  memcpy(packet + 2, name.c_str(), packet[1]);
  memcpy(packet + 2 + packet[1], address.c_str(), address.length());
  status_->setValue(packet, 2 + packet[1] + address.length());
  if (notify_) status_->notify();
}

void WifiConfig::stop(bool report) {
  if (connecting_ || online_) {
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
  }
  connecting_ = online_ = save_ = false;
  memset(&pending_, 0, sizeof(pending_));
  if (report) publish(0);
}

void WifiConfig::connect(const Credentials &credentials, bool save) {
  stop(false);
  pending_ = credentials;
  save_ = save;
  started_ = millis();
  connecting_ = true;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(true);
  WiFi.begin(pending_.ssid, pending_.password);
  publish(1);
}

void WifiConfig::loop(bool allowed, bool bluetoothConnected) {
  notify_ = bluetoothConnected;
  if (!allowed && (online_ || connecting_)) stop();
  Command command{};
  if (queue_ && xQueueReceive(queue_, &command, 0) == pdTRUE) {
    const uint8_t op = command.size ? command.bytes[0] : 0;
    if (op == 2 && command.size == 1) stop();
    else if (op == 3 && command.size == 1) {
      stop(false);
      Preferences storage;
      if (storage.begin("wifi_cfg", false)) {
        const bool removed = !storage.isKey("network") || storage.remove("network");
        storage.end();
        if (removed) { savedSSID_ = ""; publish(0); }
        else publish(6);
      } else publish(6);
    } else if (!allowed) publish(5);
    else if (op == 4 && command.size == 1) {
      Preferences storage;
      Credentials saved{};
      bool valid = false;
      if (storage.begin("wifi_cfg", true)) {
        valid = storage.getBytes("network", &saved, sizeof(saved)) == sizeof(saved);
        storage.end();
      }
      saved.ssid[32] = saved.password[64] = 0;
      if (valid && saved.ssid[0]) connect(saved, false);
      else publish(4);
      memset(&saved, 0, sizeof(saved));
    } else if (op == 1 && command.size >= 3) {
      const size_t length = command.bytes[1];
      const size_t passwordLength = command.size - 2 - min(length, size_t(command.size - 2));
      bool valid = length > 0 && length <= 32 && length + 2 <= command.size &&
          (passwordLength == 0 || (passwordLength >= 8 && passwordLength <= 63));
      for (size_t i = 2; i < command.size; ++i) if (!command.bytes[i]) valid = false;
      if (valid) {
        Credentials credentials{};
        memcpy(credentials.ssid, command.bytes + 2, length);
        memcpy(credentials.password, command.bytes + 2 + length, passwordLength);
        connect(credentials, true);
        memset(&credentials, 0, sizeof(credentials));
      } else publish(4);
    } else publish(4);
    memset(&command, 0, sizeof(command));
  }
  if (connecting_ && WiFi.status() == WL_CONNECTED) {
    bool saved = true;
    if (save_) {
      Preferences storage;
      saved = storage.begin("wifi_cfg", false);
      if (saved) {
        saved = storage.putBytes("network", &pending_, sizeof(pending_)) == sizeof(pending_);
        storage.end();
      }
    }
    if (!saved) { stop(false); publish(6); return; }
    savedSSID_ = pending_.ssid;
    memset(&pending_, 0, sizeof(pending_));
    connecting_ = save_ = false;
    online_ = true;
    publish(2);
  } else if (connecting_ && uint32_t(millis() - started_) >= 30000) {
    stop(false); publish(3);
  } else if (online_ && WiFi.status() != WL_CONNECTED) {
    stop(false); publish(3);
  }
  // Connection is an explicit maintenance session, never a boot-time radio.
  if (online_ && uint32_t(millis() - started_) >= 10UL * 60 * 1000) stop();
}
