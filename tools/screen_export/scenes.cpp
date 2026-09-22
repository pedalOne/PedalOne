static constexpr uint32_t NOW = 100000;

void capture(const char *name, void (*draw)(uint32_t)) {
  draw(NOW);
  save(name);
}

int main() {
  bluetoothEnabled = true;
  phoneConnected = true;
  batteryConnected = true;
  batteryCharging = false;
  batteryPercent = 82;
  hasLocation = true;
  lastLocationMs = NOW;
  latestLocation.timestamp = 1789331400;  // Stable documentation fixture.
  deviceNickname = "PedalOne";
  deviceEmoji = "\xF0\x9F\x8D\x84";
  onboardGpsPresent = true;

  capture("01-ready", drawReadyScreen);

  appState = MENU;
  menuSelection = 0;
  capture("02-start", drawMenu);
  appState = OPTIONS_MENU;
  capture("03-options", drawMenu);
  appState = RIDE_MENU;
  ridePaused = false;
  capture("04-ride-menu", drawMenu);

  capture("05-start-ride", [](uint32_t) { drawStartRouteMenu(); });
  gpxLibrary.routeCount = 1;
  routeSelectionForStart = true;
  capture("06-route-library", drawRouteLibrary);

  countdownStartMs = NOW - 1100;
  capture("07-countdown", drawCountdown);

  rideStartMs = NOW - 3723000;
  currentSpeedMph = 18.6f;
  rideMaxSpeedMph = 31.4f;
  averageSpeedMph = 16.8f;
  distanceMiles = 17.42f;
  climbFeet = 1260.0f;
  liveGradePercent = 5.7f;
  liveGradeValid = true;
  currentPage = 0;
  capture("08-ride-speed", drawRidePage);
  currentPage = 1;
  capture("09-ride-stats", drawRidePage);

  drawGpxRoute(720.0f, true, false, 0.0f);
  save("10-gpx-navigation");

  ridarEnabled = false;
  currentPage = RIDAR_PAGE_INDEX;
  capture("11-ridar", drawRidePage);

  routeNavigationEnabled = false;
  gpxFixAvailable = false;
  capture("11b-free-ride-map", drawFreeRideMapPage);

  navVisible = true;
  navReceivedMs = NOW;
  navManeuver = MANEUVER_RIGHT;
  strcpy(navTitle, "Turn right onto Carmel Mountain Road");
  strcpy(navMessage, "Carmel Mountain Road");
  strcpy(navDistance, "450 ft");
  capture("12-phone-navigation", drawNavigationPage);

  statusPage = 0;
  capture("13-status", drawStatus);
  statusPage = 1;
  odometerMiles = 1248.6;
  tripAMiles = 42.7;
  tripBMiles = 218.3;
  capture("14-odometer", drawStatus);
  capture("15-display", drawDisplayPage);
  capture("16-bluetooth", drawBluetoothPage);

  rideStorageReady = true;
  savedRideCount = 2;
  savedRides[0].startEpoch = 1789250400;
  savedRides[0].durationSeconds = 3723;
  savedRides[0].distanceMiles = 17.42f;
  savedRides[1].startEpoch = 1788645600;
  savedRides[1].durationSeconds = 2840;
  savedRides[1].distanceMiles = 12.18f;
  selectedRouteRow = 0;
  capture("16b-ride-history", drawRidesList);
  capture("16c-save-ride", drawSaveRidePrompt);
  ancsHistoryCount = 0;
  capture("16d-ancs-diagnostics", drawAncsTest);

  summaryRideSeconds = 3723;
  summaryAverageMph = averageSpeedMph;
  summaryDistanceMiles = distanceMiles;
  summaryClimbFeet = int(climbFeet);
  summaryPage = 0;
  capture("17-summary", drawSummary);
  altitudeSampleCount = 8;
  const float alt[] = {290, 315, 342, 405, 460, 438, 510, 552};
  const float spd[] = {13, 17, 21, 16, 12, 22, 19, 24};
  for (unsigned i = 0; i < 8; ++i) {
    altitudeSamples[i] = alt[i];
    speedSamples[i] = spd[i];
  }
  summaryPage = 1;
  capture("18-summary-elevation", drawSummary);
  summaryPage = 2;
  capture("19-summary-speed", drawSummary);
  summaryPage = 3;
  summaryRouteLoaded = true;
  summaryRoutePointCount = 7;
  const RouteScreenPoint route[] = {{95,270},{126,235},{167,247},{218,196},
                                     {271,223},{318,189},{366,248}};
  for (unsigned i = 0; i < 7; ++i) summaryRoutePoints[i] = route[i];
  capture("20-summary-route", drawSummary);

  pendingAction = ACTION_END_RIDE;
  capture("21-end-ride", drawConfirmation);
  pendingAction = ACTION_POWER_OFF;
  capture("21b-power-off", drawConfirmation);
  pendingAction = ACTION_DELETE_RIDE;
  capture("21c-delete-ride", drawConfirmation);
  pendingAction = ACTION_RESET_TRIP_A;
  capture("21d-reset-trip", drawConfirmation);
  pendingAction = ACTION_RESET_ODOMETER;
  odometerResetArmed = true;
  capture("22-reset-odometer", drawConfirmation);
  capture("23-ride-canceled", drawRideCanceled);
  capture("24-firmware-update", drawOtaUpdate);
  capture("25-splash", [](uint32_t) { drawPedalOneSplashFrame(2100); });
}
