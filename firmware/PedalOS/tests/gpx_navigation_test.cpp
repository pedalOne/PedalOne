#include "../gpx_navigation.h"
#include "../gpx_defaults.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <initializer_list>
int main() {
  assert(gpx::slightTurn(44.9f*gpx::RAD));
  assert(gpx::slightTurn(-30*gpx::RAD));
  assert(!gpx::slightTurn(45*gpx::RAD));
  assert(!gpx::slightTurn(-90*gpx::RAD));
  gpx::GuidancePageAlert alert;
  int alertPage=1;
  alert.update(true,false,alertPage);assert(alertPage==gpx::PAGE_INDEX && alert.active);
  alert.dismiss(alertPage);assert(alertPage==1);
  alert.update(true,false,alertPage);assert(alertPage==1 && !alert.active);
  alert.update(false,false,alertPage);
  alert.update(true,false,alertPage);assert(alert.active);
  alert.update(false,false,alertPage);assert(alertPage==1 && !alert.active);
  alert.update(false,true,alertPage);assert(alertPage==gpx::PAGE_INDEX && alert.active);
  alert.dismiss(alertPage);alert.update(false,true,alertPage);assert(alertPage==1 && !alert.active);
  gpx::TurnPreview preview;
  int page=1;
  preview.update(100,{300,1},true,page);assert(page==1 && !preview.active);
  preview.update(140,{300,1},false,page);assert(page==1);
  preview.update(140,{300,1},true,page);assert(page==gpx::PAGE_INDEX && preview.active);
  preview.update(299,{450,-1},true,page);assert(page==gpx::PAGE_INDEX && preview.cue.meters==300);
  preview.update(309,{450,-1},true,page);assert(preview.active);
  preview.update(310,{450,-1},true,page);assert(page==1 && !preview.active);
  preview.update(315,{450,-1},true,page);assert(preview.active);
  preview.dismiss(page);assert(page==1 && !preview.active);
  preview.update(320,{450,-1},true,page);assert(page==1 && !preview.active);
  preview.update(470,{600,1},true,page);assert(page==gpx::PAGE_INDEX && preview.active);
  preview.reset();page=gpx::PAGE_INDEX;
  preview.update(470,{600,1},true,page);assert(!preview.active && page==gpx::PAGE_INDEX);
  page=0;preview.update(470,{600,0},true,page);assert(!preview.active);
  for (unsigned i=1; i<GPX_COUNT; ++i) assert(GPX_POINTS[i].meters>GPX_POINTS[i-1].meters);
  gpx::Simulation sim;
  sim.reset(100);
  sim.tick(60100);
  assert(std::fabs(sim.meters-1072.896f)<0.01f);
  sim.paused=true; sim.tick(120100);
  assert(std::fabs(sim.meters-1072.896f)<0.01f);
  sim.paused=false; sim.tick(180100);
  assert(std::fabs(sim.meters-2145.792f)<0.01f);
  gpx::Simulation frequent;
  frequent.reset(0);
  for (unsigned ms=5; ms<=120000; ms+=5) frequent.tick(ms);
  assert(std::fabs(frequent.meters-sim.meters)<0.01f);
  sim.reset(UINT32_MAX-499);
  sim.tick(500);
  assert(std::fabs(sim.meters-17.8816f)<0.001f);
  sim.reset(0); sim.tick(4000000);
  assert(sim.finished() && sim.meters==GPX_LENGTH);
  sim.tick(4100000); assert(sim.meters==GPX_LENGTH);
  assert(gpx::sample(-1).meters==0);
  assert(gpx::sample(GPX_LENGTH+100).meters==GPX_LENGTH);
  for (float d=0; d<GPX_LENGTH; d+=17.8816f) {
    const auto p=gpx::sample(d);
    assert(std::isfinite(p.east) && std::isfinite(p.north));
    assert(std::isfinite(gpx::heading(d)));
    const auto cue=gpx::nextCue(d);
    assert(cue.meters>=d && cue.meters<=GPX_LENGTH);
    const auto match=gpx::nearestMatch(p.east,p.north,0,gpx::length());
    assert(match.valid && match.offset<0.01f);
  }
  const auto *originalPoints=gpx::points;
  const unsigned originalCount=gpx::count;
  const double originalLat=gpx::lat0, originalLon=gpx::lon0, originalScale=gpx::lonScale;
  const GpxPoint path[]={{0,0,0},{0,100,100},{100,100,200},{0,100,300}};
  gpx::points=path;gpx::count=4;gpx::lat0=0;gpx::lon0=0;gpx::lonScale=1;
  assert(std::fabs(gpx::heading(0))<1e-5);
  assert(std::fabs(gpx::heading(99))<1e-5);
  assert(std::fabs(gpx::heading(100)-float(M_PI/2))<1e-4);
  assert(std::fabs(gpx::heading(300)+float(M_PI/2))<1e-4);
  for(float angle: {0.f,float(M_PI/2),float(M_PI),float(-M_PI/2)}) {
    auto ahead=gpx::rotate(std::sin(angle),std::cos(angle),angle);
    assert(std::fabs(ahead.x)<1e-5 && std::fabs(ahead.y+1)<1e-5);
  }
  auto northAtEast=gpx::rotate(0,1,float(M_PI/2));
  assert(northAtEast.x < -0.999f && std::fabs(northAtEast.y)<1e-5);
  assert(std::fabs(gpx::nearestMatch(50,100,100,200,M_PI/2).meters-150)<0.01f);
  assert(std::fabs(gpx::nearestMatch(50,100,200,300,-M_PI/2).meters-250)<0.01f);
  // Recovery projects onto the segment interior, rather than a waypoint,
  // independent of a rider's matched progress or travel direction.
  auto recovery=gpx::sample(gpx::nearestMatch(50,160,0,gpx::length()).meters);
  assert(std::fabs(recovery.east-50)<0.01f && std::fabs(recovery.north-100)<0.01f);
  assert(std::fabs(gpx::nearestMatch(50,160,0,gpx::length()).offset-60)<0.01f);
  assert(std::fabs(std::fabs(gpx::pointBearing({50,160,0},recovery))-float(M_PI))<0.001f);
  // A heading-weighted ordered match may prefer another segment, while the
  // recovery tangent must remain the strictly closest geometric projection.
  const auto strictTangent=gpx::nearestTangent(50,160);
  assert(std::fabs(strictTangent.meters-150)<0.01f &&
         std::fabs(strictTangent.offset-60)<0.01f);
  const auto farWindow=gpx::nearestMatch(50,102,0,100,0);
  const auto globalRejoin=gpx::nearestMatch(50,102,0,gpx::length(),float(-M_PI/2));
  assert(farWindow.offset>40);
  assert(globalRejoin.offset<3 && globalRejoin.meters>=250);
  assert(gpx::canGloballyRejoin(globalRejoin,float(-M_PI/2),50));
  assert(!gpx::canGloballyRejoin(globalRejoin,float(M_PI/2),50));
  recovery=gpx::sample(gpx::nearestMatch(-50,-20,0,gpx::length()).meters);
  assert(recovery.east==0 && recovery.north==0); // Clamp past an endpoint.
  assert(std::fabs(gpx::bearing(0,179.9,0,-179.9)-M_PI/2)<1e-5);
  const gpx::Coordinate geo[]={{0,1799999000},{0,-1799999000}};
  GpxPoint projected[2];assert(gpx::build(geo,2,projected));
  assert(projected[1].east>20 && projected[1].east<23);
  const gpx::Coordinate duplicate[]={{0,0},{0,0}};
  assert(!gpx::build(duplicate,2,projected));
  gpx::points=originalPoints;gpx::count=originalCount;gpx::lat0=originalLat;gpx::lon0=originalLon;gpx::lonScale=originalScale;
  gpx::Guidance guidance;
  guidance.update(500,0);assert(guidance.mode()==gpx::TO_START);
  guidance.update(30,5);assert(guidance.mode()==gpx::ON_ROUTE);
  guidance.update(200,61);assert(guidance.mode()==gpx::OFF_COURSE);
  guidance.update(200,40);assert(guidance.mode()==gpx::OFF_COURSE);
  guidance.update(200,34);assert(guidance.mode()==gpx::ON_ROUTE);
  guidance.reset();assert(guidance.mode()==gpx::TO_START);
  guidance.update(80,80,false,150);assert(guidance.mode()==gpx::TO_ROUTE);
  guidance.update(5,5,false,150);assert(guidance.mode()==gpx::ON_ROUTE);
  GpxPoint defaultGeometry[gpx::MAX_POINTS];
  for(const auto &route:GPX_DEFAULTS) {
    assert(gpx::checksum(reinterpret_cast<const uint8_t*>(route.points),route.count*8)==route.checksum);
    assert(gpx::build(route.points,route.count,defaultGeometry));
    assert(route.thumbnailBytes==7732);
    assert(route.thumbnail[0]=='P' && route.thumbnail[1]=='1' && route.thumbnail[2]=='T' && route.thumbnail[3]=='H');
    assert(route.thumbnail[4]==1 && route.thumbnail[5]==160 && route.thumbnail[6]==96 && route.thumbnail[7]==16);
    assert(gpx::checksum(route.thumbnail,route.thumbnailBytes)==route.thumbnailChecksum);
    uint32_t thumbnailRouteID=0;memcpy(&thumbnailRouteID,route.thumbnail+8,4);
    assert(thumbnailRouteID==route.id);
  }
  puts("PASS: 40 mph, pause/resume, clock wrap, arrival clamp, entire-route geometry and previews");
}
