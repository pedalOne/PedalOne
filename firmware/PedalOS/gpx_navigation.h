#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include "gpx_example.h"

namespace gpx {
constexpr float SPEED_MPS = 17.8816f;
constexpr unsigned MAX_POINTS = 4096;
constexpr double RAD = 0.017453292519943295;
constexpr double EARTH = 6371000;
struct Coordinate { int32_t latE7, lonE7; };
static const GpxPoint *points = GPX_POINTS;
static unsigned count = GPX_COUNT;
static uint32_t revision=0;
static double lat0 = GPX_LAT0, lon0 = GPX_LON0, lonScale = GPX_LON_SCALE;
static char name[33] = "Hill Climb Clinic";
inline float length() { return points[count-1].meters; }
inline float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double longitudeDelta(double a, double b) { return std::remainder(b-a,360.0); }
inline float wrapAngle(float a) { return std::atan2(std::sin(a),std::cos(a)); }
inline double bearing(double latA, double lonA, double latB, double lonB) {
  const double a=latA*RAD,b=latB*RAD,d=longitudeDelta(lonA,lonB)*RAD;
  return std::atan2(std::sin(d)*std::cos(b),
      std::cos(a)*std::sin(b)-std::sin(a)*std::cos(b)*std::cos(d));
}
inline double distance(double latA, double lonA, double latB, double lonB) {
  const double a=latA*RAD,b=latB*RAD,d=longitudeDelta(lonA,lonB)*RAD;
  const double h=std::pow(std::sin((b-a)/2),2)+std::cos(a)*std::cos(b)*std::pow(std::sin(d/2),2);
  return 2*EARTH*std::asin(std::sqrt(h>1?1:h));
}
inline bool valid(Coordinate p) {
  return p.latE7>=-850000000 && p.latE7<=850000000 && p.lonE7>=-1800000000 && p.lonE7<=1800000000;
}
inline uint32_t checksum(const uint8_t *data, unsigned bytes) {
  uint32_t h=2166136261u;
  for (unsigned i=0;i<bytes;++i) h=(h^data[i])*16777619u;
  return h;
}
inline bool build(const Coordinate *input,unsigned size,GpxPoint *output) {
  if (size<2 || size>MAX_POINTS) return false;
  const double originLat=input[0].latE7*1e-7,originLon=input[0].lonE7*1e-7;
  double total=0;
  for (unsigned i=0;i<size;++i) {
    if (!valid(input[i])) return false;
    const double lat=input[i].latE7*1e-7,lon=input[i].lonE7*1e-7;
    if (i) {
      const double segment=distance(input[i-1].latE7*1e-7,input[i-1].lonE7*1e-7,lat,lon);
      if (segment<0.01 || segment>100000) return false;
      total+=segment;
    }
    if (total>2000000) return false;
    output[i]={float(longitudeDelta(originLon,lon)*RAD*EARTH*std::cos(originLat*RAD)),
               float((lat-originLat)*RAD*EARTH),float(total)};
  }
  return true;
}
inline unsigned segmentAt(float d) {
  unsigned lo=0,hi=count-1;
  while(lo+1<hi) { unsigned mid=(lo+hi)/2; if(points[mid].meters<=d) lo=mid; else hi=mid; }
  return lo;
}
inline GpxPoint sample(float d) {
  d=clamp(d,0,length());
  const unsigned i=segmentAt(d);
  const auto &a=points[i],&b=points[i+1];
  const float t=(d-a.meters)/(b.meters-a.meters);
  return {a.east+(b.east-a.east)*t,a.north+(b.north-a.north)*t,d};
}
inline float pointBearing(const GpxPoint &a,const GpxPoint &b) {
  return bearing(lat0+a.north/(EARTH*RAD),lon0+a.east/(EARTH*RAD*lonScale),
                 lat0+b.north/(EARTH*RAD),lon0+b.east/(EARTH*RAD*lonScale));
}
inline float pointDistance(const GpxPoint &a,const GpxPoint &b) {
  return distance(lat0+a.north/(EARTH*RAD),lon0+a.east/(EARTH*RAD*lonScale),
                  lat0+b.north/(EARTH*RAD),lon0+b.east/(EARTH*RAD*lonScale));
}
enum GuidanceMode { TO_START, ON_ROUTE, OFF_COURSE };
struct Guidance {
  bool joined=false, offCourse=false;
  void reset() { joined=false;offCourse=false; }
  void update(float distanceToStart,float offset) {
    if(!joined && distanceToStart<=30)joined=true;
    if(joined) {
      if(offset>50)offCourse=true;
      else if(offset<25)offCourse=false;
    }
  }
  GuidanceMode mode() const { return !joined ? TO_START : (offCourse ? OFF_COURSE : ON_ROUTE); }
};
inline float heading(float d) {
  const unsigned i=segmentAt(clamp(d,0,length()));
  return pointBearing(points[i],points[i+1]); // Ordered tangent, including start/finish.
}
struct ScreenVector { float x,y; };
inline ScreenVector rotate(float east,float north,float heading) {
  return {east*std::cos(heading)-north*std::sin(heading),
          -east*std::sin(heading)-north*std::cos(heading)};
}
struct Simulation {
  float meters=0; uint32_t previousMs=0; uint64_t elapsedMs=0; bool paused=false;
  void reset(uint32_t now) { meters=0; elapsedMs=0; previousMs=now; paused=false; }
  void tick(uint32_t now) {
    if (!paused && !finished()) { elapsedMs+=uint32_t(now-previousMs); meters=clamp(float(elapsedMs*0.001*SPEED_MPS),0,length()); }
    previousMs=now;
  }
  bool finished() const { return meters>=length(); }
};
struct Cue { float meters,angle; };
constexpr int PAGE_INDEX = 2;
struct TurnPreview {
  bool active=false;
  int returnPage=0;
  float handledThrough=-1;
  Cue cue={0,0};
  void reset() { active=false; returnPage=0; handledThrough=-1; cue={0,0}; }
  void dismiss(int &page) {
    if(!active)return;
    page=returnPage;active=false;
  }
  void update(float progress,Cue upcoming,bool eligible,int &page) {
    if(active) {
      if(progress>=cue.meters+10) dismiss(page);
      return;
    }
    const float remaining=upcoming.meters-progress;
    if(eligible && page!=PAGE_INDEX && upcoming.angle!=0 && upcoming.meters>handledThrough &&
       remaining>=0 && remaining<=160.9344f) {
      cue=upcoming;handledThrough=cue.meters+10;returnPage=page;page=PAGE_INDEX;active=true;
    }
  }
};
inline Cue nextCue(float d) {
  static const GpxPoint *cachedPoints=nullptr;
  static uint32_t cachedRevision=0;
  static float cachedFrom=-1;
  static Cue cached={0,0};
  if(cachedPoints==points && cachedRevision==revision && d>=cachedFrom && d+5<cached.meters)return cached;
  cachedPoints=points;cachedRevision=revision;cachedFrom=d;cached={length(),0};
  for(unsigned i=1;i+1<count;++i) {
    float at=points[i].meters;
    if(at<=d+5 || at<25 || at>length()-25) continue;
    const auto a=sample(at-25),b=sample(at),c=sample(at+25);
    float angle=wrapAngle(pointBearing(b,c)-pointBearing(a,b));
    if(std::fabs(angle)>=0.70f) {cached={at,angle};break;}
  }
  return cached;
}
inline float nearestDistance(float east,float north,float &offset,float previous=-1,float course=NAN) {
  float best=1e30f,result=0,bestOffset=0;
  for(unsigned i=1;i<count;++i) {
    const auto &a=points[i-1],&b=points[i];
    const float x=b.east-a.east,y=b.north-a.north;
    const float t=clamp(((east-a.east)*x+(north-a.north)*y)/(x*x+y*y),0,1);
    const float dx=east-a.east-t*x,dy=north-a.north-t*y;
    const float along=a.meters+t*(b.meters-a.meters);
    float square=dx*dx+dy*dy,score=square;
    // Prefer continuity and the direction of travel on overlapping/out-and-back legs.
    if(previous>=0) score+=std::pow(clamp(std::fabs(along-previous)-80,0,200),2);
    if(score>=best)continue;
    if(std::isfinite(course) && square<10000) score+=400*(1-std::cos(wrapAngle(pointBearing(a,b)-course)));
    if(score<best) { best=score;result=along;bestOffset=square; }
  }
  offset=std::sqrt(bestOffset); return result;
}
} // namespace gpx
