// The GPX ride page; playback shares the same track renderer.
GpxPoint gpxLivePosition = {};
GpxPoint gpxNearestCoursePoint = {};
float gpxLastProgress=-1,gpxRecoveryProgress=0,gpxLiveBearing=NAN;
float gpxOffCourseStartRideMeters=-1,gpxOffCourseStartProgress=0;
gpx::Guidance gpxGuidance;
gpx::TurnPreview gpxTurnPreview;
gpx::GuidancePageAlert gpxPageAlert;
bool gpxArrived=false;
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
void drawGpxMapReference(float mapBearing) {
  // 500 ft at the route map's 0.65 px/m scale.
  constexpr int scaleX=96,scaleY=405,scalePixels=99;
  canvas->drawLine(scaleX,scaleY,scaleX+scalePixels,scaleY,WHITE);
  canvas->drawLine(scaleX,scaleY-6,scaleX,scaleY+6,WHITE);
  canvas->drawLine(scaleX+scalePixels,scaleY-6,scaleX+scalePixels,scaleY+6,WHITE);
  textCentered("500 ft",scaleX+scalePixels/2,424,2,WHITE);

  // North rotates against the rider-up map and remains on its right side.
  constexpr int northX=330,northY=400;
  const float dx=-sinf(mapBearing),dy=-cosf(mapBearing);
  const float px=-dy,py=dx;
  const int tipX=northX+lroundf(dx*23),tipY=northY+lroundf(dy*23);
  const int tailX=northX-lroundf(dx*14),tailY=northY-lroundf(dy*14);
  canvas->fillTriangle(tipX,tipY,
      tailX+lroundf(px*7),tailY+lroundf(py*7),
      tailX-lroundf(px*7),tailY-lroundf(py*7),WHITE);
  canvas->fillCircle(northX,northY,3,BLACK);
  textCentered("N",northX,428,2,WHITE);
}
void drawGpxRoute(float meters, bool demo, bool hasFix, float offset) {
  beginFrame();
  const auto position = demo || !hasFix ? gpx::sample(meters) : gpxLivePosition;
  const float bearing = !demo && std::isfinite(gpxLiveBearing) ? gpxLiveBearing : gpx::heading(meters);
  const float sine = sinf(bearing), cosine = cosf(bearing);
  const float eastScale=cos((gpx::lat0+position.north/(gpx::EARTH*gpx::RAD))*gpx::RAD)/gpx::lonScale;
  constexpr float pixelsPerMeter = 0.65f;
  constexpr uint16_t routeBlue = 0x05BF;
  const auto mode=(demo || gpxArrived) ? gpx::ON_ROUTE : gpxGuidance.mode();
  const float routeDrawMeters=mode==gpx::OFF_COURSE ? gpxRecoveryProgress : meters;
  // Short distance steps keep drawing bounded and avoid screen-spanning lines.
  bool previousVisible = false;
  int previousX = 0, previousY = 0;
  for (float d = fmaxf(0, routeDrawMeters-350); d <= fminf(gpx::length(), routeDrawMeters+850); d += 4) {
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
  const bool guidance=mode!=gpx::ON_ROUTE;
  const uint16_t guidanceColor=mode==gpx::OFF_COURSE ? 0xF81F : WHITE;
  const auto target=mode==gpx::TO_START ? gpx::sample(0) : gpxNearestCoursePoint;
  const auto cue = !demo && gpxTurnPreview.active ? gpxTurnPreview.cue : gpx::nextCue(meters);
  const float remaining = max(0.0f,cue.meters-meters);
  if (remaining >= 160.9344f) snprintf(distance,sizeof(distance),"%.1f mi",remaining/1609.344f);
  else snprintf(distance,sizeof(distance),"%.0f ft",remaining*3.28084f);
  if(guidance) {
    const float recoveryMeters=gpx::pointDistance(position,target);
    if(recoveryMeters>=160.9344f)snprintf(distance,sizeof(distance),"%.1f mi",recoveryMeters/1609.344f);
    else snprintf(distance,sizeof(distance),"%.0f ft",recoveryMeters*3.28084f);
  }
  const bool arrived = demo ? meters>=gpx::length() : gpxArrived;
  textCentered(arrived ? "ARRIVED" : distance,CENTER,52,5,arrived ? GREEN : guidanceColor);
  // White bend arrow; GPX has no street names, so never invent one.
  if(guidance) {
    drawGpxDirectionArrow(gpx::wrapAngle(gpx::pointBearing(position,target)-bearing),guidanceColor);
  } else if (cue.angle != 0 && !arrived) {
    int side = cue.angle>0 ? 1 : -1;
    if(gpx::slightTurn(cue.angle)) {
      drawGpxDirectionArrow(side*45.0f*gpx::RAD,WHITE);
    } else {
    canvas->fillRoundRect(CENTER-14,136,28,80,10,WHITE);
    canvas->fillRoundRect(side>0 ? CENTER-14 : CENTER-70,123,84,28,10,WHITE);
    int tip=CENTER+side*92;
    canvas->fillTriangle(tip,137,tip-side*45,95,tip-side*45,179,WHITE);
    }
  } else {
    drawGpxDirectionArrow(0,WHITE);
    // Mark the final destination at the tip of the straight-ahead arrow.
    canvas->fillCircle(CENTER,92,14,WHITE);
    canvas->fillCircle(CENTER,92,7,BLACK);
  }
  if(guidance) textCentered(mode==gpx::TO_START ? "To Start" : "Off Course",CENTER,247,3,guidanceColor);
  if(mode==gpx::OFF_COURSE) {
    // Show the recovery tangent once the ordered rejoin point is inside the
    // local map. The large direction arrow remains visible at any distance.
    const float targetEast=(target.east-position.east)*eastScale;
    const float targetNorth=target.north-position.north;
    const int targetX=CENTER+lroundf((targetEast*cosine-targetNorth*sine)*pixelsPerMeter);
    const int targetY=342-lroundf((targetEast*sine+targetNorth*cosine)*pixelsPerMeter);
    const bool targetVisible=targetX>45 && targetX<421 && targetY>270 && targetY<432 &&
        (targetX-CENTER)*(targetX-CENTER)+(targetY-CENTER)*(targetY-CENTER)<205*205;
    if(targetVisible) {
      wideLine(CENTER,342,targetX,targetY,2,guidanceColor);
      canvas->fillCircle(targetX,targetY,8,WHITE);
      canvas->fillCircle(targetX,targetY,4,routeBlue);
    }
  }
  canvas->fillCircle(CENTER,342,20,WHITE);
  canvas->fillTriangle(CENTER,324,CENTER-13,356,CENTER,350,0x039F);
  canvas->fillTriangle(CENTER,324,CENTER,350,CENTER+13,356,0x039F);
  drawGpxMapReference(bearing);
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
  // Before joining, only the beginning of the ordered route is eligible. Once
  // riding, allow modest look-ahead but never jump to a geometrically-near
  // return leg later in the GPX sequence.
  const float previous=max(0.0f,gpxLastProgress);
  const float minimum=gpxGuidance.joined ? max(0.0f,previous-35.0f) : 0.0f;
  float forwardWindow=300.0f;
  if(gpxGuidance.joined && gpxGuidance.offCourse && gpxOffCourseStartRideMeters>=0) {
    const float riddenSinceExit=max(0.0f,distanceMiles*1609.344f-gpxOffCourseStartRideMeters);
    // A valid later rejoin cannot advance farther through the ordered GPX
    // than the rider has actually travelled since leaving it. This expands
    // over long detours without admitting a skipped out-and-back return leg.
    forwardWindow=max(300.0f,gpxOffCourseStartProgress-previous+riddenSinceExit+300.0f);
  }
  const float maximum=gpxGuidance.joined ? min(gpx::length(),previous+forwardWindow)
                                         : min(gpx::length(),250.0f);
  const auto match=gpx::nearestMatch(east,north,minimum,maximum,course);
  if(!match.valid)return;
  gpxRecoveryProgress=match.meters;
  gpxNearestCoursePoint=gpx::sample(match.meters);
  const bool wrongWay=std::isfinite(course) && std::isfinite(match.tangent) &&
      fabsf(gpx::wrapAngle(match.tangent-course))>1.74533f; // More than 100 degrees.
  const float distanceToStart=gpx::pointDistance(gpxLivePosition,gpx::sample(0));
  const bool wasJoined=gpxGuidance.joined;
  const bool wasOffCourse=gpxGuidance.offCourse;
  gpxGuidance.update(distanceToStart,match.offset,wrongWay);
  if(!wasJoined && gpxGuidance.joined)gpxLastProgress=0;
  if(!wasOffCourse && gpxGuidance.offCourse) {
    gpxOffCourseStartRideMeters=distanceMiles*1609.344f;
    gpxOffCourseStartProgress=previous;
  } else if(wasOffCourse && !gpxGuidance.offCourse) {
    gpxOffCourseStartRideMeters=-1;
  }
  // Freeze ordered progress while off course or riding the route backwards.
  // Rejoining within the forward window advances directly to that point.
  if(gpxGuidance.joined && match.offset<=50 && !wrongWay)
    gpxLastProgress=max(max(0.0f,gpxLastProgress),match.meters);
  else if(gpxLastProgress<0)
    gpxLastProgress=0;
  if(gpxGuidance.mode()==gpx::ON_ROUTE && gpxLastProgress>=gpx::length()-20 &&
     gpx::pointDistance(gpxLivePosition,gpx::sample(gpx::length()))<=20)gpxArrived=true;
  if(std::isfinite(course)) {
    if(std::isfinite(gpxLiveBearing))
      gpxLiveBearing=gpx::wrapAngle(gpxLiveBearing+0.25f*gpx::wrapAngle(course-gpxLiveBearing));
    else gpxLiveBearing=course;
  }
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
