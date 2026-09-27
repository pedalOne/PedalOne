constexpr char RIDE_CHECKPOINT_PATH[] = "/ride.resume";
constexpr char RIDE_CHECKPOINT_TEMP[] = "/ride.resume.tmp";

void clearRideCheckpoint() {
  if (!rideStorageReady) return;
  FFat.remove(RIDE_CHECKPOINT_PATH);
  FFat.remove(RIDE_CHECKPOINT_TEMP);
}

bool saveRideCheckpoint(uint32_t now) {
  if (!rideStorageReady || !activeGpsFile) return false;
  activeGpsFile.flush();
  if (activeGpsFile.size() != size_t(gpsSampleCount) * sizeof(GpsTrackPoint))
    return false;
  auto *r = new(std::nothrow) ride_checkpoint::Record();
  if (!r) return false;
  r->pendingSave = appState == SAVE_RIDE_PROMPT;
  r->elapsedMs = r->pendingSave ? summaryRideSeconds * 1000 : activeRideElapsedMs(now);
  r->startEpoch = rideStartEpoch;
  r->routeId = gpxLibrary.selectedID;
  r->minutes = altitudeSampleCount; r->gpsPoints = gpsSampleCount;
  r->manualPause = ridePaused && !rideAutoPaused;
  r->navigation = routeNavigationEnabled;
  r->elevationValid = altitudeFusionInitialized;
  r->distance = distanceMiles; r->average = averageSpeedMph;
  r->maximum = rideMaxSpeedMph; r->climb = climbFeet;
  r->elevation = filteredElevationFt; r->climbAnchor = climbAnchorFt;
  r->progress = gpxLastProgress;
  memcpy(r->altitudes, altitudeSamples, sizeof(altitudeSamples));
  memcpy(r->speeds, speedSamples, sizeof(speedSamples));
  r->checksum = ride_checkpoint::checksum(*r);
  File out = FFat.open(RIDE_CHECKPOINT_TEMP, FILE_WRITE);
  bool ok = ride_checkpoint::valid(*r) && out &&
      out.write(reinterpret_cast<const uint8_t *>(r), sizeof(*r)) == sizeof(*r);
  if (out) { out.flush(); out.close(); }
  // Read back before permitting power loss. The old checkpoint is consumed
  // on wake; a temporary or incomplete write never becomes a resumable ride.
  File verify = FFat.open(RIDE_CHECKPOINT_TEMP, FILE_READ);
  ok = ok && verify && verify.size() == sizeof(*r) &&
      verify.read(reinterpret_cast<uint8_t *>(r), sizeof(*r)) == sizeof(*r) &&
      ride_checkpoint::valid(*r);
  if (verify) verify.close();
  if (ok) {
    FFat.remove(RIDE_CHECKPOINT_PATH);
    ok = FFat.rename(RIDE_CHECKPOINT_TEMP, RIDE_CHECKPOINT_PATH);
  }
  delete r;
  Serial.printf("Ride resume checkpoint: %s\n", ok ? "saved" : "FAILED");
  return ok;
}

bool restoreRideCheckpoint(uint32_t now) {
  if (!rideStorageReady || !FFat.exists(RIDE_CHECKPOINT_PATH)) return false;
  auto *r = new(std::nothrow) ride_checkpoint::Record();
  if (!r) return false;
  File in = FFat.open(RIDE_CHECKPOINT_PATH, FILE_READ);
  bool ok = in && in.size() == sizeof(*r) &&
      in.read(reinterpret_cast<uint8_t *>(r), sizeof(*r)) == sizeof(*r) &&
      ride_checkpoint::valid(*r);
  if (in) in.close();
  File track = FFat.open(ACTIVE_GPS_PATH, FILE_READ);
  ok = ok && track && track.size() == size_t(r->gpsPoints) * sizeof(GpsTrackPoint);
  if (track) track.close();
  if (!ok) {
    Serial.println("Ride resume rejected: invalid checkpoint or GPS track");
    delete r; return false;
  }
  // Append only: calling startRide here would erase the retained GPS track.
  activeGpsFile = FFat.open(ACTIVE_GPS_PATH, FILE_APPEND);
  if (!activeGpsFile) { delete r; return false; }
  routeNavigationEnabled = r->navigation &&
      (r->routeId == 0 ? gpxLibrary.selectedID == 0 : gpxLibrary.select(r->routeId));
  gpxLibrary.changed = false;
  gpxGuidance.reset(); gpxTurnPreview.reset(); gpxPageAlert.reset();
  gpxLastProgress = r->progress;
  distanceMiles = r->distance; averageSpeedMph = r->average;
  rideMaxSpeedMph = r->maximum; climbFeet = r->climb;
  filteredElevationFt = r->elevation; climbAnchorFt = r->climbAnchor;
  altitudeFusionInitialized = r->elevationValid;
  rideStartEpoch = r->startEpoch;
  altitudeSampleCount = r->minutes; gpsSampleCount = r->gpsPoints;
  memcpy(altitudeSamples, r->altitudes, sizeof(altitudeSamples));
  memcpy(speedSamples, r->speeds, sizeof(speedSamples));
  rideStartMs = now - r->elapsedMs;
  // Start paused until fresh moving fixes arrive. A manual pause stays manual.
  ridePaused = true; rideAutoPaused = !r->manualPause; ridePausedAtMs = now;
  ridePreviousMs = minuteAccumulatorMs = nextGpsSampleMs = now;
  nextAltitudeSampleMs = now + 60000;
  resetGpsSpeedFilter(now);
  appState = r->pendingSave ? SAVE_RIDE_PROMPT : RIDING;
  if (r->pendingSave) {
    summaryRideSeconds = r->elapsedMs / 1000;
    summaryDistanceMiles = distanceMiles; summaryAverageMph = averageSpeedMph;
    summaryClimbFeet = int(roundf(climbFeet));
  }
  currentPage = 0; previousDrawMs = 0;
  lastActivityMs = zeroSpeedSinceMs = now;
  Serial.printf("Ride resumed: %.2f mi, %lu seconds, %u GPS points\n",
                distanceMiles, (unsigned long)(r->elapsedMs / 1000), gpsSampleCount);
  delete r;
  clearRideCheckpoint();
  notifyPhone("riding");
  return true;
}
