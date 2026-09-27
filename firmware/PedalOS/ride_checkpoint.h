#pragma once
#include <stdint.h>
#include <stddef.h>
#include <math.h>

// One sealed snapshot per automatic sleep, never a write on each GPS update.
namespace ride_checkpoint {
constexpr uint32_t MAGIC = 0x31524350;
struct Record {
  uint32_t magic = MAGIC, version = 1;
  uint32_t elapsedMs = 0, startEpoch = 0, routeId = 0;
  uint16_t minutes = 0, gpsPoints = 0;
  uint8_t manualPause = 0, navigation = 0, elevationValid = 0, pendingSave = 0;
  float distance = 0, average = 0, maximum = 0, climb = 0;
  float elevation = 0, climbAnchor = 0, progress = -1;
  float altitudes[720] = {}, speeds[720] = {};
  uint32_t checksum = 0;
};
inline uint32_t checksum(const Record &r) {
  const auto *bytes = reinterpret_cast<const uint8_t *>(&r);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < offsetof(Record, checksum); ++i)
    hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}
inline bool valid(const Record &r) {
  return r.magic == MAGIC && r.version == 1 && r.minutes <= 720 &&
      r.gpsPoints <= 4320 && r.manualPause <= 1 && r.navigation <= 1 &&
      r.elevationValid <= 1 && r.pendingSave <= 1 && isfinite(r.distance) && r.distance >= 0 &&
      isfinite(r.average) && isfinite(r.maximum) && isfinite(r.climb) &&
      isfinite(r.elevation) && isfinite(r.climbAnchor) &&
      isfinite(r.progress) && r.checksum == checksum(r);
}
}  // namespace ride_checkpoint

void clearRideCheckpoint();
bool saveRideCheckpoint(uint32_t now);
bool restoreRideCheckpoint(uint32_t now);
