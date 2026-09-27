#include "onboard_gps.h"

#include <sys/time.h>
#include <time.h>

namespace {
constexpr uint8_t COMMAND_ADDRESS = 0x50;
constexpr uint8_t READ_ADDRESS = 0x54;
constexpr uint8_t WRITE_ADDRESS = 0x58;
constexpr uint8_t IO_EXPANDER_ADDRESS = 0x20;
constexpr uint8_t IO_EXPANDER_OUTPUT_REGISTER = 0x01;
constexpr uint8_t IO_EXPANDER_CONFIG_REGISTER = 0x03;
constexpr uint8_t GPS_RESET_MASK = 0x80;  // TCA9554 P7 / EXIO7.
constexpr uint32_t COMMAND_CLOCK_HZ = 100000;
constexpr uint32_t RUNTIME_CLOCK_HZ = 400000;
constexpr uint32_t POLL_INTERVAL_MS = 200;
constexpr uint32_t FIX_STALE_MS = 3000;
constexpr uint32_t NMEA_STALE_MS = 3000;
constexpr size_t READ_CHUNK = 1024;
constexpr uint16_t STATIONARY_FIX_INTERVAL_MS = 1000;
constexpr uint16_t MOVING_FIX_INTERVAL_MS = 200;
constexpr float MOVING_SPEED_KNOTS = 1.30f;      // About 1.5 mph.
constexpr float STATIONARY_SPEED_KNOTS = 0.43f;  // About 0.5 mph.
constexpr uint32_t STATIONARY_RATE_DELAY_MS = 2UL * 60UL * 1000UL;
constexpr uint32_t RATE_CHANGE_COOLDOWN_MS = 5000;

bool readExpanderRegister(TwoWire &wire, uint8_t reg, uint8_t &value) {
  wire.beginTransmission(IO_EXPANDER_ADDRESS);
  wire.write(reg);
  if (wire.endTransmission(false) != 0) return false;
  if (wire.requestFrom(IO_EXPANDER_ADDRESS, uint8_t(1), true) != 1)
    return false;
  value = wire.read();
  return true;
}

bool writeExpanderRegister(TwoWire &wire, uint8_t reg, uint8_t value) {
  wire.beginTransmission(IO_EXPANDER_ADDRESS);
  wire.write(reg);
  wire.write(value);
  return wire.endTransmission() == 0;
}

bool pulseBackupExitReset(TwoWire &wire) {
  uint8_t output = 0;
  uint8_t config = 0;
  wire.setClock(COMMAND_CLOCK_HZ);
  if (!readExpanderRegister(wire, IO_EXPANDER_OUTPUT_REGISTER, output) ||
      !readExpanderRegister(wire, IO_EXPANDER_CONFIG_REGISTER, config)) {
    wire.setClock(RUNTIME_CLOCK_HZ);
    return false;
  }

  // Program the output latch low before changing P7 from input to output, then
  // hold LC76G RESET_N low longer than Quectel's 100 ms minimum. Releasing it
  // after VCC returns exits backup mode while preserving the V_BCKP domain.
  if (!writeExpanderRegister(wire, IO_EXPANDER_OUTPUT_REGISTER,
                             output & ~GPS_RESET_MASK) ||
      !writeExpanderRegister(wire, IO_EXPANDER_CONFIG_REGISTER,
                             config & ~GPS_RESET_MASK)) {
    wire.setClock(RUNTIME_CLOCK_HZ);
    return false;
  }
  delay(150);
  const bool released = writeExpanderRegister(
      wire, IO_EXPANDER_OUTPUT_REGISTER, output | GPS_RESET_MASK);
  delay(300);
  wire.setClock(RUNTIME_CLOCK_HZ);
  return released;
}

uint8_t nmeaChecksum(const char *body) {
  uint8_t result = 0;
  while (*body && *body != '*') result ^= uint8_t(*body++);
  return result;
}

bool validChecksum(const char *line) {
  const char *star = strchr(line, '*');
  if (!star || line[0] != '$' || strlen(star) < 3) return false;
  char expected[3] = {star[1], star[2], 0};
  return nmeaChecksum(line + 1) == strtoul(expected, nullptr, 16);
}

int splitCsv(char *text, char *fields[], int maximum) {
  if (!text || maximum < 1) return 0;
  int count = 1;
  fields[0] = text;
  for (char *p = text; *p && count < maximum; ++p) {
    if (*p == ',') {
      *p = 0;
      fields[count++] = p + 1;
    }
  }
  return count;
}

double parseCoordinate(const char *value, const char *hemisphere) {
  if (!value || !*value) return NAN;
  const double packed = atof(value);
  const int degrees = int(packed / 100.0);
  double result = degrees + (packed - degrees * 100.0) / 60.0;
  if (hemisphere && (*hemisphere == 'S' || *hemisphere == 'W')) result = -result;
  return result;
}

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = unsigned(year - era * 400);
  const unsigned dayOfYear =
      (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 -
      yearOfEra / 100 + dayOfYear;
  return int64_t(era) * 146097 + int64_t(dayOfEra) - 719468;
}
}  // namespace

bool OnboardGps::begin(TwoWire &wire, uint32_t now) {
  wire_ = &wire;
  if (pulseBackupExitReset(*wire_))
    Serial.println("GPS backup-exit reset pulse complete");
  else
    Serial.println("GPS reset expander unavailable; continuing without pulse");
  wire_->setClock(COMMAND_CLOCK_HZ);
  // The LC76G bridge acknowledges its command endpoint before its FIFO is
  // guaranteed ready. The standalone GPS firmware relies on this ACK and
  // starts configuration after a short boot delay; probing FIFO register 8
  // here could therefore reject a real receiver permanently at boot.
  wire_->beginTransmission(COMMAND_ADDRESS);
  present_ = wire_->endTransmission() == 0;
  wire_->setClock(RUNTIME_CLOCK_HZ);
  if (!present_) return false;

  acquisitionStartMs_ = now;
  configStage_ = CONFIG_WAKE;
  // Match the known-good receiver bring-up: allow its I2C bridge and NMEA
  // engine to settle before issuing the first PAIR command.
  configDeadlineMs_ = now + 2000;
  return true;
}

bool OnboardGps::commandWrite(const uint8_t *data, size_t length) {
  if (!wire_) return false;
  wire_->beginTransmission(COMMAND_ADDRESS);
  wire_->write(data, length);
  return wire_->endTransmission() == 0;
}

bool OnboardGps::readRegister(uint16_t offset, uint8_t *data, size_t length,
                              uint8_t attempts) {
  const uint8_t command[8] = {
      uint8_t(offset), uint8_t(offset >> 8), 0x51, 0xAA,
      uint8_t(length), uint8_t(length >> 8), uint8_t(length >> 16),
      uint8_t(length >> 24)};
  for (uint8_t attempt = 0; attempt < attempts; ++attempt) {
    if (!commandWrite(command, sizeof(command))) {
      delay(20);
      continue;
    }
    delay(20);
    const size_t received = wire_->requestFrom(READ_ADDRESS, length, true);
    for (size_t i = 0; i < received; ++i) data[i] = wire_->read();
    if (received == length) return true;
    delay(20);
  }
  return false;
}

bool OnboardGps::writeReceiver(const uint8_t *data, size_t length) {
  uint8_t availableBytes[4] = {};
  if (!readRegister(0x0004, availableBytes, sizeof(availableBytes))) return false;
  const uint32_t available = uint32_t(availableBytes[0]) |
      uint32_t(availableBytes[1]) << 8 | uint32_t(availableBytes[2]) << 16 |
      uint32_t(availableBytes[3]) << 24;
  if (available < length) return false;
  const uint8_t command[8] = {
      0x00, 0x10, 0x53, 0xAA, uint8_t(length), uint8_t(length >> 8),
      uint8_t(length >> 16), uint8_t(length >> 24)};
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (!commandWrite(command, sizeof(command))) {
      delay(20);
      continue;
    }
    delay(20);
    wire_->beginTransmission(WRITE_ADDRESS);
    wire_->write(data, length);
    if (wire_->endTransmission() == 0) return true;
    delay(20);
  }
  return false;
}

bool OnboardGps::sendPair(const char *sentence) {
  return writeReceiver(reinterpret_cast<const uint8_t *>(sentence),
                       strlen(sentence));
}

bool OnboardGps::sendPairBody(const char *body) {
  char sentence[64];
  const int length = snprintf(sentence, sizeof(sentence), "$%s*%02X\r\n", body,
                              nmeaChecksum(body));
  return length > 0 && size_t(length) < sizeof(sentence) && sendPair(sentence);
}

void OnboardGps::serviceConfiguration(uint32_t now) {
  if (int32_t(now - configDeadlineMs_) < 0) return;
  switch (configStage_) {
    case CONFIG_WAKE:
      // Harmless on cold boot, and wakes a receiver left in ALP by deep sleep.
      sendPairBody("PAIR732,0");
      configStage_ = CONFIG_RATE;
      configDeadlineMs_ = now + 150;
      break;
    case CONFIG_RATE:
      // Begin at 1 Hz. Once an RMC speed fix shows that the bike is moving,
      // adaptive rate control raises the receiver to 5 Hz.
      if (setFixInterval(STATIONARY_FIX_INTERVAL_MS, now)) {
        configStage_ = CONFIG_REBOOT_WAIT;
        configDeadlineMs_ = now + 3000;
      } else {
        configDeadlineMs_ = now + 1000;
      }
      break;
    case CONFIG_REBOOT_WAIT:
      outputCommand_ = 0;
      configStage_ = CONFIG_OUTPUTS;
      configDeadlineMs_ = now;
      break;
    case CONFIG_OUTPUTS: {
      // LC76G retains NMEA output settings through backup mode.  Configure
      // every sentence the application relies on instead of assuming RMC is
      // still at its factory default.  VTG supplies an independent Doppler
      // speed path if an RMC sentence is temporarily invalid or omitted.
      static const char *const commandBodies[] = {
          "PAIR062,0,1",  // GGA: position/fix quality every solution.
          "PAIR062,1,0",  // GLL: unused.
          "PAIR062,2,5",  // GSA: dilution/fix type every five solutions.
          "PAIR062,3,5",  // GSV: satellites every five solutions.
          "PAIR062,4,1",  // RMC: time, position, speed, and course.
          "PAIR062,5,1"   // VTG: redundant speed and course.
      };
      if (outputCommand_ <
          sizeof(commandBodies) / sizeof(commandBodies[0])) {
        if (sendPairBody(commandBodies[outputCommand_])) ++outputCommand_;
        configDeadlineMs_ = now + 150;
      } else {
        configStage_ = CONFIG_QUERY;
        configDeadlineMs_ = now + 150;
      }
      break;
    }
    case CONFIG_QUERY:
      rateReplySeen_ = false;
      if (sendPair("$PAIR051*3E\r\n")) {
        configStage_ = CONFIG_QUERY_WAIT;
        configDeadlineMs_ = now + 2000;
      } else {
        configDeadlineMs_ = now + 1000;
      }
      break;
    case CONFIG_QUERY_WAIT:
      if (rateReplySeen_) configStage_ = CONFIG_DONE;
      else {
        configStage_ = CONFIG_QUERY;
        configDeadlineMs_ = now + 500;
      }
      break;
    case CONFIG_DONE:
      break;
  }
}

void OnboardGps::serviceWatchdog(uint32_t now) {
  const bool neverStarted = !lastNmeaMs_ && now - acquisitionStartMs_ > 10000;
  const bool becameStale = configStage_ == CONFIG_DONE && lastNmeaMs_ &&
      now - lastNmeaMs_ > NMEA_STALE_MS;
  if ((!neverStarted && !becameStale) || now - lastRecoveryMs_ < 3000) return;
  lastRecoveryMs_ = now;
  if (++recoveryAttempts_ <= 2) {
    sendPair("$PAIR051*3E\r\n");
    return;
  }
  if (sendPair("$PAIR004*3E\r\n")) {
    acquisitionStartMs_ = now;
    configStage_ = CONFIG_REBOOT_WAIT;
    configDeadlineMs_ = now + 3000;
    outputCommand_ = 0;
    rateReplySeen_ = false;
  }
  recoveryAttempts_ = 0;
}

bool OnboardGps::setFixInterval(uint16_t intervalMs, uint32_t now) {
  char body[24];
  snprintf(body, sizeof(body), "PAIR050,%u", unsigned(intervalMs));
  if (!sendPairBody(body)) return false;
  fixIntervalMs_ = intervalMs;
  lastRateChangeMs_ = now;
  stationarySinceMs_ = 0;
  Serial.printf("GPS fix rate -> %u Hz\n",
                unsigned(1000U / (intervalMs ? intervalMs : 1U)));
  return true;
}

void OnboardGps::serviceAdaptiveRate(uint32_t now) {
  if (configStage_ != CONFIG_DONE || !isfinite(speedKnots_) ||
      now - lastSpeedMs_ > FIX_STALE_MS ||
      now - lastRateChangeMs_ < RATE_CHANGE_COOLDOWN_MS) return;

  if (fixIntervalMs_ != MOVING_FIX_INTERVAL_MS) {
    stationarySinceMs_ = 0;
    if (speedKnots_ < MOVING_SPEED_KNOTS ||
        !setFixInterval(MOVING_FIX_INTERVAL_MS, now)) return;
  } else {
    if (speedKnots_ > STATIONARY_SPEED_KNOTS) {
      stationarySinceMs_ = 0;
      return;
    }
    if (!stationarySinceMs_) stationarySinceMs_ = now;
    if (now - stationarySinceMs_ < STATIONARY_RATE_DELAY_MS ||
        !setFixInterval(STATIONARY_FIX_INTERVAL_MS, now)) return;
  }

  // PAIR050 reboots the receiver when the requested frequency changes.
  // Reapply the NMEA output configuration after its three-second restart.
  configStage_ = CONFIG_REBOOT_WAIT;
  configDeadlineMs_ = now + 3000;
  outputCommand_ = 0;
  rateReplySeen_ = false;
  hasPosition_ = false;
  lastPositionMs_ = 0;
  lastFixSentenceMs_ = 0;
}

bool OnboardGps::pollReceiver(uint32_t now) {
  uint8_t lengthBytes[4] = {};
  if (!readRegister(0x0008, lengthBytes, sizeof(lengthBytes))) return false;
  uint32_t length = uint32_t(lengthBytes[0]) | uint32_t(lengthBytes[1]) << 8 |
      uint32_t(lengthBytes[2]) << 16 | uint32_t(lengthBytes[3]) << 24;
  if (!length) return true;
  if (length > READ_CHUNK) length = READ_CHUNK;
  uint8_t data[READ_CHUNK];
  if (!readRegister(0x2000, data, length)) return false;
  for (size_t i = 0; i < length; ++i) feedNmea(char(data[i]));
  (void)now;
  return true;
}

void OnboardGps::service(uint32_t now) {
  if (!present_ || !wire_) return;
  wire_->setClock(COMMAND_CLOCK_HZ);
  serviceConfiguration(now);
  serviceWatchdog(now);
  wire_->setClock(RUNTIME_CLOCK_HZ);
  const uint32_t interval = readFailures_ >= 3 ? 500 : POLL_INTERVAL_MS;
  if (now - lastPollMs_ >= interval) {
    lastPollMs_ = now;
    if (pollReceiver(now)) readFailures_ = 0;
    else if (readFailures_ < 255) ++readFailures_;
  }
  serviceAdaptiveRate(millis());
  now=millis();
  if(now-lastDiagnosticMs_>=5000 && Serial && Serial.availableForWrite()>=192) {
    lastDiagnosticMs_=now;
    Serial.printf("GPS stage=%u NMEA=%lu ms fix=%u quality=%u sats=%u speed=%.2fkn hdop=%.1f readErrors=%u\n",
        unsigned(configStage_), lastNmeaMs_ ? (unsigned long)(now-lastNmeaMs_) : 999999UL,
        unsigned(fixFresh(now)),unsigned(fixQuality_),unsigned(satellites_),
        isfinite(speedKnots_) ? speedKnots_ : -1.0f,
        isfinite(hdop_) ? hdop_ : -1.0f,unsigned(readFailures_));
  }
}

void OnboardGps::feedNmea(char value) {
  if (value == '$') {
    nmeaLength_ = 0;
    nmeaLine_[nmeaLength_++] = value;
  } else if (value == '\r' || value == '\n') {
    if (nmeaLength_) {
      nmeaLine_[nmeaLength_] = 0;
      parseNmea(nmeaLine_, millis());
      nmeaLength_ = 0;
    }
  } else if (nmeaLength_ && nmeaLength_ < sizeof(nmeaLine_) - 1) {
    nmeaLine_[nmeaLength_++] = value;
  } else if (nmeaLength_) {
    nmeaLength_ = 0;
  }
}

void OnboardGps::parseNmea(char *line, uint32_t now) {
  if (!validChecksum(line)) return;
  char *star = strchr(line, '*');
  if (star) *star = 0;
  char *fields[24] = {};
  const int count = splitCsv(line, fields, 24);
  if (!count) return;
  if (fields[0][0] == '$' && fields[0][1] == 'G') {
    lastNmeaMs_ = now;
    recoveryAttempts_ = 0;
  }
  const size_t length = strlen(fields[0]);
  const char *type = length >= 3 ? fields[0] + length - 3 : "";
  if (!strcmp(type, "GGA")) parseGga(fields, count, now);
  else if (!strcmp(type, "RMC")) parseRmc(fields, count, now);
  else if (!strcmp(type, "VTG")) parseVtg(fields, count, now);
  else if (!strcmp(type, "GSA")) parseGsa(fields, count, now);
  else if (!strcmp(fields[0], "$PAIR051") && count >= 2) {
    const int reportedInterval = atoi(fields[1]);
    if (reportedInterval >= 100 && reportedInterval <= 1000)
      fixIntervalMs_ = uint16_t(reportedInterval);
    rateReplySeen_ = true;
  }
}

void OnboardGps::parseGga(char *fields[], int count, uint32_t now) {
  if (count < 10) return;
  lastFixSentenceMs_ = now;
  fixQuality_ = atoi(fields[6]);
  satellites_ = constrain(atoi(fields[7]),0,255);
  hdop_ = *fields[8] ? atof(fields[8]) : NAN;
  altitudeM_ = *fields[9] ? atof(fields[9]) : NAN;
  if (isfinite(altitudeM_)) lastAltitudeMs_ = now;
  if (fixQuality_ && *fields[2] && *fields[4]) {
    latitude_ = parseCoordinate(fields[2], fields[3]);
    longitude_ = parseCoordinate(fields[4], fields[5]);
    hasPosition_ = true;
    everHadFix_ = true;
    lastPositionMs_ = now;
    if (fixType_ < 2) fixType_ = 2;
    publishFix(now);
  }
}

void OnboardGps::parseRmc(char *fields[], int count, uint32_t now) {
  if (count < 10 || fields[2][0] != 'A') return;
  if (*fields[1] && *fields[9]) setClockFromGps(fields[1], fields[9]);
  latitude_ = parseCoordinate(fields[3], fields[4]);
  longitude_ = parseCoordinate(fields[5], fields[6]);
  speedKnots_ = *fields[7] ? atof(fields[7]) : NAN;
  courseDegrees_ = *fields[8] ? atof(fields[8]) : NAN;
  if (isfinite(speedKnots_)) lastSpeedMs_ = now;
  hasPosition_ = true;
  everHadFix_ = true;
  lastPositionMs_ = now;
  lastFixSentenceMs_ = now;
  publishFix(now);
}

void OnboardGps::parseVtg(char *fields[], int count, uint32_t now) {
  if (count < 8) return;
  // NMEA VTG fields 5 and 7 are speed in knots and km/h respectively. Prefer
  // knots to match RMC, with km/h as a standards-compliant fallback.
  float knots = *fields[5] ? atof(fields[5]) : NAN;
  if (!isfinite(knots) && *fields[7]) knots = atof(fields[7]) / 1.852f;
  if (!isfinite(knots) || knots < 0.0f) return;
  speedKnots_ = knots;
  lastSpeedMs_ = now;
  if (*fields[1]) courseDegrees_ = atof(fields[1]);
  if (hasPosition_ && now - lastPositionMs_ <= FIX_STALE_MS)
    publishFix(now);
}

void OnboardGps::parseGsa(char *fields[], int count, uint32_t now) {
  if (count < 3) return;
  fixType_ = constrain(atoi(fields[2]), 1, 3);
  lastFixSentenceMs_ = now;
}

void OnboardGps::publishFix(uint32_t now) {
  if (!hasPosition_ || fixType_ < 2 || !isfinite(latitude_) ||
      !isfinite(longitude_)) return;
  OnboardGpsFix fix = {};
  fix.sequence = latestFix_.sequence + 1;
  fix.timestamp = hasTime_ ? uint32_t(time(nullptr)) : 0;
  fix.latitudeE7 = int32_t(llround(latitude_ * 10000000.0));
  fix.longitudeE7 = int32_t(llround(longitude_ * 10000000.0));
  if (isfinite(altitudeM_) && now - lastAltitudeMs_ <= FIX_STALE_MS) {
    fix.altitudeDm = int16_t(constrain(int(lroundf(altitudeM_ * 10.0f)),
                                      -32768, 32767));
    fix.flags |= 1 << 1;
  }
  if (isfinite(speedKnots_) && now - lastSpeedMs_ <= FIX_STALE_MS) {
    fix.speedCms = uint16_t(constrain(int(lroundf(speedKnots_ * 51.4444f)),
                                      0, 65535));
    fix.flags |= 1;
  }
  if (isfinite(courseDegrees_) && now - lastSpeedMs_ <= FIX_STALE_MS) {
    fix.courseDeg100 = uint16_t(constrain(
        int(lroundf(courseDegrees_ * 100.0f)), 0, 35999));
    fix.flags |= 1 << 2;
  }
  if (isfinite(hdop_)) {
    fix.accuracyCm = uint16_t(constrain(int(lroundf(hdop_ * 500.0f)),
                                        0, 65535));
  }
  latestFix_ = fix;
}

void OnboardGps::setClockFromGps(const char *utc, const char *date) {
  if (!utc || strlen(utc) < 6 || !date || strlen(date) < 6) return;
  static char lastUtc[7] = {};
  static char lastDate[7] = {};
  if (!strncmp(lastUtc, utc, 6) && !strncmp(lastDate, date, 6)) return;
  const int hour = (utc[0] - '0') * 10 + utc[1] - '0';
  const int minute = (utc[2] - '0') * 10 + utc[3] - '0';
  const int second = (utc[4] - '0') * 10 + utc[5] - '0';
  const int day = (date[0] - '0') * 10 + date[1] - '0';
  const int month = (date[2] - '0') * 10 + date[3] - '0';
  const int shortYear = (date[4] - '0') * 10 + date[5] - '0';
  const int year = shortYear >= 80 ? 1900 + shortYear : 2000 + shortYear;
  const time_t epoch = time_t(daysFromCivil(year, month, day) * 86400 +
      hour * 3600 + minute * 60 + second);
  if (epoch <= 0) return;
  struct timeval timeValue = {epoch, 0};
  settimeofday(&timeValue, nullptr);
  hasTime_ = true;
  memcpy(lastUtc, utc, 6);
  lastUtc[6] = 0;
  memcpy(lastDate, date, 6);
  lastDate[6] = 0;
}

bool OnboardGps::takeFix(uint16_t &lastSequence, OnboardGpsFix &fix) const {
  if (!present_ || !latestFix_.sequence || latestFix_.sequence == lastSequence)
    return false;
  fix = latestFix_;
  lastSequence = latestFix_.sequence;
  return true;
}

bool OnboardGps::fixFresh(uint32_t now) const {
  // A caller can retain a loop timestamp from just before a GPS read. Treat
  // a slightly newer sample as age zero rather than unsigned-wrap stale.
  const auto fresh = [now](uint32_t timestamp) {
    const int32_t age=int32_t(now-timestamp);
    return age<=int32_t(FIX_STALE_MS);
  };
  return present_ && hasPosition_ && fixType_ >= 2 && lastPositionMs_ &&
      fresh(lastPositionMs_) && lastFixSentenceMs_ && fresh(lastFixSentenceMs_);
}

void OnboardGps::prepareForSleep() {
  if (!present_ || !wire_) return;
  wire_->setClock(COMMAND_CLOCK_HZ);
  const bool rateAccepted = sendPairBody("PAIR050,1000");
  if (rateAccepted) delay(3200);
  if (rateAccepted) sendPairBody("PAIR732,1");
  if (rateAccepted) fixIntervalMs_ = STATIONARY_FIX_INTERVAL_MS;
  stationarySinceMs_ = 0;
  wire_->setClock(RUNTIME_CLOCK_HZ);
}

bool OnboardGps::prepareForHardPowerOff() {
  if(!present_ || !wire_)return false;
  wire_->setClock(COMMAND_CLOCK_HZ);
  // Exit ALP before the explicit backup request; VCC will then be cut by
  // PMIC shutdown. V_BCKP retention depends on the board's RTC supply.
  bool sent=sendPairBody("PAIR732,0");
  delay(150);
  if(sent)sent=sendPairBody("PAIR650,0");
  if(sent)delay(1000); // Quectel's backup entry sequence before removing VCC.
  wire_->setClock(RUNTIME_CLOCK_HZ);
  return sent;
}

void OnboardGps::resumeAfterLightSleep(uint32_t now) {
  if (!present_ || !wire_) return;
  wire_->setClock(COMMAND_CLOCK_HZ);
  sendPairBody("PAIR732,0");
  wire_->setClock(RUNTIME_CLOCK_HZ);
  acquisitionStartMs_ = now;
  configStage_ = CONFIG_RATE;
  configDeadlineMs_ = now + 150;
  outputCommand_ = 0;
  recoveryAttempts_ = 0;
  rateReplySeen_ = false;
  fixIntervalMs_ = STATIONARY_FIX_INTERVAL_MS;
  stationarySinceMs_ = 0;
  lastRateChangeMs_ = now;
  hasPosition_ = false;
  lastPositionMs_ = 0;
  lastFixSentenceMs_ = 0;
  lastNmeaMs_ = 0;
  latestFix_ = {};
}
