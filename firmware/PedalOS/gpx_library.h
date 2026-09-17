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
struct GpxThumbnailHeader {
  char magic[4];
  uint8_t version=0,width=0,height=0,paletteCount=0;
  uint32_t routeID=0,pixelBytes=0,payloadChecksum=0;
};
#pragma pack(pop)
static_assert(sizeof(GpxThumbnailHeader)==20,"Thumbnail file layout changed");

class GpxLibrary : public BLECharacteristicCallbacks {
 public:
  static constexpr unsigned CAPACITY=40;
  GpxFileHeader routes[CAPACITY];
  unsigned routeCount=0;
  uint32_t selectedID=0;
  bool changed=false;
  uint32_t thumbnailRevision=0;
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
      if((staging || thumbnailTransfer) && millis()-lastCommandMs>30000) { abortTransfer();status("error:Transfer timed out"); }
      return;
    }
    lastCommandMs=millis();
    if(data[0]==0) { inventory(); return; }
    if(data[0]==9 && bytes==5) {
      uint32_t id;memcpy(&id,data+1,4);
      uint32_t checksum=thumbnailChecksum(id);
      status(checksum ? String("thumb:")+hex(checksum) : "thumb:none");return;
    }
    if(!storageReady) { status("error:Route storage unavailable");return; }
    if(data[0]==12 && bytes==5) {
      uint32_t id;memcpy(&id,data+1,4);
      const GpxFileHeader *route=findRoute(id);
      if(!route){status("error:Route not found");return;}
      const uint8_t nameLength=strnlen(route->name,128);
      uint8_t reply[140];reply[0]=0x8c;
      memcpy(reply+1,&route->id,4);memcpy(reply+5,&route->count,2);
      memcpy(reply+7,&route->checksum,4);reply[11]=nameLength;
      memcpy(reply+12,route->name,nameLength);status(reply,12+nameLength);return;
    }
    if(data[0]==13 && bytes==7) {
      uint32_t id;uint16_t offset;memcpy(&id,data+1,4);memcpy(&offset,data+5,2);
      const GpxFileHeader *route=findRoute(id);
      if(!route || offset>=route->count){status("error:Invalid route download offset");return;}
      const uint8_t count=min(23u,unsigned(route->count-offset));
      char path[32];routePath(id,path);File file=FFat.open(path,FILE_READ);
      uint8_t reply[192];reply[0]=0x8d;memcpy(reply+1,&id,4);memcpy(reply+5,&offset,2);reply[7]=count;
      const size_t payload=count*8;
      const bool ok=file && file.seek(sizeof(GpxFileHeader)+offset*8) && file.read(reply+8,payload)==payload;
      if(file)file.close();
      if(!ok){status("error:Could not read route");return;}
      status(reply,8+payload);return;
    }
    if(!canChange) { abortTransfer();status("error:End the ride before syncing routes");return; }
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
    if(data[0]==6) {
      abortTransfer();
      if(bytes!=11) {status("error:Invalid preview header");return;}
      memcpy(&thumbnailID,data+1,4);memcpy(&thumbnailExpectedBytes,data+5,2);
      memcpy(&thumbnailExpectedChecksum,data+7,4);
      if(!thumbnailID || thumbnailExpectedBytes<sizeof(GpxThumbnailHeader) || thumbnailExpectedBytes>40000) {
        status("error:Invalid preview size");return;
      }
      char route[32];routePath(thumbnailID,route);
      if(!FFat.exists(route) || thumbnailExpectedBytes>FFat.totalBytes()-FFat.usedBytes()) {
        status("error:Route or preview storage unavailable");return;
      }
      FFat.remove("/gpx_thumbnail.tmp");
      thumbnailTransfer=FFat.open("/gpx_thumbnail.tmp",FILE_WRITE);thumbnailReceived=0;
      if(!thumbnailTransfer) {status("error:Could not create preview");return;}
      status("thumbready:0");return;
    }
    if(data[0]==10 && thumbnailTransfer) {
      uint16_t offset=0;if(bytes>=3)memcpy(&offset,data+1,2);
      const size_t payload=bytes>=3 ? bytes-3 : 0;
      if(!payload || offset!=thumbnailReceived || payload>thumbnailExpectedBytes-thumbnailReceived ||
         thumbnailTransfer.write(data+3,payload)!=payload) {fail("Preview chunk sequence");return;}
      thumbnailReceived+=payload;status(String("thumbready:")+thumbnailReceived);return;
    }
    if(data[0]==11 && bytes==1 && thumbnailTransfer) {
      thumbnailTransfer.flush();thumbnailTransfer.close();
      if(thumbnailReceived!=thumbnailExpectedBytes ||
         !validThumbnail("/gpx_thumbnail.tmp",thumbnailID,thumbnailExpectedChecksum)) {
        abortTransfer();status("error:Incomplete preview or checksum mismatch");return;
      }
      char path[32],backup[32];thumbnailPath(thumbnailID,path);thumbnailBackupPath(thumbnailID,backup);
      FFat.remove(backup);
      const bool hadOld=FFat.exists(path);
      bool ok=!hadOld || FFat.rename(path,backup);
      if(ok)ok=FFat.rename("/gpx_thumbnail.tmp",path);
      if(!ok && hadOld && !FFat.exists(path))FFat.rename(backup,path);
      if(ok)FFat.remove(backup);
      uint32_t id=thumbnailID;clearThumbnailTransfer();
      if(!ok) {FFat.remove("/gpx_thumbnail.tmp");status("error:Could not install preview");return;}
      ++thumbnailRevision;status(String("thumbsaved:")+hex(id));return;
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
      char thumbnail[32];thumbnailPath(id,thumbnail);FFat.remove(thumbnail);
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
  void restore(bool storage,const char *firmwareVersion) {
    storageReady=storage;
    if(!storage)return;
    seedDefaults(firmwareVersion);scan();
    uint32_t id=0;File f=FFat.open("/gpx_selected",FILE_READ);
    if(f) {f.read(reinterpret_cast<uint8_t*>(&id),4);f.close();}
    if(id && select(id,false)) {
      Serial.printf("GPX storage restored: %u routes, selected=%08lx\n",
                    routeCount, (unsigned long)selectedID);
      return;
    }
    if(routeCount)select(routes[0].id,false);
    Serial.printf("GPX storage restored: %u routes, selected=%08lx\n",
                  routeCount, (unsigned long)selectedID);
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
      selection.flush();
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
  void seedDefaults(const char *firmwareVersion) {
    uint32_t installRevision=updateFNV(2166136261UL,reinterpret_cast<const uint8_t*>(firmwareVersion),strlen(firmwareVersion));
    installRevision=updateFNV(installRevision,reinterpret_cast<const uint8_t*>(&GPX_DEFAULTS_REVISION),sizeof(GPX_DEFAULTS_REVISION));
    char marker[32];snprintf(marker,sizeof(marker),"/gpx_defaults_%08lx",(unsigned long)installRevision);
    if(!storageReady)return;
    const bool revisionAlreadyInstalled=FFat.exists(marker);
    for(const auto &route:GPX_DEFAULTS) {
      char path[32];routePath(route.id,path);
      if(!FFat.exists(path) && !revisionAlreadyInstalled) {
        GpxFileHeader h;h.id=route.id;h.count=route.count;h.checksum=route.checksum;
        strncpy(h.name,route.name,sizeof(h.name)-1);
        FFat.remove("/gpx_seed.tmp");File f=FFat.open("/gpx_seed.tmp",FILE_WRITE);
        const bool ok=f && f.write(reinterpret_cast<const uint8_t*>(&h),sizeof(h))==sizeof(h) &&
            f.write(reinterpret_cast<const uint8_t*>(route.points),route.count*8)==route.count*8;
        if(f){f.flush();f.close();}
        if(!ok || !FFat.rename("/gpx_seed.tmp",path))return;
      }
      // Once this release's bundle was installed, absence means the user
      // deliberately deleted that route. Do not recreate either file until a
      // later firmware/defaults revision performs a new additive merge.
      if(!FFat.exists(path))continue;
      char preview[32];thumbnailPath(route.id,preview);
      // Always repair a missing/corrupt bundled preview. A marker from an
      // earlier build of the same firmware version must not permanently hide
      // assets that were added later; valid phone-rendered previews are kept.
      if(!validThumbnail(preview,route.id)) {
        FFat.remove(preview);
        FFat.remove("/gpx_thumbnail.tmp");File f=FFat.open("/gpx_thumbnail.tmp",FILE_WRITE);
        const bool ok=f && f.write(route.thumbnail,route.thumbnailBytes)==route.thumbnailBytes;
        if(f){f.flush();f.close();}
        if(!ok || !validThumbnail("/gpx_thumbnail.tmp",route.id,route.thumbnailChecksum) ||
           !FFat.rename("/gpx_thumbnail.tmp",preview))return;
      }
    }
    if(revisionAlreadyInstalled)return;
    File done=FFat.open(marker,FILE_WRITE);
    if(done){done.write(uint8_t(1));done.close();}
  }
  const char *name() const { return activeName; }
  bool active() {
    bool commandQueued=false;
    portENTER_CRITICAL(&mux);
    commandQueued=queued;
    portEXIT_CRITICAL(&mux);
    return commandQueued || staging || bool(thumbnailTransfer);
  }
  bool openThumbnail(uint32_t id,File &file,GpxThumbnailHeader &header) const {
    char path[32];thumbnailPath(id,path);file=FFat.open(path,FILE_READ);
    if(!file || file.read(reinterpret_cast<uint8_t*>(&header),sizeof(header))!=sizeof(header) ||
       !validThumbnailHeader(header,id,file.size())) {if(file)file.close();return false;}
    return true;
  }
 private:
  BLECharacteristic *characteristic=nullptr;
  bool storageReady=false;
  portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
  bool queued=false;size_t commandBytes=0;uint8_t command[200];
  gpx::Coordinate *staging=nullptr;GpxPoint *owned=nullptr;
  GpxFileHeader pending;
  uint16_t received=0;unsigned nameBytes=0,receivedName=0;uint32_t lastCommandMs=0;
  char activeName[129]="Hill_Climb_Clinic_.gpx";
  File thumbnailTransfer;
  uint32_t thumbnailID=0,thumbnailExpectedChecksum=0;
  uint16_t thumbnailExpectedBytes=0,thumbnailReceived=0;
  static void routePath(uint32_t id,char *out) {snprintf(out,32,"/gpx_%08lx.bin",(unsigned long)id);}
  static void thumbnailPath(uint32_t id,char *out) {snprintf(out,32,"/gpx_%08lx.thm",(unsigned long)id);}
  static void thumbnailBackupPath(uint32_t id,char *out) {snprintf(out,32,"/gpx_%08lx.bak",(unsigned long)id);}
  static String hex(uint32_t id) {char out[9];snprintf(out,sizeof(out),"%08lx",(unsigned long)id);return String(out);}
  const GpxFileHeader *findRoute(uint32_t id) const {
    for(unsigned i=0;i<routeCount;++i)if(routes[i].id==id)return &routes[i];
    return nullptr;
  }
  void status(const String &s) {
    if(characteristic)characteristic->setValue(s);
    if(Serial && Serial.availableForWrite()>160 && (s.startsWith("saved:") || s.startsWith("thumbsaved:") || s.startsWith("selected:") || s.startsWith("deleted:") || s.startsWith("error:")))
      Serial.println(String("GPX ")+s);
  }
  void status(const uint8_t *data,size_t bytes) {
    if(characteristic)characteristic->setValue(data,bytes);
  }
  static uint32_t updateFNV(uint32_t value,const uint8_t *data,size_t bytes) {
    for(size_t i=0;i<bytes;++i)value=(value^data[i])*16777619UL;return value;
  }
  static bool validThumbnailHeader(const GpxThumbnailHeader &header,uint32_t id,size_t fileBytes) {
    if(memcmp(header.magic,"P1TH",4) || header.width!=160 || header.height!=96 ||
       header.routeID!=id)return false;
    size_t paletteBytes=0;
    if(header.version==1 && header.paletteCount==16 && header.pixelBytes==7680)paletteBytes=32;
    else if(header.version==2 && header.paletteCount==0 && header.pixelBytes==30720)paletteBytes=0;
    else return false;
    return fileBytes==sizeof(header)+paletteBytes+header.pixelBytes;
  }
  static bool validThumbnail(const char *path,uint32_t id,uint32_t wholeChecksum=0) {
    File file=FFat.open(path,FILE_READ);GpxThumbnailHeader header{};
    if(!file || file.read(reinterpret_cast<uint8_t*>(&header),sizeof(header))!=sizeof(header) ||
       !validThumbnailHeader(header,id,file.size())) {if(file)file.close();return false;}
    file.seek(0);uint8_t buffer[256];uint32_t whole=2166136261UL,payload=2166136261UL;size_t offset=0;
    while(file.available()) {
      const size_t count=file.read(buffer,min(sizeof(buffer),size_t(file.available())));
      if(!count)break;whole=updateFNV(whole,buffer,count);
      if(offset+count>sizeof(header)) {
        const size_t start=offset<sizeof(header) ? sizeof(header)-offset : 0;
        payload=updateFNV(payload,buffer+start,count-start);
      }
      offset+=count;
    }
    const size_t expected=sizeof(header)+(header.version==1 ? 32 : 0)+header.pixelBytes;
    file.close();return offset==expected && payload==header.payloadChecksum &&
        (!wholeChecksum || whole==wholeChecksum);
  }
  uint32_t thumbnailChecksum(uint32_t id) const {
    char path[32];thumbnailPath(id,path);
    if(!validThumbnail(path,id))return 0;
    File file=FFat.open(path,FILE_READ);uint8_t buffer[256];uint32_t value=2166136261UL;
    while(file && file.available()) {size_t n=file.read(buffer,min(sizeof(buffer),size_t(file.available())));if(!n)break;value=updateFNV(value,buffer,n);}
    if(file)file.close();return value;
  }
  void clearThumbnailTransfer() {
    if(thumbnailTransfer)thumbnailTransfer.close();thumbnailID=thumbnailExpectedChecksum=0;
    thumbnailExpectedBytes=thumbnailReceived=0;
  }
  void abortTransfer() {
    delete[] staging;staging=nullptr;received=receivedName=0;clearThumbnailTransfer();FFat.remove("/gpx_thumbnail.tmp");
  }
  void fail(const char *message) {abortTransfer();status(String("error:")+message);}
  void inventory() {
    String result="routes:";
    for(unsigned i=0;i<routeCount;++i) {if(i)result+=",";result+=hex(routes[i].id);}
    status(result);
  }
};
