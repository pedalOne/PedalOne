namespace {
constexpr float RADAR_KMH_TO_MPH = 0.621371f;
constexpr float RADAR_METERS_TO_FEET = 3.28084f;
constexpr uint16_t RADAR_GRID_COLOR = 0xBDF7;
constexpr int RADAR_SETTINGS_CONTROL_LEFT = 48;
constexpr int RADAR_SETTINGS_CONTROL_RIGHT = 192;

float radarDisplayMaxFeet() {
  const int configuredFeet = radarRangeFeet(
      radarSettings.maximumRangeMeters);
  return float(constrain(((configuredFeet + 25) / 50) * 50, 50, 350));
}

int radarLaneHalfWidth(int y) {
  return us(14) + (y - uy(48)) * us(62) / max(1, uy(205) - uy(48));
}

int radarRangeY(float feet) {
  return uy(48) + lroundf(constrain(feet / radarDisplayMaxFeet(),
                                    0.0f, 1.0f) * us(157));
}

void drawRadarRangeArc(float feet, const char *label) {
  const int y = radarRangeY(feet);
  const int half = radarLaneHalfWidth(y);
  constexpr int segments = 18;
  int previousX = CENTER - half;
  int previousY = y;
  for (int segment = 1; segment <= segments; ++segment) {
    const float normalized = -1.0f + 2.0f * segment / segments;
    const int x = CENTER + lroundf(normalized * half);
    const int nextY = y + lroundf(us(7) * (1.0f - normalized * normalized));
    wideLine(previousX, previousY, x, nextY, max(1, us(1)), RADAR_GRID_COLOR);
    previousX = x;
    previousY = nextY;
  }
  // Keep the final label inside the cone where the round panel narrows.
  // The first five retain the clear outside-the-rail layout.
  if (feet >= radarDisplayMaxFeet())
    textCentered(label, CENTER + half - us(20), y - us(8), 2,
                 RADAR_GRID_COLOR);
  else
    textCentered(label, CENTER + half + us(20), y + us(2), 2,
                 RADAR_GRID_COLOR);
}

uint16_t radarThreatColor(float closingMph, float distanceFeet) {
  const float closingFeetPerSecond = closingMph * 1.46667f;
  const float ttc = closingFeetPerSecond > 0.2f
      ? distanceFeet / closingFeetPerSecond : 999.0f;
  if (ttc <= 5.0f || closingMph >= 20.0f) return RED;
  if (ttc <= 10.0f || closingMph >= 10.0f) return ORANGE;
  return YELLOW;
}

void drawRadarFooter() {
  if (appState == RADAR_PREVIEW) {
    textCentered("TAP LEFT: SETTINGS", CENTER, uy(232), 1, 0x8410);
    return;
  }
  drawRidePageDots();
  drawPausedBadge();
}
}

void drawRadarPage(uint32_t now) {
  beginFrame();
  drawClock(now, us(10), 2);

  if (radarClient.state() != RadarClient::STATE_CONNECTED) {
    textCentered("RADAR", CENTER, uy(74), 3, WHITE);
    textCentered(radarClient.state() == RadarClient::STATE_TIMED_OUT
                     ? "Connection timed out" : "Reconnecting...",
                 CENTER, uy(113), 2,
                 radarClient.state() == RadarClient::STATE_TIMED_OUT ? RED : YELLOW);
    textCentered("HLK-LD2451 2F42", CENTER, uy(139), 2, 0xAD55);
    drawRadarFooter();
    endFrame();
    return;
  }

  // The rider is at the narrow top of the cone; targets trail behind toward
  // the bottom. The LD2451 reports +/-15 degrees, matching the two rails.
  wideLine(CENTER - us(14), uy(48), CENTER - us(76), uy(205), us(3), WHITE);
  wideLine(CENTER + us(14), uy(48), CENTER + us(76), uy(205), us(3), WHITE);
  canvas->fillTriangle(CENTER, uy(39), CENTER - us(6), uy(49),
                       CENTER + us(6), uy(49), GREEN);
  const int displayMaxFeet = int(radarDisplayMaxFeet());
  constexpr int tickCount = 4;
  constexpr int tickRoundingFeet = 5;
  int previousTickFeet = 0;
  for (int tick = 1; tick <= tickCount; ++tick) {
    const float rawFeet = displayMaxFeet * float(tick) / tickCount;
    int feet = int(lroundf(rawFeet / tickRoundingFeet)) * tickRoundingFeet;
    // The outer mark always states the selected display maximum exactly.
    if (tick == tickCount) feet = displayMaxFeet;
    feet = constrain(feet, previousTickFeet + tickRoundingFeet,
                     displayMaxFeet);
    char label[8];
    snprintf(label, sizeof(label), "%dft", feet);
    drawRadarRangeArc(float(feet), label);
    previousTickFeet = feet;
  }

  RadarClient::Target targets[RadarClient::MAX_TARGETS] = {};
  const size_t count = radarClient.snapshot(targets, RadarClient::MAX_TARGETS, now);
  size_t visibleCount = 0;
  for (size_t index = 0; index < count; ++index) {
    if (radarSettings.direction == 0 && !targets[index].approaching) continue;
    if (radarSettings.direction == 1 && targets[index].approaching) continue;
    const float distanceFeet = targets[index].distanceMeters * RADAR_METERS_TO_FEET;
    const float closingMph = targets[index].closingSpeedKmh * RADAR_KMH_TO_MPH;
    const int y = radarRangeY(distanceFeet);
    const int half = radarLaneHalfWidth(y) - us(12);
    const int x = CENTER + lroundf(
        constrain(float(targets[index].angleDegrees) / 15.0f, -1.0f, 1.0f) * half);
    const uint16_t color = closingMph < 0.0f
        ? uint16_t(0x8410) : radarThreatColor(closingMph, distanceFeet);
    canvas->fillCircle(x, y, us(6), color);
    ++visibleCount;
  }

  if (!visibleCount)
    textCentered("CLEAR", CENTER, uy(121), 3, GREEN);

  drawRadarFooter();
  endFrame();
}

void drawRadarSettingsPage(uint32_t now) {
  beginFrame();
  drawHeader(now);
  textCentered("RADAR SETTINGS", CENTER, uy(55), 3, WHITE);

  char values[6][16];
  snprintf(values[0], sizeof(values[0]), "%d ft",
           radarRangeFeet(radarSettings.maximumRangeMeters));
  const char *direction = radarSettings.direction == 0 ? "APPROACH" :
      (radarSettings.direction == 1 ? "AWAY" : "BOTH");
  snprintf(values[1], sizeof(values[1]), "%s", direction);
  snprintf(values[2], sizeof(values[2]), "%u km/h",
           radarSettings.minimumSpeedKmh);
  snprintf(values[3], sizeof(values[3]), "%u s",
           radarSettings.noTargetDelaySeconds);
  snprintf(values[4], sizeof(values[4]), "%u", radarSettings.triggerCount);
  snprintf(values[5], sizeof(values[5]), "%u", radarSettings.snrThreshold);
  const char *labels[] = {"Range", "Direction", "Min speed",
                          "No-target", "Trigger", "SNR"};

  for (int row = 0; row < 6; ++row) {
    const int y = uy(79 + row * 19);
    if (row) canvas->drawFastHLine(ux(58), y - us(11), us(124), 0x2945);
    textCentered(labels[row], ux(89), y, 1, WHITE);
    canvas->fillRoundRect(ux(121), y - us(8), us(58), us(16), us(5), 0x1082);
    textCentered(values[row], ux(150), y, 1, GREEN);
    canvas->fillTriangle(ux(52), y, ux(60), y - us(5), ux(60), y + us(5),
                         0xAD55);
    canvas->fillTriangle(ux(188), y, ux(180), y - us(5), ux(180), y + us(5),
                         0xAD55);
  }

  const RadarClient::ConfigState config = radarClient.configState();
  uint16_t buttonColor = radarSettingsDirty ? YELLOW : GREEN;
  const char *buttonText = radarSettingsDirty ? "APPLY" : "SAVED";
  if (config == RadarClient::CONFIG_APPLYING) {
    buttonColor = YELLOW;
    buttonText = "APPLYING";
  } else if (config == RadarClient::CONFIG_FAILED) {
    buttonColor = RED;
    buttonText = "RETRY";
  }
  canvas->fillRoundRect(ux(69), uy(194), us(102), us(34), us(9), buttonColor);
  textCentered(buttonText, CENTER, uy(211), buttonText[0] == 'A' ? 2 : 3,
               BLACK);
  if (radarSettingsReturnState != RIDING && radarEnabled)
    textCentered("TAP RIGHT: LIVE TEST", CENTER, uy(232), 1, 0x8410);
  endFrame();
}
