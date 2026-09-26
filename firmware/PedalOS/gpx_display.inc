// The GPX ride page; playback shares the same track renderer.
GpxPoint gpxLivePosition = {};
GpxPoint gpxNearestCoursePoint = {};
float gpxLastProgress=-1,gpxRecoveryProgress=0,gpxLiveBearing=NAN;
float gpxOffCourseStartRideMeters=-1,gpxOffCourseStartProgress=0;
uint32_t gpxOffCourseSinceMs=0;
gpx::Guidance gpxGuidance;
gpx::TurnPreview gpxTurnPreview;
gpx::GuidancePageAlert gpxPageAlert;
bool gpxArrived=false;
uint32_t gpxFixReceived=0;
bool gpxFixAvailable=false;
float gpxMapDisplayedScale=0.32f;
bool gpxMapZoomAnimating=false;
uint32_t gpxMapPreviousFrameMs=0;
uint8_t freeRideMapZoomLevel=1;
float freeRideMapDisplayedScale=0.65f;
bool freeRideMapZoomAnimating=false;
uint32_t freeRideMapPreviousFrameMs=0;
float ridarMapDisplayedScale=1.35f;
bool ridarMapZoomAnimating=false;
uint32_t ridarMapPreviousFrameMs=0;
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
bool drawRouteThumbnail(uint32_t id,int left,int top,int width,int height) {
  static uint32_t cachedID=0,cachedRevision=UINT32_MAX;
  static bool available=false;
  static uint8_t format=0;
  static uint16_t palette[16];
  static uint8_t pixels[30720];
  if(cachedID!=id || cachedRevision!=gpxLibrary.thumbnailRevision) {
    cachedID=id;cachedRevision=gpxLibrary.thumbnailRevision;available=false;
    File file;GpxThumbnailHeader header{};
    if(gpxLibrary.openThumbnail(id,file,header)) {
      format=header.version;
      if(format==1) {
        available=file.read(reinterpret_cast<uint8_t*>(palette),sizeof(palette))==sizeof(palette) &&
                  file.read(pixels,7680)==7680;
        if(available) {
          // Existing 4-bit previews carry the former colored map palette. Recolor at
          // display time so stored routes do not need to be retransferred.
          palette[2]=0x4A69; palette[3]=0x738E; palette[6]=0x632C;
          palette[13]=0x05BF; palette[14]=WHITE;
        }
      } else if(format==2) {
        available=file.read(pixels,30720)==30720;
      }
    }
    if(file)file.close();
  }
  canvas->fillRect(left,top,width,height,0x0841);
  if(!available) {
    textCentered("PREVIEW WILL SYNC",left+width/2,top+height/2,1,0xAD55);
    canvas->drawRect(left,top,width,height,0x3186);return false;
  }
  // Rotation 1 stores logical (x,y) at raw (row=x,column=SCREEN_SIZE-1-y).
  // Scaling the indexed image directly into the framebuffer makes carousel
  // changes immediate without allocating a second RGB image.
  uint16_t *frame=reinterpret_cast<uint16_t*>(canvas->getFramebuffer());
  for(int y=0;y<height;++y) {
    const int sourceY=y*96/height;
    for(int x=0;x<width;++x) {
      const int sourceX=x*160/width;
      uint16_t color;
      if(format==2)color=reinterpret_cast<uint16_t*>(pixels)[sourceY*160+sourceX];
      else {
        const uint8_t byte=pixels[sourceY*80+sourceX/2];
        const uint8_t index=sourceX&1 ? byte&0x0f : byte>>4;
        color=palette[index];
      }
      frame[(left+x)*SCREEN_SIZE+(SCREEN_SIZE-1-(top+y))]=color;
    }
  }
  canvas->drawRect(left,top,width,height,0x5AEB);return true;
}
void drawRouteLibrary(uint32_t now) {
  beginFrame();
  textCentered("ROUTES",CENTER,uy(20),2,WHITE);
  if(!gpxLibrary.routeCount) {
    textCentered("No GPX routes",CENTER,uy(117),3,WHITE);
    textCentered("Sync from the phone app",CENTER,uy(149),1,0xAD55);
    endFrame();return;
  }
  char number[24];snprintf(number,sizeof(number),"%u / %u",unsigned(selectedRouteRow+1),max(1u,gpxLibrary.routeCount));
  drawRouteFilename(gpxLibrary.routes[selectedRouteRow].name,uy(43),now,2);
  textCentered(number,CENTER,uy(61),1,0xAD55);
  drawRouteThumbnail(gpxLibrary.routes[selectedRouteRow].id,ux(48),uy(70),us(144),us(96));
  // Large chevrons are both visual controls and the left/right tap areas.
  wideLine(ux(31),uy(106),ux(19),uy(118),us(3),WHITE);
  wideLine(ux(19),uy(118),ux(31),uy(130),us(3),WHITE);
  wideLine(ux(209),uy(106),ux(221),uy(118),us(3),WHITE);
  wideLine(ux(221),uy(118),ux(209),uy(130),us(3),WHITE);
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
  textCentered("Free Ride",CENTER,uy(166),3,WHITE);
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
float gpxTargetMapScale(float remainingMeters) {
  constexpr float halfMile=804.672f,fifthMile=321.8688f;
  if(remainingMeters>halfMile)return 0.32f;
  if(remainingMeters>fifthMile)return 0.65f;
  return 1.35f;
}
float gpxSmoothMapScale(float target,uint32_t now) {
  const float seconds=gpxMapPreviousFrameMs
      ? min(uint32_t(250),now-gpxMapPreviousFrameMs)*0.001f : 0.0f;
  gpxMapPreviousFrameMs=now;
  // Finish a full far-to-near transition in roughly two seconds while keeping
  // enough intermediate frames to avoid a distracting zoom jump.
  const float maximumStep=0.50f*seconds;
  gpxMapDisplayedScale+=constrain(target-gpxMapDisplayedScale,-maximumStep,maximumStep);
  gpxMapZoomAnimating=fabsf(target-gpxMapDisplayedScale)>0.002f;
  return gpxMapDisplayedScale;
}
uint32_t gpxMapFrameIntervalMs() {
  if(gpxMapZoomAnimating)return 83;
  const float pixelsPerSecond=currentSpeedMph*0.44704f*gpxMapDisplayedScale;
  const int framesPerSecond=constrain(int(ceilf(pixelsPerSecond/0.8f)),4,12);
  return 1000/framesPerSecond;
}
constexpr float FREE_RIDE_MAP_SCALES[] = {0.32f,0.65f,1.35f};
void resetFreeRideMap(uint32_t now) {
  freeRideMapZoomLevel=1;
  freeRideMapDisplayedScale=FREE_RIDE_MAP_SCALES[freeRideMapZoomLevel];
  freeRideMapPreviousFrameMs=now;
  freeRideMapZoomAnimating=false;
}
int freeRideMapZoomControl(int x,int y) {
  if(x<ux(178) || x>ux(232))return 0;
  if(y>=uy(50) && y<=uy(111))return 1;
  if(y>=uy(129) && y<=uy(190))return -1;
  return 0;
}
void adjustFreeRideMapZoom(int direction,uint32_t now) {
  const int next=constrain(int(freeRideMapZoomLevel)+direction,0,2);
  if(next==freeRideMapZoomLevel)return;
  freeRideMapZoomLevel=uint8_t(next);
  freeRideMapPreviousFrameMs=now;
  freeRideMapZoomAnimating=true;
  previousDrawMs=0;
}
float smoothFreeRideMapScale(uint32_t now) {
  const float target=FREE_RIDE_MAP_SCALES[freeRideMapZoomLevel];
  const float seconds=freeRideMapPreviousFrameMs
      ? min(uint32_t(250),now-freeRideMapPreviousFrameMs)*0.001f : 0.0f;
  freeRideMapPreviousFrameMs=now;
  const float maximumStep=0.70f*seconds;
  freeRideMapDisplayedScale+=constrain(target-freeRideMapDisplayedScale,
                                      -maximumStep,maximumStep);
  freeRideMapZoomAnimating=fabsf(target-freeRideMapDisplayedScale)>0.002f;
  return freeRideMapDisplayedScale;
}
uint32_t freeRideMapFrameIntervalMs() {
  if(ridePaused)return 1000;
  if(freeRideMapZoomAnimating)return 83;
  const float pixelsPerSecond=currentSpeedMph*0.44704f*freeRideMapDisplayedScale;
  const int framesPerSecond=constrain(int(ceilf(pixelsPerSecond/0.8f)),4,12);
  return 1000/framesPerSecond;
}
void drawGpxMapReference(float mapScale,float mapBearing) {
  int scaleFeet;
  if(mapScale<0.48f)scaleFeet=1000;
  else if(mapScale<0.82f)scaleFeet=500;
  else scaleFeet=100;
  const int scalePixels=lroundf(scaleFeet*0.3048f*mapScale);
  constexpr int scaleX=90,scaleY=400;
  canvas->drawLine(scaleX,scaleY,scaleX+scalePixels,scaleY,WHITE);
  canvas->drawLine(scaleX,scaleY-us(3),scaleX,scaleY+us(3),WHITE);
  canvas->drawLine(scaleX+scalePixels,scaleY-us(3),scaleX+scalePixels,scaleY+us(3),WHITE);
  char label[16];snprintf(label,sizeof(label),"%d ft",scaleFeet);
  textCentered(label,scaleX+scalePixels/2,378,2,WHITE);

  // A self-explanatory compass pointer rotates against the rider-up map. The
  // outer chevron and inner needle move as a single mark, so no separate N is
  // needed and nothing can become visually detached while turning.
  constexpr int northX=330,northY=400;
  const float dx=-sinf(mapBearing),dy=-cosf(mapBearing);
  const float px=-dy,py=dx;
  const auto point=[&](float side,float forward,int &x,int &y) {
    x=northX+lroundf(px*side+dx*forward);
    y=northY+lroundf(py*side+dy*forward);
  };
  int tipX,tipY,leftX,leftY,notchX,notchY,rightX,rightY;
  point(0,27,tipX,tipY);
  point(-18,-20,leftX,leftY);
  point(0,-10,notchX,notchY);
  point(18,-20,rightX,rightY);
  wideLine(tipX,tipY,leftX,leftY,4,WHITE);
  wideLine(leftX,leftY,notchX,notchY,4,WHITE);
  wideLine(notchX,notchY,rightX,rightY,4,WHITE);
  wideLine(rightX,rightY,tipX,tipY,4,WHITE);
  int needleTipX,needleTipY,needleLeftX,needleLeftY,needleRightX,needleRightY;
  point(0,17,needleTipX,needleTipY);
  point(-6,-10,needleLeftX,needleLeftY);
  point(6,-10,needleRightX,needleRightY);
  canvas->fillTriangle(needleTipX,needleTipY,needleLeftX,needleLeftY,
                       needleRightX,needleRightY,WHITE);
  textCentered("N",northX,365,2,WHITE);
}
bool darkenGpxMapForehead() {
  uint16_t *pixels=reinterpret_cast<uint16_t*>(canvas->getFramebuffer());
  constexpr int fadeBottom=SCREEN_SIZE/2,topBrightness=51;
  for(int y=0;y<fadeBottom;++y) {
    if((y&7)==0 && (touchPending || touchActive))return false;
    const int brightness=topBrightness+(255-topBrightness)*y/(fadeBottom-1);
    const int framebufferColumn=SCREEN_SIZE-1-y;
    for(int x=0;x<SCREEN_SIZE;++x) {
      uint16_t &pixel=pixels[x*SCREEN_SIZE+framebufferColumn];
      const uint16_t color=pixel;
      const uint16_t red=((color>>11)&0x1f)*brightness/255;
      const uint16_t green=((color>>5)&0x3f)*brightness/255;
      const uint16_t blue=(color&0x1f)*brightness/255;
      pixel=uint16_t((red<<11)|(green<<5)|blue);
    }
  }
  return true;
}
void drawGpxRoute(float meters, bool demo, bool hasFix, float offset) {
  const auto position = demo || !hasFix ? gpx::sample(meters) : gpxLivePosition;
  const float bearing = !demo && std::isfinite(gpxLiveBearing) ? gpxLiveBearing : gpx::heading(meters);
  const float sine = sinf(bearing), cosine = cosf(bearing);
  const float eastScale=cos((gpx::lat0+position.north/(gpx::EARTH*gpx::RAD))*gpx::RAD)/gpx::lonScale;
  constexpr uint16_t routeBlue = 0x05BF;
  const auto mode=(demo || gpxArrived) ? gpx::ON_ROUTE : gpxGuidance.mode();
  const auto cue = !demo && gpxTurnPreview.active ? gpxTurnPreview.cue : gpx::nextCue(meters);
  const auto target=mode==gpx::TO_START ? gpx::sample(0) : gpxNearestCoursePoint;
  const bool guidance=mode!=gpx::ON_ROUTE;
  const float recoveryMeters=guidance ? gpx::pointDistance(position,target) : 0.0f;
  const float remaining=max(0.0f,cue.meters-meters);
  const float pixelsPerMeter=gpxSmoothMapScale(
      gpxTargetMapScale(guidance ? recoveryMeters : remaining),millis());
  beginFrame();
  canvas->fillScreen(0x0841);
  if(!drawSharedRoadMap(position,bearing,pixelsPerMeter))return;
  const float routeDrawMeters=guidance ? gpxRecoveryProgress : meters;
  // Short distance steps keep drawing bounded and avoid screen-spanning lines.
  bool previousVisible = false;
  int previousX = 0, previousY = 0;
  for (float d = fmaxf(0, routeDrawMeters-350); d <= fminf(gpx::length(), routeDrawMeters+850); d += 4) {
    if(touchPending || touchActive)return;
    const auto p = gpx::sample(d);
    const float east=(p.east-position.east)*eastScale, north=p.north-position.north;
    const int x = CENTER+lroundf((east*cosine-north*sine)*pixelsPerMeter);
    const int y = uy(185)-lroundf((east*sine+north*cosine)*pixelsPerMeter);
    const bool visible = x>-8 && x<SCREEN_SIZE+8 && y>-8 && y<SCREEN_SIZE+8;
    if (visible && previousVisible) {
      // Match the original demo's substantial 11-pixel route stroke. The old
      // wideLine helper only emitted three pixels regardless of its radius.
      drawSharedMapStroke(previousX,previousY,x,y,5,routeBlue);
    }
    previousVisible=visible; previousX=x; previousY=y;
  }
  char distance[32];
  // Near-course recovery uses familiar road-sign yellow. Magenta is reserved
  // for a substantial detour of one mile or more.
  constexpr uint16_t trafficSignYellow=0xFE60; // approximately #FFD000
  const uint16_t guidanceColor=mode==gpx::OFF_COURSE
      ? (recoveryMeters<1609.344f ? trafficSignYellow : 0xF81F)
      : WHITE;
  if (remaining >= 160.9344f) snprintf(distance,sizeof(distance),"%.1f mi",remaining/1609.344f);
  else snprintf(distance,sizeof(distance),"%.0f ft",remaining*3.28084f);
  if(guidance) {
    if(recoveryMeters>=160.9344f)snprintf(distance,sizeof(distance),"%.1f mi",recoveryMeters/1609.344f);
    else snprintf(distance,sizeof(distance),"%.0f ft",recoveryMeters*3.28084f);
  }
  const bool arrived = demo ? meters>=gpx::length() : gpxArrived;
  if(guidance) {
    // Show the recovery tangent once the ordered rejoin point is inside the
    // local map. The large direction arrow remains visible at any distance.
    const float targetEast=(target.east-position.east)*eastScale;
    const float targetNorth=target.north-position.north;
    const float screenDx=(targetEast*cosine-targetNorth*sine)*pixelsPerMeter;
    const float screenDy=-(targetEast*sine+targetNorth*cosine)*pixelsPerMeter;
    const int targetX=CENTER+lroundf(screenDx);
    const int targetY=uy(185)+lroundf(screenDy);
    const bool targetVisible=targetX>45 && targetX<421 && targetY>270 && targetY<432 &&
        screenDx*screenDx+screenDy*screenDy<205.0f*205.0f;
    int lineX=targetX,lineY=targetY;
    if(!targetVisible) {
      // Route matching continues even when the ordered rejoin point is beyond
      // this local map. Clip the recovery line to the visible map boundary so
      // the rider can still see that guidance remains active.
      const float dx=screenDx,dy=screenDy;
      float edgeScale=1.0f;
      if(dx>0.0f)edgeScale=min(edgeScale,178.0f/dx);
      else if(dx<0.0f)edgeScale=min(edgeScale,-178.0f/dx);
      if(dy>0.0f)edgeScale=min(edgeScale,86.0f/dy);
      else if(dy<0.0f)edgeScale=min(edgeScale,-68.0f/dy);
      lineX=CENTER+lroundf(dx*edgeScale);
      lineY=uy(185)+lroundf(dy*edgeScale);
    }
    wideLine(CENTER,uy(185),lineX,lineY,targetVisible ? 2 : 3,guidanceColor);
    if(targetVisible) {
      canvas->fillCircle(targetX,targetY,8,WHITE);
      canvas->fillCircle(targetX,targetY,4,routeBlue);
    } else {
      canvas->fillCircle(lineX,lineY,5,guidanceColor);
    }
  }
  const int riderY=uy(185);
  canvas->fillCircle(CENTER,riderY,us(13),WHITE);
  canvas->fillTriangle(CENTER,riderY-us(11),CENTER-us(8),riderY+us(8),
                       CENTER,riderY+us(4),0x039F);
  canvas->fillTriangle(CENTER,riderY-us(11),CENTER,riderY+us(4),
                       CENTER+us(8),riderY+us(8),0x039F);
  if(!darkenGpxMapForehead())return;
  // Redraw guidance over the shaded map using the same hierarchy as demo mode.
  // The compact map clock occupies the very top of the round display. Keep
  // the large maneuver distance below it with a deliberate visual gap.
  textCentered(arrived ? "ARRIVED" : distance,CENTER,58,5,arrived ? GREEN : guidanceColor);
  if(guidance) {
    drawGpxDirectionArrow(gpx::wrapAngle(gpx::pointBearing(position,target)-bearing),guidanceColor);
  } else if(cue.angle!=0 && !arrived) {
    const int side=cue.angle>0 ? 1 : -1;
    if(gpx::slightTurn(cue.angle))drawGpxDirectionArrow(side*45.0f*gpx::RAD,WHITE);
    else {
      canvas->fillRoundRect(CENTER-14,136,28,80,10,WHITE);
      canvas->fillRoundRect(side>0 ? CENTER-14 : CENTER-70,123,84,28,10,WHITE);
      const int tip=CENTER+side*92;
      canvas->fillTriangle(tip,137,tip-side*45,95,tip-side*45,179,WHITE);
    }
  } else {
    drawGpxDirectionArrow(0,WHITE);
    canvas->fillCircle(CENTER,92,14,GREEN);
    canvas->fillCircle(CENTER,92,7,0x0841);
  }
  if(guidance) {
    const char *label=mode==gpx::TO_START ? "To Start" :
                      (mode==gpx::TO_ROUTE ? "To Route" : "Off Course");
    textCentered(label,CENTER,247,3,guidanceColor);
  }
  drawGpxMapReference(pixelsPerMeter,bearing);
  if (demo && gpxSimulation.paused) textCentered("PAUSED",CENTER,419,2,WHITE);
  if (!demo) drawRidePageDots();
  endAnimatedFrame();
}

bool updateGpxLiveFix(uint32_t now,float &course) {
  LocationPacket packet;uint32_t received;
  portENTER_CRITICAL(&locationMux);
  packet=latestLocation;received=lastLocationMs;
  gpxFixAvailable=hasLocation && timestampIsFresh(now,received,LOCATION_TIMEOUT_MS);
  portEXIT_CRITICAL(&locationMux);
  if(!gpxFixAvailable || received==gpxFixReceived)return false;
  gpxFixReceived=received;
  const float east=gpx::longitudeDelta(gpx::lon0,packet.longitudeE7*1e-7)*gpx::RAD*gpx::EARTH*gpx::lonScale;
  const float north=(packet.latitudeE7*1e-7-gpx::lat0)*gpx::RAD*gpx::EARTH;
  gpxLivePosition={east,north,0};
  course=(packet.flags & 4) && (packet.flags & 1) && packet.speedCms>=80 && packet.courseDeg100<36000
      ? packet.courseDeg100*0.01f*gpx::RAD : NAN;
  if(std::isfinite(course)) {
    if(std::isfinite(gpxLiveBearing))
      gpxLiveBearing=gpx::wrapAngle(gpxLiveBearing+0.25f*gpx::wrapAngle(course-gpxLiveBearing));
    else gpxLiveBearing=course;
  }
  return true;
}
void updateGpxPosition(uint32_t now) {
  float course=NAN;
  if(!updateGpxLiveFix(now,course))return;
  const float east=gpxLivePosition.east,north=gpxLivePosition.north;
  // Before joining, search the complete route so a ride can begin midway.
  // Once riding, allow modest look-ahead but never jump to a geometrically-near
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
                                         : gpx::length();
  const auto orderedMatch=gpx::nearestMatch(east,north,minimum,maximum,course);
  if(!orderedMatch.valid)return;
  auto match=orderedMatch;
  bool wrongWay=gpx::wrongDirection(match,course);
  bool globalRejoin=false;
  // When the ordered window no longer contains the road under the rider,
  // search the whole GPX with heading weighting. A close, direction-compatible
  // result is a real rejoin even when it is behind or far ahead of progress.
  if(gpxGuidance.offCourse || orderedMatch.offset>gpx::OFF_COURSE_DISTANCE_METERS || wrongWay) {
    const auto globalMatch=gpx::nearestMatch(east,north,0,gpx::length(),course);
    if(gpx::canGloballyRejoin(globalMatch,course,previous)) {
      match=globalMatch;
      wrongWay=false;
      globalRejoin=true;
    }
  }
  const float distanceToTarget=gpx::pointDistance(gpxLivePosition,gpx::sample(match.meters));
  const bool wasJoined=gpxGuidance.joined;
  const bool wasOffCourse=gpxGuidance.offCourse;
  gpxGuidance.update(distanceToTarget,match.offset,wrongWay,match.meters);
  // Route progress remains ordered, but recovery guidance must point to the
  // actual closest tangent even when that point falls behind the progress
  // window or on a nearby return leg.
  const auto recovery=gpxGuidance.offCourse ? gpx::nearestTangent(east,north) : match;
  if(recovery.valid) {
    gpxRecoveryProgress=recovery.meters;
    gpxNearestCoursePoint=gpx::sample(recovery.meters);
  }
  if(!wasJoined && gpxGuidance.joined)gpxLastProgress=match.meters;
  if(!wasOffCourse && gpxGuidance.offCourse) {
    gpxOffCourseStartRideMeters=distanceMiles*1609.344f;
    gpxOffCourseStartProgress=previous;
    gpxOffCourseSinceMs=now;
  } else if(wasOffCourse && !gpxGuidance.offCourse) {
    gpxOffCourseStartRideMeters=-1;
    gpxOffCourseSinceMs=0;
  }
  // Freeze ordered progress while off course or riding the route backwards.
  // Rejoining within the forward window advances directly to that point.
  if(globalRejoin && !gpxGuidance.offCourse)
    gpxLastProgress=match.meters;
  else if(gpxGuidance.joined && match.offset<=gpx::OFF_COURSE_DISTANCE_METERS && !wrongWay)
    gpxLastProgress=max(max(0.0f,gpxLastProgress),match.meters);
  else if(gpxLastProgress<0)
    gpxLastProgress=0;
  if(gpxGuidance.mode()==gpx::ON_ROUTE && gpxLastProgress>=gpx::length()-20 &&
     gpx::pointDistance(gpxLivePosition,gpx::sample(gpx::length()))<=20)gpxArrived=true;
}
void drawLiveGpxPage(uint32_t now) {
  if(!routeNavigationEnabled) {
    beginFrame();drawClock(now,us(10),2);
    textCentered("No GPX selected",CENTER,uy(115),3,WHITE);
    drawRidePageDots();endFrame();return;
  }
  updateGpxPosition(now);
  if(!gpxFixAvailable) {
    beginFrame();drawClock(now,us(10),2);
    textCentered("Waiting for GPS",CENTER,uy(105),3,WHITE);
    drawRidePageDots();endFrame();return;
  }
  drawGpxRoute(max(0.0f,gpxLastProgress),false,true,0);
}
void drawFreeRideMapPage(uint32_t now) {
  float unusedCourse=NAN;
  updateGpxLiveFix(now,unusedCourse);
  if(!gpxFixAvailable) {
    beginFrame();
    drawClock(now,us(10),2);
    textCentered("Waiting for GPS",CENTER,uy(105),3,WHITE);
    drawRidePageDots();
    endFrame();
    return;
  }
  const float bearing=std::isfinite(gpxLiveBearing) ? gpxLiveBearing : 0.0f;
  const float mapScale=smoothFreeRideMapScale(now);
  beginFrame();
  canvas->fillScreen(0x0841);
  if(!drawSharedRoadMap(gpxLivePosition,bearing,mapScale))return;

  const int riderY=uy(185);
  canvas->fillCircle(CENTER,riderY,us(13),WHITE);
  canvas->fillTriangle(CENTER,riderY-us(11),CENTER-us(8),riderY+us(8),
                       CENTER,riderY+us(4),0x039F);
  canvas->fillTriangle(CENTER,riderY-us(11),CENTER,riderY+us(4),
                       CENTER+us(8),riderY+us(8),0x039F);

  constexpr int controlX=390;
  const int plusY=uy(81),minusY=uy(159),radius=us(23);
  canvas->fillCircle(controlX,plusY,radius,BLACK);
  canvas->drawCircle(controlX,plusY,radius,WHITE);
  canvas->drawCircle(controlX,plusY,radius-1,WHITE);
  canvas->drawFastHLine(controlX-us(9),plusY,us(18),WHITE);
  canvas->drawFastVLine(controlX,plusY-us(9),us(18),WHITE);
  canvas->drawFastHLine(controlX-us(22),CENTER,us(44),0x8410);
  canvas->fillCircle(controlX,minusY,radius,BLACK);
  canvas->drawCircle(controlX,minusY,radius,WHITE);
  canvas->drawCircle(controlX,minusY,radius-1,WHITE);
  canvas->drawFastHLine(controlX-us(9),minusY,us(18),WHITE);

  drawClock(now,us(10),2);
  drawRidePageDots();
  drawPausedBadge();
  endAnimatedFrame();
}

void drawRidarRoute(const GpxPoint &mapCenter,float bearing,float mapScale) {
  if(!routeNavigationEnabled || gpx::count<2)return;
  const float sine=sinf(bearing),cosine=cosf(bearing);
  constexpr uint16_t routeBlue=0x05BF;
  bool previousVisible=false;
  int previousX=0,previousY=0;
  for(unsigned i=0;i<gpx::count;++i) {
    if((i&31)==0 && (touchPending || touchActive))return;
    const auto &point=gpx::points[i];
    const float east=point.east-mapCenter.east;
    const float north=point.north-mapCenter.north;
    const int x=CENTER+lroundf((east*cosine-north*sine)*mapScale);
    const int y=uy(185)-lroundf((east*sine+north*cosine)*mapScale);
    const bool visible=x>-8 && x<SCREEN_SIZE+8 && y>-8 && y<SCREEN_SIZE+8;
    if(visible && previousVisible)
      drawSharedMapStroke(previousX,previousY,x,y,3,routeBlue);
    previousVisible=visible;
    previousX=x;previousY=y;
  }
}

uint16_t ridarRgb332To565(uint8_t color) {
  const uint8_t red=(color>>5)&0x07;
  const uint8_t green=(color>>2)&0x07;
  const uint8_t blue=color&0x03;
  return uint16_t((uint16_t(red*31/7)<<11) |
                  (uint16_t(green*63/7)<<5) |
                  uint16_t(blue*31/3));
}

void drawRidarPeerEmoji(int centerX,int centerY,const uint8_t *pixels) {
  if(!pixels)return;
  const int scale=max(1,us(1));
  const int width=RiderNetwork::ICON_WIDTH*scale;
  const int height=RiderNetwork::ICON_HEIGHT*scale;
  canvas->fillCircle(centerX,centerY,max(width,height)/2+us(2),BLACK);
  const int left=centerX-width/2;
  const int top=centerY-height/2;
  for(uint8_t y=0;y<RiderNetwork::ICON_HEIGHT;++y) {
    for(uint8_t x=0;x<RiderNetwork::ICON_WIDTH;++x) {
      const uint8_t color=pixels[uint16_t(y)*RiderNetwork::ICON_WIDTH+x];
      if(!color)continue;
      canvas->fillRect(left+x*scale,top+y*scale,scale,scale,
                       ridarRgb332To565(color));
    }
  }
}

void drawRidarLocalEmoji(int centerX,int centerY) {
  if(!deviceIcon.availableFor(deviceEmoji))return;
  const int scale=max(1,us(1));
  const int width=RiderNetwork::ICON_WIDTH*scale;
  const int height=RiderNetwork::ICON_HEIGHT*scale;
  canvas->fillCircle(centerX,centerY,max(width,height)/2+us(2),BLACK);
  const int left=centerX-width/2;
  const int top=centerY-height/2;
  const uint16_t *pixels=deviceIcon.pixels();
  for(uint8_t y=0;y<RiderNetwork::ICON_HEIGHT;++y) {
    const uint8_t sourceY=uint8_t(
        (uint16_t(y)*deviceIcon.height())/RiderNetwork::ICON_HEIGHT);
    for(uint8_t x=0;x<RiderNetwork::ICON_WIDTH;++x) {
      const uint8_t sourceX=uint8_t(
          (uint16_t(x)*deviceIcon.width())/RiderNetwork::ICON_WIDTH);
      const uint16_t color=pixels[uint16_t(sourceY)*deviceIcon.width()+sourceX];
      if(color==BLACK)continue;
      canvas->fillRect(left+x*scale,top+y*scale,scale,scale,color);
    }
  }
}

void drawRidarPage(uint32_t now) {
  if(!ridarEnabled) {
    beginFrame();
    drawClock(now,us(10),2);
    textCentered("RiDar Off",CENTER,uy(105),4,WHITE);
    textCentered("Enable in Menu",CENTER,uy(137),2,0xAD55);
    drawRidePageDots();
    endFrame();
    return;
  }

  float unusedCourse=NAN;
  updateGpxLiveFix(now,unusedCourse);
  if(!gpxFixAvailable) {
    beginFrame();
    drawClock(now,us(10),2);
    textCentered("Waiting for GPS",CENTER,uy(105),3,WHITE);
    textCentered("RiDar listening",CENTER,uy(137),2,0xAD55);
    drawRidePageDots();
    endFrame();
    return;
  }

  struct ScreenRider { GpxPoint position; };
  RiderNetwork::Rider snapshot[RiderNetwork::MAX_RIDERS]{};
  ScreenRider riders[RiderNetwork::MAX_RIDERS]{};
  const size_t riderCount=riderNetwork.snapshot(
      snapshot,RiderNetwork::MAX_RIDERS,now);
  const float bearing=std::isfinite(gpxLiveBearing) ? gpxLiveBearing : 0.0f;
  const float sine=sinf(bearing),cosine=cosf(bearing);
  float minimumRight=0,maximumRight=0,minimumForward=0,maximumForward=0;
  for(size_t i=0;i<riderCount;++i) {
    const float east=gpx::longitudeDelta(
        gpx::lon0,snapshot[i].longitudeE7*1e-7)*gpx::RAD*gpx::EARTH*gpx::lonScale;
    const float north=(snapshot[i].latitudeE7*1e-7-gpx::lat0)*gpx::RAD*gpx::EARTH;
    riders[i]={{east,north,0}};
    const float deltaEast=east-gpxLivePosition.east;
    const float deltaNorth=north-gpxLivePosition.north;
    const float right=deltaEast*cosine-deltaNorth*sine;
    const float forward=deltaEast*sine+deltaNorth*cosine;
    minimumRight=min(minimumRight,right);maximumRight=max(maximumRight,right);
    minimumForward=min(minimumForward,forward);maximumForward=max(maximumForward,forward);
  }

  // Include comfortable marker/name padding, then stop at the existing
  // closest 100-foot scale even when the group is tightly packed.
  const float targetScale=max(0.05f,min(1.35f,min(
      350.0f/max(1.0f,maximumRight-minimumRight+50.0f),
      340.0f/max(1.0f,maximumForward-minimumForward+50.0f))));
  const float seconds=ridarMapPreviousFrameMs
      ? min(uint32_t(250),now-ridarMapPreviousFrameMs)*0.001f : 0.0f;
  ridarMapPreviousFrameMs=now;
  const float maximumStep=0.8f*seconds;
  ridarMapDisplayedScale+=constrain(targetScale-ridarMapDisplayedScale,
                                   -maximumStep,maximumStep);
  ridarMapZoomAnimating=fabsf(targetScale-ridarMapDisplayedScale)>0.002f;
  const float mapScale=ridarMapDisplayedScale;

  // Center the complete group in the useful map area. The shared renderer's
  // normal navigation anchor is low on the screen, so shift its world center
  // rearward until the group midpoint appears near the physical screen center.
  const float centerRight=(minimumRight+maximumRight)*0.5f;
  const float centerForward=(minimumForward+maximumForward)*0.5f;
  const float desiredGroupY=230.0f;
  const float mapOriginY=float(uy(185));
  const float mapCenterForward=centerForward-(mapOriginY-desiredGroupY)/mapScale;
  GpxPoint mapCenter={
      gpxLivePosition.east+centerRight*cosine+mapCenterForward*sine,
      gpxLivePosition.north-centerRight*sine+mapCenterForward*cosine,0};

  beginFrame();
  canvas->fillScreen(0x0841);
  if(!drawSharedRoadMap(mapCenter,bearing,mapScale))return;
  drawRidarRoute(mapCenter,bearing,mapScale);

  const auto markerPosition=[&](const GpxPoint &point,int &x,int &y) {
    const float east=point.east-mapCenter.east;
    const float north=point.north-mapCenter.north;
    x=CENTER+lroundf((east*cosine-north*sine)*mapScale);
    y=uy(185)-lroundf((east*sine+north*cosine)*mapScale);
    return x>=18 && x<=SCREEN_SIZE-18 && y>=18 && y<=SCREEN_SIZE-18;
  };

  for(size_t i=0;i<riderCount;++i) {
    const uint32_t age=uint32_t(now-snapshot[i].lastSeenMs);
    int x=0,y=0;
    if(!markerPosition(riders[i].position,x,y))continue;
    if(snapshot[i].iconAvailable)
      drawRidarPeerEmoji(x,y,snapshot[i].iconPixels);
    else {
      canvas->fillCircle(x,y,us(7),WHITE);
      canvas->fillCircle(x,y,us(5),age>8000 ? 0x8410 : GREEN);
    }
  }
  int selfX=0,selfY=0;
  if(markerPosition(gpxLivePosition,selfX,selfY)) {
    if(deviceIcon.availableFor(deviceEmoji))drawRidarLocalEmoji(selfX,selfY);
    else {
      canvas->fillCircle(selfX,selfY,us(11),WHITE);
      canvas->fillCircle(selfX,selfY,us(7),0x039F);
    }
  }

  char title[24];
  snprintf(title,sizeof(title),"RiDar  %u",unsigned(riderCount));
  drawClock(now,us(10),2);
  canvas->fillRoundRect(CENTER-us(48),us(23),us(96),us(22),us(7),BLACK);
  textCentered(title,CENTER,us(34),2,WHITE);
  drawGpxMapReference(mapScale,bearing);
  drawRidePageDots();
  drawPausedBadge();
  endAnimatedFrame();
}
