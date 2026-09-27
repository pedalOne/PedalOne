// Full-screen, firmware-native map prototype for PQ loop.
constexpr float PQ_DEMO_SPEED_MPS = 8.9408f;  // 20 mph
constexpr int PQ_MAP_PAGE_INDEX = 2;
float pqMapDisplayedScale = 0.32f;
bool pqMapZoomAnimating = false;

struct PqScreenPoint { int x, y; };

PqRoutePoint pqRouteSample(float meters) {
  meters = constrain(meters, 0.0f, float(PQ_ROUTE_LENGTH_METERS));
  uint16_t low = 0, high = PQ_ROUTE_POINT_COUNT - 1;
  while (low + 1 < high) {
    const uint16_t middle = (low + high) / 2;
    if (PQ_ROUTE_POINTS[middle].meters <= meters) low = middle;
    else high = middle;
  }
  const PqRoutePoint &a = PQ_ROUTE_POINTS[low];
  const PqRoutePoint &b = PQ_ROUTE_POINTS[high];
  const float span = max(1, int(b.meters) - int(a.meters));
  const float amount = constrain((meters - a.meters) / span, 0.0f, 1.0f);
  return {int16_t(lroundf(a.east + (b.east - a.east) * amount)),
          int16_t(lroundf(a.north + (b.north - a.north) * amount)),
          uint16_t(lroundf(meters))};
}

float pqRouteHeading(float meters) {
  const PqRoutePoint behind = pqRouteSample(max(0.0f, meters - 8.0f));
  const PqRoutePoint ahead = pqRouteSample(min(float(PQ_ROUTE_LENGTH_METERS), meters + 18.0f));
  return atan2f(ahead.east - behind.east, ahead.north - behind.north);
}

struct PqTurnCue { float meters, angle; };

float pqTurnAngle(float at) {
  const float incoming = pqRouteHeading(at - 28.0f);
  const float outgoing = pqRouteHeading(at + 28.0f);
  return gpx::wrapAngle(outgoing - incoming);
}

PqTurnCue pqFindTurn(float from) {
  for (float at = max(from, 30.0f); at < PQ_ROUTE_LENGTH_METERS - 30.0f; at += 10.0f) {
    const float firstAngle = pqTurnAngle(at);
    if (fabsf(firstAngle) < 38.0f * gpx::RAD) continue;
    // Consolidate a bend into one waypoint and classify it at its strongest
    // point. This avoids treating samples along the same corner as new turns.
    PqTurnCue strongest = {at, firstAngle};
    for (float candidate = at + 10.0f;
         candidate <= min(at + 55.0f, float(PQ_ROUTE_LENGTH_METERS - 30));
         candidate += 10.0f) {
      const float angle = pqTurnAngle(candidate);
      if (fabsf(angle) > fabsf(strongest.angle)) strongest = {candidate, angle};
    }
    return strongest;
  }
  return {float(PQ_ROUTE_LENGTH_METERS), 0.0f};
}

PqTurnCue pqStableTurn(float progress) {
  static PqTurnCue locked = {0.0f, 0.0f};
  static float handledThrough = 0.0f;
  static float previousProgress = -1.0f;
  static bool active = false;
  if (previousProgress >= 0.0f && progress + 100.0f < previousProgress) {
    active = false;
    handledThrough = 0.0f;
  }
  previousProgress = progress;
  if (active && progress <= locked.meters + 25.0f) return locked;
  if (active) {
    // Ignore the remainder of the same bend after its selected waypoint.
    handledThrough = max(handledThrough, locked.meters + 85.0f);
    active = false;
  }
  locked = pqFindTurn(max(progress + 30.0f, handledThrough));
  active = true;
  return locked;
}

PqScreenPoint pqMapScreenPoint(float east, float north,
                               const PqRoutePoint &position,
                               float heading, float scale) {
  const float dx = east - position.east;
  const float dy = north - position.north;
  const float sine = sinf(heading), cosine = cosf(heading);
  return {CENTER + lroundf((dx * cosine - dy * sine) * scale),
          uy(185) - lroundf((dx * sine + dy * cosine) * scale)};
}

void drawPqStroke(int x1, int y1, int x2, int y2,
                  int halfWidth, uint16_t color) {
  for (int offset = -halfWidth; offset <= halfWidth; ++offset) {
    canvas->drawLine(x1 + offset, y1, x2 + offset, y2, color);
    if (offset) canvas->drawLine(x1, y1 + offset, x2, y2 + offset, color);
  }
}

void drawPqMapPolyline(const PqMapPoint *points, uint16_t count,
                       const PqRoutePoint &position, float heading,
                       float scale, uint16_t color, int width) {
  if (count < 2) return;
  PqScreenPoint previous = pqMapScreenPoint(points[0].east, points[0].north,
                                             position, heading, scale);
  for (uint16_t index = 1; index < count; ++index) {
    const PqScreenPoint next = pqMapScreenPoint(points[index].east,
                                                points[index].north,
                                                position, heading, scale);
    const bool visible = max(previous.x, next.x) >= -8 &&
                         min(previous.x, next.x) <= SCREEN_SIZE + 8 &&
                         max(previous.y, next.y) >= -8 &&
                         min(previous.y, next.y) <= SCREEN_SIZE + 8;
    if (visible) drawPqStroke(previous.x, previous.y, next.x, next.y,
                             width, color);
    previous = next;
  }
}

void drawPqOriginalTurnArrow(float angle, bool destination) {
  if (destination) {
    drawGpxDirectionArrow(0.0f, WHITE);
    canvas->fillCircle(CENTER, 92, 14, GREEN);
    canvas->fillCircle(CENTER, 92, 7, 0x0841);
    return;
  }
  const int side = angle > 0.0f ? 1 : -1;
  if (gpx::slightTurn(angle)) {
    drawGpxDirectionArrow(side * 45.0f * gpx::RAD, WHITE);
    return;
  }
  canvas->fillRoundRect(CENTER - 14, 136, 28, 80, 10, WHITE);
  canvas->fillRoundRect(side > 0 ? CENTER - 14 : CENTER - 70,
                        123, 84, 28, 10, WHITE);
  const int tip = CENTER + side * 92;
  canvas->fillTriangle(tip, 137, tip - side * 45, 95,
                       tip - side * 45, 179, WHITE);
}

float pqTargetMapScale(float remainingMeters) {
  constexpr float farScale = 0.32f;
  constexpr float middleScale = 0.65f;
  constexpr float nearScale = 1.35f;
  constexpr float halfMile = 804.672f;
  constexpr float fifthMile = 321.8688f;
  if (remainingMeters > halfMile) return farScale;
  if (remainingMeters > fifthMile) return middleScale;
  return nearScale;
}

float pqSmoothMapScale(float target, uint32_t now) {
  static uint32_t previousMs = 0;
  const float seconds = previousMs ? min(uint32_t(250), now - previousMs) * 0.001f : 0.0f;
  previousMs = now;
  // A full far-to-near transition takes about three seconds in either direction.
  const float maximumStep = 0.23f * seconds;
  pqMapDisplayedScale += constrain(target - pqMapDisplayedScale,
                                   -maximumStep, maximumStep);
  pqMapZoomAnimating = fabsf(target - pqMapDisplayedScale) > 0.002f;
  return pqMapDisplayedScale;
}

uint32_t pqMapFrameIntervalMs() {
  if (ridePaused) return 1000;
  if (pqMapZoomAnimating) return 83;
  // Keep map displacement below about one pixel per rendered frame. Far views
  // therefore need fewer frames than close views at the same road speed.
  const float pixelsPerSecond = PQ_DEMO_SPEED_MPS * pqMapDisplayedScale;
  const int framesPerSecond = constrain(int(ceilf(pixelsPerSecond / 0.8f)), 4, 12);
  return 1000 / framesPerSecond;
}

void drawPqMapScaleAndCompass(float mapScale, float heading) {
  int scaleFeet;
  if (mapScale < 0.48f) scaleFeet = 1000;
  else if (mapScale < 0.82f) scaleFeet = 500;
  else scaleFeet = 100;
  const int pixels = lroundf(scaleFeet * 0.3048f * mapScale);
  const int scaleX = 90, scaleY = 400;
  canvas->drawLine(scaleX, scaleY, scaleX + pixels, scaleY, WHITE);
  canvas->drawLine(scaleX, scaleY - us(3), scaleX, scaleY + us(3), WHITE);
  canvas->drawLine(scaleX + pixels, scaleY - us(3),
                   scaleX + pixels, scaleY + us(3), WHITE);
  char label[16];
  snprintf(label, sizeof(label), "%d ft", scaleFeet);
  textCentered(label, scaleX + pixels / 2, 378, 2, WHITE);

  const int northX = 345, northY = 395;
  const float dx = -sinf(heading), dy = -cosf(heading);
  const float px = -dy, py = dx;
  const int tipX = northX + lroundf(dx * us(12));
  const int tipY = northY + lroundf(dy * us(12));
  const int tailX = northX - lroundf(dx * us(7));
  const int tailY = northY - lroundf(dy * us(7));
  canvas->fillTriangle(tipX, tipY,
      tailX + lroundf(px * us(4)), tailY + lroundf(py * us(4)),
      tailX - lroundf(px * us(4)), tailY - lroundf(py * us(4)), WHITE);
  canvas->fillCircle(northX, northY, us(2), 0x0841);
  textCentered("N", northX, 365, 2, WHITE);
}

void darkenPqMapForehead() {
  // Darken only the map beneath the navigation overlay. The fade reaches full
  // brightness at the screen midpoint, leaving the lower riding view intact.
  uint16_t *pixels = reinterpret_cast<uint16_t *>(canvas->getFramebuffer());
  constexpr int fadeBottom = SCREEN_SIZE / 2;
  constexpr int topBrightness = 51;  // 20% at the top edge.
  // Canvas rotation 1 stores logical (x, y) at raw (row=x,
  // column=SCREEN_SIZE-1-y). Work in logical screen coordinates here so the
  // gradient runs from the physical forehead downward, rather than sideways.
  for (int y = 0; y < fadeBottom; ++y) {
    const int brightness = topBrightness +
        (255 - topBrightness) * y / (fadeBottom - 1);
    const int framebufferColumn = SCREEN_SIZE - 1 - y;
    for (int x = 0; x < SCREEN_SIZE; ++x) {
      uint16_t &pixel = pixels[x * SCREEN_SIZE + framebufferColumn];
      const uint16_t color = pixel;
      const uint16_t red = ((color >> 11) & 0x1f) * brightness / 255;
      const uint16_t green = ((color >> 5) & 0x3f) * brightness / 255;
      const uint16_t blue = (color & 0x1f) * brightness / 255;
      pixel = uint16_t((red << 11) | (green << 5) | blue);
    }
  }
}

void drawPqMapDemoPage(uint32_t now) {
  const float elapsedSeconds = activeRideElapsedMs(now) * 0.001f;
  const float progress = fmodf(elapsedSeconds * PQ_DEMO_SPEED_MPS,
                               float(PQ_ROUTE_LENGTH_METERS));
  const PqRoutePoint position = pqRouteSample(progress);
  const float heading = pqRouteHeading(progress);
  const PqTurnCue cue = pqStableTurn(progress);
  const float remaining = max(0.0f, cue.meters - progress);
  const bool destination = cue.angle == 0.0f;
  const float mapScale = pqSmoothMapScale(pqTargetMapScale(remaining), now);

  beginFrame();
  canvas->fillScreen(0x0841);

  // Keep the prototype neutral as well, even though Free Ride no longer uses
  // this simulated page.
  constexpr uint16_t colors[] = {0x8410, 0x5AEB, 0x738E};
  constexpr uint8_t widths[] = {1, 0, 1};
  for (uint8_t pass = 0; pass < 3; ++pass) {
    for (uint16_t index = 0; index < PQ_MAP_LINE_COUNT; ++index) {
      const PqMapLine &line = PQ_MAP_LINES[index];
      if (line.kind != pass) continue;
      drawPqMapPolyline(PQ_MAP_POINTS + line.first, line.count, position,
                        heading, mapScale, colors[pass], widths[pass]);
    }
  }

  PqScreenPoint previous = pqMapScreenPoint(PQ_ROUTE_POINTS[0].east,
                                             PQ_ROUTE_POINTS[0].north,
                                             position, heading, mapScale);
  for (uint16_t index = 1; index < PQ_ROUTE_POINT_COUNT; ++index) {
    const PqScreenPoint next = pqMapScreenPoint(PQ_ROUTE_POINTS[index].east,
                                                PQ_ROUTE_POINTS[index].north,
                                                position, heading, mapScale);
    const bool visible = max(previous.x, next.x) >= -8 &&
                         min(previous.x, next.x) <= SCREEN_SIZE + 8 &&
                         max(previous.y, next.y) >= -8 &&
                         min(previous.y, next.y) <= SCREEN_SIZE + 8;
    if (visible) drawPqStroke(previous.x, previous.y, next.x, next.y, 5, 0x05BF);
    previous = next;
  }

  const int riderY = uy(185);
  canvas->fillCircle(CENTER, riderY, us(13), WHITE);
  canvas->fillTriangle(CENTER, riderY - us(11), CENTER - us(8), riderY + us(8),
                       CENTER, riderY + us(4), 0x039F);
  canvas->fillTriangle(CENTER, riderY - us(11), CENTER, riderY + us(4),
                       CENTER + us(8), riderY + us(8), 0x039F);

  darkenPqMapForehead();

  char distance[20];
  if (remaining >= 160.9344f)
    snprintf(distance, sizeof(distance), "%.1f mi", remaining / 1609.344f);
  else
    snprintf(distance, sizeof(distance), "%.0f ft", remaining * 3.28084f);
  textCentered(distance, CENTER, 52, 5, WHITE);
  drawPqOriginalTurnArrow(cue.angle, destination);
  drawPqMapScaleAndCompass(mapScale, heading);
  drawRidePageDots();
  drawPausedBadge();
  endAnimatedFrame();
}
