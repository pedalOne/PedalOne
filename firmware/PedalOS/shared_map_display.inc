// Render the compact P1VT road geometry around the current GPX position.
// Tile payloads live in PSRAM after their first use so animated zoom and
// heading changes never reread thousands of individual points from FFat.
struct SharedMapScreenPoint { int x, y; };

constexpr uint8_t SHARED_MAP_CACHE_SLOTS = 9;
struct SharedMapCachedTile {
  uint32_t x = 0, y = 0, bytes = 0, lastUse = 0;
  uint8_t *data = nullptr;
};
SharedMapCachedTile sharedMapTileCache[SHARED_MAP_CACHE_SLOTS];
uint32_t sharedMapCacheFingerprint = 0;
uint32_t sharedMapCacheRevision = 0;
uint32_t sharedMapCacheUse = 0;

void clearSharedMapTileCache() {
  for (auto &tile : sharedMapTileCache) {
    if (tile.data) free(tile.data);
    tile = {};
  }
}

const uint8_t *cachedSharedMapTile(uint32_t tileX, uint32_t tileY,
                                   uint32_t &payloadBytes) {
  const uint32_t fingerprint = sharedMap.fingerprint();
  const uint32_t revision = sharedMap.revision();
  if (fingerprint != sharedMapCacheFingerprint ||
      revision != sharedMapCacheRevision) {
    clearSharedMapTileCache();
    sharedMapCacheFingerprint = fingerprint;
    sharedMapCacheRevision = revision;
  }
  ++sharedMapCacheUse;
  for (auto &tile : sharedMapTileCache) {
    if (tile.data && tile.x == tileX && tile.y == tileY) {
      tile.lastUse = sharedMapCacheUse;
      payloadBytes = tile.bytes;
      return tile.data;
    }
  }

  SharedMapCachedTile *slot = nullptr;
  for (auto &tile : sharedMapTileCache) {
    if (!tile.data) { slot = &tile; break; }
    if (!slot || tile.lastUse < slot->lastUse) slot = &tile;
  }
  if (!slot) return nullptr;

  File file;
  SharedMapTileEntry entry{};
  if (!sharedMap.openTile(tileX, tileY, file, entry) ||
      entry.payloadLength < sizeof(SharedVectorTileHeader)) return nullptr;
  uint8_t *data = static_cast<uint8_t *>(ps_malloc(entry.payloadLength));
  if (!data) data = static_cast<uint8_t *>(malloc(entry.payloadLength));
  if (!data || file.read(data, entry.payloadLength) != entry.payloadLength) {
    if (data) free(data);
    file.close();
    return nullptr;
  }
  file.close();
  if (slot->data) free(slot->data);
  slot->x = tileX;
  slot->y = tileY;
  slot->bytes = entry.payloadLength;
  slot->lastUse = sharedMapCacheUse;
  slot->data = data;
  payloadBytes = slot->bytes;
  return slot->data;
}

SharedMapScreenPoint sharedMapScreenPoint(float east, float north,
                                          float sine, float cosine,
                                          float pixelsPerMeter) {
  return {
      CENTER + lroundf((east * cosine - north * sine) * pixelsPerMeter),
      uy(185) - lroundf((east * sine + north * cosine) * pixelsPerMeter)};
}

void drawSharedMapStroke(int x1, int y1, int x2, int y2,
                         int halfWidth, uint16_t color) {
  for (int offset = -halfWidth; offset <= halfWidth; ++offset) {
    canvas->drawLine(x1 + offset, y1, x2 + offset, y2, color);
    if (offset) canvas->drawLine(x1, y1 + offset, x2, y2 + offset, color);
  }
}

bool drawSharedMapTile(uint32_t tileX, uint32_t tileY,
                       double riderX, double riderY,
                       double groundMetersPerTile,
                       float sine, float cosine, float pixelsPerMeter) {
  // Downloaded map tiles can contain far more geometry than the compact demo.
  // Yield the frame as soon as the touch controller asks for service instead
  // of making the rider wait for every remaining polyline to be rasterized.
  if (touchPending || touchActive) return false;
  uint32_t payloadBytes = 0;
  const uint8_t *data = cachedSharedMapTile(tileX, tileY, payloadBytes);
  if (!data || payloadBytes < sizeof(SharedVectorTileHeader)) return true;

  SharedVectorTileHeader header{};
  memcpy(&header, data, sizeof(header));
  if (memcmp(header.magic, "P1VT", 4) || header.version != 1) return true;
  size_t cursor = sizeof(header);

  double dxTiles = double(tileX) - riderX;
  const double worldTiles = double(uint32_t(1) << sharedMap.zoom());
  if (dxTiles > worldTiles / 2) dxTiles -= worldTiles;
  if (dxTiles < -worldTiles / 2) dxTiles += worldTiles;
  const float eastOrigin = float(dxTiles * groundMetersPerTile);
  const float northOrigin = float(-(double(tileY) - riderY) * groundMetersPerTile);
  const float metersPerExtent = float(groundMetersPerTile / 4095.0);

  // Use one high-contrast neutral stroke for every surrounding road. Road
  // hierarchy was too subtle in sunlight, while the much wider blue GPX line
  // still keeps the planned route visually dominant.
  constexpr uint16_t roadColor = 0xBDF7;
  constexpr int roadHalfWidth = 1;
  for (uint16_t lineIndex = 0; lineIndex < header.lineCount; ++lineIndex) {
    if (touchPending || touchActive) return false;
    SharedVectorLineHeader line{};
    if (cursor + sizeof(line) > payloadBytes) break;
    memcpy(&line, data + cursor, sizeof(line));
    cursor += sizeof(line);
    const size_t pointBytes = size_t(line.pointCount) * sizeof(SharedVectorPoint);
    if (line.pointCount < 2 || cursor + pointBytes > payloadBytes) break;

    SharedVectorPoint point{};
    memcpy(&point, data + cursor, sizeof(point));
    cursor += sizeof(point);
    SharedMapScreenPoint emitted = sharedMapScreenPoint(
        eastOrigin + point.x * metersPerExtent,
        northOrigin - point.y * metersPerExtent,
        sine, cosine, pixelsPerMeter);
    for (uint16_t pointIndex = 1; pointIndex < line.pointCount; ++pointIndex) {
      if ((pointIndex & 15) == 0 && (touchPending || touchActive)) return false;
      memcpy(&point, data + cursor, sizeof(point));
      cursor += sizeof(point);
      const SharedMapScreenPoint next = sharedMapScreenPoint(
          eastOrigin + point.x * metersPerExtent,
          northOrigin - point.y * metersPerExtent,
          sine, cosine, pixelsPerMeter);
      // Collapse sub-two-pixel vector steps. They are invisible on this panel
      // but were responsible for thousands of redundant line draws per frame.
      const int deltaX = next.x - emitted.x;
      const int deltaY = next.y - emitted.y;
      const bool last = pointIndex + 1 == line.pointCount;
      if (!last && deltaX * deltaX + deltaY * deltaY < 4) continue;
      const bool visible = max(emitted.x, next.x) >= -8 &&
          min(emitted.x, next.x) <= SCREEN_SIZE + 8 &&
          max(emitted.y, next.y) >= -8 &&
          min(emitted.y, next.y) <= SCREEN_SIZE + 8;
      if (visible) {
        drawSharedMapStroke(emitted.x, emitted.y, next.x, next.y,
                            roadHalfWidth, roadColor);
      }
      emitted = next;
    }
  }
  return true;
}

bool drawSharedRoadMap(const GpxPoint &position, float heading,
                       float pixelsPerMeter) {
  if (touchPending || touchActive) return false;
  if (!sharedMap.valid()) return true;
  const double latitude = gpx::lat0 +
      position.north / (gpx::EARTH * gpx::RAD);
  const double longitude = gpx::lon0 +
      position.east / (gpx::EARTH * gpx::RAD * gpx::lonScale);
  const double latitudeRadians =
      max(-85.05112878, min(85.05112878, latitude)) * PI / 180.0;
  const double tileCount = double(uint32_t(1) << sharedMap.zoom());
  const double riderX = (longitude + 180.0) / 360.0 * tileCount;
  const double riderY =
      (1.0 - log(tan(latitudeRadians) + 1.0 / cos(latitudeRadians)) / PI) *
      0.5 * tileCount;
  const int32_t centerX = floor(riderX);
  const int32_t centerY = floor(riderY);
  const double groundMetersPerTile =
      (2.0 * PI * 6378137.0 / tileCount) * cos(latitudeRadians);
  const int32_t world = int32_t(uint32_t(1) << sharedMap.zoom());
  const float sine = sinf(heading), cosine = cosf(heading);
  // The rider sits below center. A 440-pixel circle encloses every screen
  // corner and lets us reject tiles that cannot contribute a visible point.
  const double visibleRadiusMeters = 440.0 / max(0.05f, pixelsPerMeter);

  for (int dy = -1; dy <= 1; ++dy) {
    const int32_t y = centerY + dy;
    if (y < 0 || y >= world) continue;
    for (int dx = -1; dx <= 1; ++dx) {
      int32_t x = (centerX + dx) % world;
      if (x < 0) x += world;
      double tileDx = double(x) - riderX;
      if (tileDx > tileCount / 2) tileDx -= tileCount;
      if (tileDx < -tileCount / 2) tileDx += tileCount;
      const double east0 = tileDx * groundMetersPerTile;
      const double east1 = east0 + groundMetersPerTile;
      const double north0 = -(double(y) - riderY) * groundMetersPerTile;
      const double north1 = north0 - groundMetersPerTile;
      const double nearestEast = east0 > 0 ? east0 : (east1 < 0 ? east1 : 0);
      const double nearestNorth = north1 > 0 ? north1 : (north0 < 0 ? north0 : 0);
      if (nearestEast * nearestEast + nearestNorth * nearestNorth >
          visibleRadiusMeters * visibleRadiusMeters) continue;
      if (!drawSharedMapTile(uint32_t(x), uint32_t(y), riderX, riderY,
                             groundMetersPerTile, sine, cosine,
                             pixelsPerMeter)) return false;
    }
  }
  return true;
}
