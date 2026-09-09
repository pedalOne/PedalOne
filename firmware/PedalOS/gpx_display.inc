// The GPX ride page; playback shares the same track renderer.
GpxPoint gpxLivePosition = {};
GpxPoint gpxNearestCoursePoint = {};
float gpxLastProgress=-1,gpxLiveBearing=NAN;
gpx::Guidance gpxGuidance;
gpx::TurnPreview gpxTurnPreview;
uint32_t gpxFixReceived=0;
bool gpxFixAvailable=false;
void drawRouteFilename(const char *name,int y,uint32_t now,uint8_t size=1) {
  String title(name);
  const unsigned visible=size>=3 ? 17 : (size==2 ? 25 : 27);
  if(title.length()>visible) {
    unsigned start=(now/450)%(title.length()-visible+8);
    start=min(start,unsigned(title.length()-visible));
    title=title.substring(start,start+visible);
  }
  textCentered(title.c_str(),CENTER,y,size,WHITE);
}
void drawRouteLibrary(uint32_t now) {
  beginFrame();
  textCentered("ROUTES",CENTER,uy(53),4,WHITE);
  if(!gpxLibrary.routeCount) {
    textCentered("No GPX routes",CENTER,uy(117),3,WHITE);
    textCentered("Sync from the phone app",CENTER,uy(149),1,0xAD55);
    endFrame();return;
  }
  char number[24];snprintf(number,sizeof(number),"%u / %u",unsigned(selectedRouteRow+1),max(1u,gpxLibrary.routeCount));
  textCentered(number,CENTER,uy(85),3,WHITE);
  drawRouteFilename(gpxLibrary.routes[selectedRouteRow].name,uy(119),now,2);
  // Large chevrons indicate the left and right tap areas.
  wideLine(ux(65),uy(145),ux(53),uy(157),us(3),WHITE);
  wideLine(ux(53),uy(157),ux(65),uy(169),us(3),WHITE);
  wideLine(ux(175),uy(145),ux(187),uy(157),us(3),WHITE);
  wideLine(ux(187),uy(157),ux(175),uy(169),us(3),WHITE);
  if(routeSelectionForStart) {
    for(int inset=0;inset<us(2);++inset)
      canvas->drawRoundRect(ux(66)+inset,uy(182)+inset,us(108)-2*inset,us(40)-2*inset,us(7),START_GREEN);
    textCentered("START",CENTER,uy(202),4,WHITE);
  }
  endFrame();
}
void drawStartRouteMenu() {
  beginFrame();
  textCentered("START RIDE",CENTER,uy(51),4,WHITE);
  canvas->drawRoundRect(ux(26),uy(80),us(188),us(48),us(10),GREEN);
  textCentered("Select from GPX",CENTER,uy(104),3,WHITE);
  canvas->drawRoundRect(ux(26),uy(142),us(188),us(48),us(10),WHITE);
  textCentered("Start Anyways",CENTER,uy(166),3,WHITE);
  endFrame();
}
void drawGpxDirectionArrow(float relativeBearing,uint16_t color) {
  const float sine=sinf(relativeBearing),cosine=cosf(relativeBearing);
  const float x[]={0,-43,43,-15,15,15,-15};
  const float y[]={-65,-12,-12,-12,-12,58,58};
  int px[7],py[7];
  for(int i=0;i<7;++i) {
    px[i]=CENTER+lroundf(x[i]*cosine-y[i]*sine);
    py[i]=157+lroundf(x[i]*sine+y[i]*cosine);
  }
  canvas->fillTriangle(px[0],py[0],px[1],py[1],px[2],py[2],color);
  canvas->fillTriangle(px[3],py[3],px[4],py[4],px[5],py[5],color);
  canvas->fillTriangle(px[3],py[3],px[5],py[5],px[6],py[6],color);
}
void drawGpxRoute(float meters, bool demo, bool hasFix, float offset) {
  beginFrame();
  const auto position = demo || !hasFix ? gpx::sample(meters) : gpxLivePosition;
  const float bearing = !demo && std::isfinite(gpxLiveBearing) ? gpxLiveBearing : gpx::heading(meters);
  const float sine = sinf(bearing), cosine = cosf(bearing);
  const float eastScale=cos((gpx::lat0+position.north/(gpx::EARTH*gpx::RAD))*gpx::RAD)/gpx::lonScale;
  constexpr float pixelsPerMeter = 0.65f;
  constexpr uint16_t routeBlue = 0x05BF;
  // Short distance steps keep drawing bounded and avoid screen-spanning lines.
  bool previousVisible = false;
  int previousX = 0, previousY = 0;
  for (float d = fmaxf(0, meters-350); d <= fminf(gpx::length(), meters+850); d += 4) {
    const auto p = gpx::sample(d);
    const float east=(p.east-position.east)*eastScale, north=p.north-position.north;
    const int x = CENTER+lroundf((east*cosine-north*sine)*pixelsPerMeter);
    const int y = 342-lroundf((east*sine+north*cosine)*pixelsPerMeter);
    const bool visible = x>35 && x<431 && y>270 && y<437 &&
        (x-CENTER)*(x-CENTER)+(y-CENTER)*(y-CENTER)<210*210;
    if (visible && previousVisible) {
      for (int width=-3; width<=3; ++width) {
        canvas->drawLine(previousX+width,previousY,x+width,y,routeBlue);
        canvas->drawLine(previousX,previousY+width,x,y+width,routeBlue);
      }
    }
    previousVisible=visible; previousX=x; previousY=y;
  }
  char distance[32];
  const auto mode=demo ? gpx::ON_ROUTE : gpxGuidance.mode();
  const bool guidance=mode!=gpx::ON_ROUTE;
  const uint16_t guidanceColor=mode==gpx::OFF_COURSE ? 0xF81F : WHITE;
  const auto target=mode==gpx::TO_START ? gpx::sample(0) : gpxNearestCoursePoint;
  const auto cue = !demo && gpxTurnPreview.active ? gpxTurnPreview.cue : gpx::nextCue(meters);
  const float remaining = max(0.0f,cue.meters-meters);
  if (remaining >= 160.9344f) snprintf(distance,sizeof(distance),"%.1f mi",remaining/1609.344f);
  else snprintf(distance,sizeof(distance),"%.0f ft",remaining*3.28084f);
  if(guidance)snprintf(distance,sizeof(distance),"%.2f mi",gpx::pointDistance(position,target)/1609.344f);
  const bool arrived = !guidance && meters >= gpx::length();
  textCentered(arrived ? "ARRIVED" : distance,CENTER,52,5,guidanceColor);
  // White bend arrow; GPX has no street names, so never invent one.
  if(guidance) {
    drawGpxDirectionArrow(gpx::wrapAngle(gpx::pointBearing(position,target)-bearing),guidanceColor);
  } else if (cue.angle != 0 && !arrived) {
    int side = cue.angle>0 ? 1 : -1;
    canvas->fillRoundRect(CENTER-14,136,28,80,10,WHITE);
    canvas->fillRoundRect(side>0 ? CENTER-14 : CENTER-70,123,84,28,10,WHITE);
    int tip=CENTER+side*92;
    canvas->fillTriangle(tip,137,tip-side*45,95,tip-side*45,179,WHITE);
  } else {
    drawGpxDirectionArrow(0,WHITE);
    // Mark the final destination at the tip of the straight-ahead arrow.
    canvas->fillCircle(CENTER,92,14,WHITE);
    canvas->fillCircle(CENTER,92,7,BLACK);
  }
  if(guidance) textCentered(mode==gpx::TO_START ? "To Start" : "Off Course",CENTER,247,3,guidanceColor);
  canvas->fillCircle(CENTER,342,20,WHITE);
  canvas->fillTriangle(CENTER,324,CENTER-13,356,CENTER,350,0x039F);
  canvas->fillTriangle(CENTER,324,CENTER,350,CENTER+13,356,0x039F);
  if (demo && gpxSimulation.paused) textCentered("PAUSED",CENTER,419,2,WHITE);
  if (!demo) drawRidePageDots();
  endFrame();
}

void updateGpxPosition(uint32_t now) {
  LocationPacket packet;uint32_t received;
  portENTER_CRITICAL(&locationMux);
  packet=latestLocation;received=lastLocationMs;
  gpxFixAvailable=hasLocation && timestampIsFresh(now,received,LOCATION_TIMEOUT_MS);
  portEXIT_CRITICAL(&locationMux);
  if(!gpxFixAvailable || received==gpxFixReceived)return;
  gpxFixReceived=received;
  const float east=gpx::longitudeDelta(gpx::lon0,packet.longitudeE7*1e-7)*gpx::RAD*gpx::EARTH*gpx::lonScale;
  const float north=(packet.latitudeE7*1e-7-gpx::lat0)*gpx::RAD*gpx::EARTH;
  gpxLivePosition={east,north,0};
  const float course=(packet.flags & 4) && (packet.flags & 1) && packet.speedCms>=80 && packet.courseDeg100<36000
      ? packet.courseDeg100*0.01f*gpx::RAD : NAN;
  float offset=0;
  float meters=gpx::nearestDistance(east,north,offset,gpxLastProgress,course);
  if(offset>100)meters=gpx::nearestDistance(east,north,offset,-1,course);
  gpxLastProgress=meters;
  // Recovery targets the closest projection on any segment, without the
  // progress/course preference used to match overlapping riding legs.
  float nearestOffset=0;
  gpxNearestCoursePoint=gpx::sample(gpx::nearestDistance(east,north,nearestOffset));
  if(std::isfinite(course))gpxLiveBearing=course;
  else if(!std::isfinite(gpxLiveBearing))gpxLiveBearing=0; // North-up until a GPS course is available.
  gpxGuidance.update(gpx::pointDistance(gpxLivePosition,gpx::sample(0)),offset);
}
void drawLiveGpxPage(uint32_t now) {
  if(!routeNavigationEnabled) {
    beginFrame();textCentered("No GPX selected",CENTER,uy(115),3,WHITE);
    drawRidePageDots();endFrame();return;
  }
  updateGpxPosition(now);
  if(!gpxFixAvailable) {
    beginFrame();textCentered("Waiting for GPS",CENTER,uy(105),3,WHITE);
    drawRidePageDots();endFrame();return;
  }
  drawGpxRoute(max(0.0f,gpxLastProgress),false,true,0);
}
