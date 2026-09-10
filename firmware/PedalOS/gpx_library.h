#pragma once
#include <Arduino.h>
#include <new>
#include <BLECharacteristic.h>
#include <FFat.h>
#include "gpx_navigation.h"
#include "gpx_defaults.h"

constexpr char GPX_BLE_UUID[] = "8E400007-F315-4F60-9FB8-838830DAEA50";
#pragma pack(push,1)
struct GpxFileHeader {
  uint32_t magic=0x31585047, id=0;
  uint16_t count=0;
  uint32_t checksum=0;
  char name[129]={};
};
#pragma pack(pop)

class GpxLibrary : public BLECharacteristicCallbacks {
 public:
  static constexpr unsigned CAPACITY=40;
  GpxFileHeader routes[CAPACITY];
  unsigned routeCount=0;
  uint32_t selectedID=0;
  bool changed=false;
  void begin(BLECharacteristic *c,bool storage) {
    characteristic=c; storageReady=storage;
    queued=false; abortTransfer(); scan();
    characteristic->setCallbacks(this); status("idle");
  }
  void onWrite(BLECharacteristic *c) override {
    auto value=c->getValue();
    if(value.length()>sizeof(command) || !value.length()) { status("error:Invalid command"); return; }
    portENTER_CRITICAL(&mux);
    if(queued) { portEXIT_CRITICAL(&mux); return; }
    portEXIT_CRITICAL(&mux);
    status("busy");
    portENTER_CRITICAL(&mux);
    memcpy(command,value.c_str(),value.length()); commandBytes=value.length(); queued=true;
    portEXIT_CRITICAL(&mux);
  }
  void loop(bool canChange) {
    uint8_t data[200]; size_t bytes=0;
    portENTER_CRITICAL(&mux);
    if(queued) { bytes=commandBytes;memcpy(data,command,bytes);queued=false; }
    portEXIT_CRITICAL(&mux);
    if(!bytes) {
      if(staging && millis()-lastCommandMs>30000) { abortTransfer();status("error:Transfer timed out"); }
      return;
    }
    lastCommandMs=millis();
    if(data[0]==0) { inventory(); return; }
    if(!canChange) { abortTransfer();status("error:End the ride before syncing routes");return; }
    if(!storageReady) { status("error:Route storage unavailable");return; }
    if(data[0]==1) {
      abortTransfer();
      if(bytes!=12) { status("error:Invalid route header");return; }
      pending=GpxFileHeader{};
      memcpy(&pending.id,data+1,4);memcpy(&pending.count,data+5,2);memcpy(&pending.checksum,data+7,4);
      nameBytes=data[11]; receivedName=received=0;
      if(!pending.id || pending.count<2 || pending.count>gpx::MAX_POINTS || !nameBytes || nameBytes>128) {
        status("error:Route exceeds device limits");return;
      }
      staging=new(std::nothrow) gpx::Coordinate[pending.count];
      if(!staging) { status("error:Not enough memory");return; }
      status("ready:0");return;
    }
    if(data[0]==4 && staging) {
      if(bytes<3 || data[1]!=receivedName || receivedName+bytes-2>nameBytes) { fail("Filename sequence");return; }
      memcpy(pending.name+receivedName,data+2,bytes-2);receivedName+=bytes-2;
      status(String("name:")+receivedName);return;
    }
    if(data[0]==2 && staging) {
      uint16_t offset=0;if(bytes>=3)memcpy(&offset,data+1,2);
      if(bytes<11 || (bytes-3)%8 || offset!=received || received+(bytes-3)/8>pending.count) { fail("Point sequence");return; }
      memcpy(staging+received,data+3,bytes-3);received+=(bytes-3)/8;
      status(String("ready:")+received);return;
    }
    if(data[0]==3 && staging && bytes==1) {
      if(received!=pending.count || receivedName!=nameBytes ||
          gpx::checksum(reinterpret_cast<uint8_t*>(staging),received*8)!=pending.checksum) { fail("Incomplete route or checksum mismatch");return; }
      auto *geometry=new(std::nothrow) GpxPoint[pending.count];
      if(!geometry) { fail("Not enough memory");return; }
      bool valid=gpx::build(staging,pending.count,geometry);delete[] geometry;
      if(!valid) { fail("Invalid or discontinuous route");return; }
      char path[32];routePath(pending.id,path);
      if(FFat.exists(path)) { fail("Route ID already exists");return; }
      if(routeCount>=CAPACITY) { fail("Route storage is full");return; }
      File file=FFat.open("/gpx_transfer.tmp",FILE_WRITE);
      bool ok=file && file.write(reinterpret_cast<const uint8_t*>(&pending),sizeof(pending))==sizeof(pending) &&
          file.write(reinterpret_cast<uint8_t*>(staging),received*8)==received*8;
      if(file) { file.flush();file.close(); }
      if(ok) ok=FFat.rename("/gpx_transfer.tmp",path);
      uint32_t id=pending.id;
      abortTransfer();
      if(!ok) { FFat.remove("/gpx_transfer.tmp");status("error:Could not save route");return; }
      scan();status(String("saved:")+hex(id));return;
    }
    if((data[0]==5 || data[0]==7) && bytes==5) {
      uint32_t id;memcpy(&id,data+1,4);
      if(data[0]==7) { status(select(id)?String("selected:")+hex(id):"error:Could not open route");return; }
      char path[32];routePath(id,path);
      if(FFat.exists(path) && !FFat.remove(path)) { status("error:Could not delete route");return; }
      if(selectedID==id) { useExample();FFat.remove("/gpx_selected"); }
      scan();status(String("deleted:")+hex(id));return;
    }
    if(data[0]==8) { abortTransfer();status("idle");return; }
    status("error:Unexpected route command");
  }
  void scan() {
    routeCount=0;
    if(!storageReady)return;
    File root=FFat.open("/");
    for(File f=root.openNextFile();f;f=root.openNextFile()) {
      String path=f.name();
      if(path.indexOf("gpx_")>=0 && path.endsWith(".bin") && routeCount<CAPACITY) {
        GpxFileHeader h;
        if(f.read(reinterpret_cast<uint8_t*>(&h),sizeof(h))==sizeof(h) && h.magic==0x31585047 &&
            h.id && h.count>=2 && h.count<=gpx::MAX_POINTS && h.name[128]=='\0' &&
            f.size()==sizeof(h)+h.count*8) routes[routeCount++]=h;
      }
      f.close();
    }
    root.close();
    for(unsigned i=0;i<routeCount;++i)for(unsigned j=i+1;j<routeCount;++j)
      if(strcmp(routes[i].name,routes[j].name)>0) {auto tmp=routes[i];routes[i]=routes[j];routes[j]=tmp;}
  }
  void restore(bool storage) {
    storageReady=storage;
    if(!storage)return;
    seedDefaults();scan();
    uint32_t id=0;File f=FFat.open("/gpx_selected",FILE_READ);
    if(f) {f.read(reinterpret_cast<uint8_t*>(&id),4);f.close();}
    if(id && select(id,false))return;
    if(routeCount)select(routes[0].id,false);
  }
  bool select(uint32_t id,bool persist=true) {
    if(selectedID==id && owned)return true;
    char path[32];routePath(id,path);
    File f=FFat.open(path,FILE_READ);GpxFileHeader h;
    if(!f || f.read(reinterpret_cast<uint8_t*>(&h),sizeof(h))!=sizeof(h) || h.magic!=0x31585047 ||
        h.id!=id || h.count<2 || h.count>gpx::MAX_POINTS || h.name[128]!='\0') {if(f)f.close();return false;}
    auto *coordinates=new(std::nothrow) gpx::Coordinate[h.count];
    auto *geometry=new(std::nothrow) GpxPoint[h.count];
    bool ok=coordinates && geometry && f.read(reinterpret_cast<uint8_t*>(coordinates),h.count*8)==h.count*8 &&
        gpx::checksum(reinterpret_cast<uint8_t*>(coordinates),h.count*8)==h.checksum && gpx::build(coordinates,h.count,geometry);
    f.close();
    if(!ok) {delete[] coordinates;delete[] geometry;return false;}
    if(persist) {
      File selection=FFat.open("/gpx_selected",FILE_WRITE);
      if(!selection || selection.write(reinterpret_cast<const uint8_t*>(&id),4)!=4) {
        if(selection)selection.close();delete[] coordinates;delete[] geometry;return false;
      }
      selection.close();
    }
    gpx::lat0=coordinates[0].latE7*1e-7;gpx::lon0=coordinates[0].lonE7*1e-7;gpx::lonScale=cos(gpx::lat0*gpx::RAD);
    delete[] coordinates;delete[] owned;owned=geometry;gpx::points=owned;gpx::count=h.count;
    strncpy(activeName,h.name,sizeof(activeName)-1);selectedID=id;++gpx::revision;changed=true;return true;
  }
  void useExample() {
    gpx::points=GPX_POINTS;gpx::count=GPX_COUNT;gpx::lat0=GPX_LAT0;gpx::lon0=GPX_LON0;gpx::lonScale=GPX_LON_SCALE;
    delete[] owned;owned=nullptr;selectedID=0;strcpy(activeName,"Hill_Climb_Clinic_.gpx");++gpx::revision;changed=true;
  }
  void seedDefaults() {
    if(!storageReady || FFat.exists("/gpx_defaults_v1"))return;
    for(const auto &route:GPX_DEFAULTS) {
      char path[32];routePath(route.id,path);
      if(FFat.exists(path))continue;
      GpxFileHeader h;h.id=route.id;h.count=route.count;h.checksum=route.checksum;
      strncpy(h.name,route.name,sizeof(h.name)-1);
      File f=FFat.open("/gpx_seed.tmp",FILE_WRITE);
      const bool ok=f && f.write(reinterpret_cast<const uint8_t*>(&h),sizeof(h))==sizeof(h) &&
          f.write(reinterpret_cast<const uint8_t*>(route.points),route.count*8)==route.count*8;
      if(f){f.flush();f.close();}
      if(!ok || !FFat.rename("/gpx_seed.tmp",path))return;
    }
    File marker=FFat.open("/gpx_defaults_v1",FILE_WRITE);
    if(marker){marker.write(uint8_t(1));marker.close();}
  }
  const char *name() const { return activeName; }
 private:
  BLECharacteristic *characteristic=nullptr;
  bool storageReady=false;
  portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
  bool queued=false;size_t commandBytes=0;uint8_t command[200];
  gpx::Coordinate *staging=nullptr;GpxPoint *owned=nullptr;
  GpxFileHeader pending;
  uint16_t received=0;unsigned nameBytes=0,receivedName=0;uint32_t lastCommandMs=0;
  char activeName[129]="Hill_Climb_Clinic_.gpx";
  static void routePath(uint32_t id,char *out) {snprintf(out,32,"/gpx_%08lx.bin",(unsigned long)id);}
  static String hex(uint32_t id) {char out[9];snprintf(out,sizeof(out),"%08lx",(unsigned long)id);return String(out);}
  void status(const String &s) {
    if(characteristic)characteristic->setValue(s);
    if(Serial && Serial.availableForWrite()>160 && (s.startsWith("saved:") || s.startsWith("selected:") || s.startsWith("deleted:") || s.startsWith("error:")))
      Serial.println(String("GPX ")+s);
  }
  void abortTransfer() {delete[] staging;staging=nullptr;received=receivedName=0;}
  void fail(const char *message) {abortTransfer();status(String("error:")+message);}
  void inventory() {
    String result="routes:";
    for(unsigned i=0;i<routeCount;++i) {if(i)result+=",";result+=hex(routes[i].id);}
    status(result);
  }
};
