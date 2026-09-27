// GPX ride page with a full-width cue over the shared nearby-road map.
constexpr uint16_t DASH_ROUTE_BLUE = 0x05BF;

void drawDashboardManeuver(int cx,int cy,float angle,bool arrived,
                           bool recovery,uint16_t color) {
  if(recovery) {
    drawGpxDirectionArrow(isfinite(angle) ? angle : 0,color,cx,cy);
    return;
  }
  if(arrived) {
    drawGpxDestinationArrow(cx,cy);
    return;
  }
  if(!isfinite(angle))angle=0;
  // Restore the compact, centered maneuver artwork from the v130 dashboard.
  const int side=angle<0 ? -1 : 1;
  const bool turn=fabsf(angle)>20.0f*gpx::RAD;
  const bool slight=turn && gpx::slightTurn(angle);
  const int shaftBottom=cy+us(26);
  const int shaftTop=cy-us(6);
  const int halfWidth=us(4);
  if(!turn) {
    drawSharedMapStroke(cx,shaftBottom,cx,cy-us(10),halfWidth,color);
    canvas->fillTriangle(cx,cy-us(27),cx-us(14),cy-us(8),
                         cx+us(14),cy-us(8),color);
    return;
  }
  drawSharedMapStroke(cx,shaftBottom,cx,shaftTop,halfWidth,color);
  if(slight) {
    const int branchX=cx+side*us(22);
    const int branchY=cy-us(18);
    drawSharedMapStroke(cx,shaftTop,branchX,branchY,halfWidth,color);
    canvas->fillTriangle(branchX+side*us(10),branchY-us(8),
                         branchX-side*us(12),branchY-us(7),
                         branchX+side*us(4),branchY+us(10),color);
  } else {
    const int branchX=cx+side*us(30);
    drawSharedMapStroke(cx,shaftTop,branchX,shaftTop,halfWidth,color);
    canvas->fillTriangle(branchX+side*us(15),shaftTop,
                         branchX-side*us(5),shaftTop-us(13),
                         branchX-side*us(5),shaftTop+us(13),color);
  }
}

void drawDashboardRoute(float progress,const GpxPoint &position,
                        float bearing,bool recovery,float mapScale) {
  const float sine=sinf(bearing),cosine=cosf(bearing);
  const float eastScale=cosf((gpx::lat0+position.north/
      (gpx::EARTH*gpx::RAD))*gpx::RAD)/gpx::lonScale;
  const int riderY=uy(165);
  const float routePosition=recovery ? gpxRecoveryProgress : progress;
  bool previousVisible=false;
  int previousX=0,previousY=0;
  for(float d=max(0.0f,routePosition-300.0f);
      d<=min(gpx::length(),routePosition+1000.0f);d+=5.0f) {
    if(touchPending || touchActive)return;
    const GpxPoint point=gpx::sample(d);
    const float east=(point.east-position.east)*eastScale;
    const float north=point.north-position.north;
    const int x=CENTER+lroundf((east*cosine-north*sine)*mapScale);
    const int y=riderY-lroundf((east*sine+north*cosine)*mapScale);
    const bool visible=x>-8 && x<SCREEN_SIZE+8 &&
                       y>-8 && y<SCREEN_SIZE+8;
    if(visible && previousVisible)
      drawSharedMapStroke(previousX,previousY,x,y,5,DASH_ROUTE_BLUE);
    previousVisible=visible;previousX=x;previousY=y;
  }
}

void drawDashboardScaleAndRider(bool fix,float mapScale) {
  const int scaleX=ux(30),scaleY=uy(167);
  const int scaleFeet=mapScale<0.48f ? 1000 : (mapScale<0.82f ? 500 : 100);
  const int scaleLength=lroundf(scaleFeet*0.3048f*mapScale);
  canvas->drawFastHLine(scaleX,scaleY,scaleLength,0xAD55);
  canvas->drawFastVLine(scaleX,scaleY-us(3),us(6),0xAD55);
  canvas->drawFastVLine(scaleX+scaleLength,scaleY-us(3),us(6),0xAD55);
  char scaleLabel[16];
  snprintf(scaleLabel,sizeof(scaleLabel),"%d ft",scaleFeet);
  textCentered(scaleLabel,scaleX+scaleLength/2,uy(159),1,WHITE);
  if(fix) {
    const int riderY=uy(165);
    canvas->fillCircle(CENTER,riderY,us(8),WHITE);
    canvas->fillTriangle(CENTER,riderY-us(7),CENTER-us(5),riderY+us(5),
                         CENTER+us(5),riderY+us(5),0x039F);
  }
}

void drawRouteDashboardPage(uint32_t now) {
  updateGpxPosition(now);
  const float progress=max(0.0f,gpxLastProgress);
  const auto mode=gpxArrived ? gpx::ON_ROUTE : gpxGuidance.mode();
  const bool recovery=mode!=gpx::ON_ROUTE;
  const GpxPoint position=gpxFixAvailable ? gpxLivePosition
                                          : gpx::sample(progress);
  const float bearing=isfinite(gpxLiveBearing)
      ? gpxLiveBearing : gpx::heading(progress);
  const auto cue=gpxTurnPreview.active ? gpxTurnPreview.cue
                                       : gpx::nextCue(progress);
  const GpxPoint target=mode==gpx::TO_START ? gpx::sample(0)
                                             : gpxNearestCoursePoint;
  const float remaining=recovery && gpxFixAvailable
      ? gpx::pointDistance(position,target)
      : max(0.0f,cue.meters-progress);
  const float mapScale=gpxSmoothMapScale(gpxTargetMapScale(remaining),now);
  beginFrame();
  canvas->fillScreen(0x0841);
  // Keep road and route projection anchored to the same lower rider position.
  if(!drawSharedRoadMap(position,bearing,mapScale,uy(165),0x736E))return;
  drawDashboardRoute(progress,position,bearing,recovery,mapScale);
  if(touchPending || touchActive)return;
  if(!darkenGpxMapForehead())return;
  drawHeader(now);

  char distanceText[20];
  if(!gpxFixAvailable)snprintf(distanceText,sizeof(distanceText),"GPS...");
  else if(gpxArrived)snprintf(distanceText,sizeof(distanceText),"ARRIVED");
  else if(remaining>=160.9344f)
    snprintf(distanceText,sizeof(distanceText),"%.1f mi",remaining/1609.344f);
  else snprintf(distanceText,sizeof(distanceText),"%.0f ft",remaining*3.28084f);
  constexpr uint16_t offCourseColor=0xF81F;
  const uint16_t cueColor=gpxArrived ? GREEN :
      (mode==gpx::OFF_COURSE ? offCourseColor : WHITE);
  textCentered(distanceText,CENTER,uy(65),5,cueColor);
  if(gpxFixAvailable) {
    const float angle=recovery
        ? gpx::wrapAngle(gpx::pointBearing(position,target)-bearing)
        : cue.angle;
    // Keep the v130 turn placement. Lower the arrival marker slightly so its
    // green circle clears the larger ARRIVED label above it.
    drawDashboardManeuver(CENTER,uy(gpxArrived ? 118 : (recovery ? 105 : 110)),
                          angle,gpxArrived,recovery,cueColor);
    if(recovery)
      textCentered(mode==gpx::TO_START ? "TO START" :
                   (mode==gpx::TO_ROUTE ? "TO ROUTE" : "OFF COURSE"),
                   CENTER,uy(148),2,cueColor);
  }
  drawDashboardScaleAndRider(gpxFixAvailable,mapScale);

  // Speed is communicated by the horizontal 0–25 mph bar; no speed numeral.
  const float speed=isfinite(currentSpeedMph)
      ? max(0.0f,currentSpeedMph) : 0.0f;
  const int left=ux(28),right=ux(212),top=uy(178),height=us(7);
  canvas->fillRoundRect(left,top,right-left,height,height/2,0x2104);
  const int filled=lroundf((right-left)*constrain(speed/25.0f,0.0f,1.0f));
  for(int x=0;x<filled;++x)
    canvas->drawFastVLine(left+x,top,height,speedArcColor(float(x)/(right-left)));
  for(int i=1;i<5;++i)
    canvas->drawFastVLine(left+(right-left)*i/5,top-us(2),height+us(4),WHITE);

  // Two quiet panels keep ride distance and time legible over nearby streets.
  canvas->fillRoundRect(ux(45),uy(187),us(61),us(34),us(6),0x0841);
  canvas->fillRoundRect(ux(120),uy(187),us(71),us(34),us(6),0x0841);
  const uint32_t seconds=activeRideElapsedMs(now)/1000;
  char rideDistance[16],rideTime[16];
  const float miles=isfinite(distanceMiles) ? max(0.0f,distanceMiles) : 0.0f;
  snprintf(rideDistance,sizeof(rideDistance),miles<9.995f ? "%.2f" : "%.1f",miles);
  if(seconds<3600)
    snprintf(rideTime,sizeof(rideTime),"%02lu:%02lu",
             (unsigned long)(seconds/60),(unsigned long)(seconds%60));
  else snprintf(rideTime,sizeof(rideTime),"%02lu:%02lu:%02lu",
                (unsigned long)(seconds/3600),
                (unsigned long)((seconds/60)%60),(unsigned long)(seconds%60));
  textCentered("DIST",ux(76),uy(194),1,0x8410);
  textCentered(rideDistance,ux(76),uy(207),4,WHITE);
  textCentered("TIME",ux(151),uy(194),1,0x8410);
  textCentered(rideTime,ux(151),uy(207),3,WHITE);
  drawRidePageDots();
  drawPausedBadge();
  endAnimatedFrame();
}
