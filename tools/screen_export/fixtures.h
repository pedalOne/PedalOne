struct {bool availableFor(String){return false;} int width(){return 0;} int height(){return 0;} uint16_t*pixels(){return nullptr;}} deviceIcon;
struct {int satellites(){return 12;}} onboardGps;
struct {bool verified=false; bool rebootPending(){return verified;} int progressPercent(){return 65;}} otaUpdate;
struct AncsClient {enum {Ready,Connected};struct Diagnostics{int state=Ready,rawEvents=0,filteredEvents=0,drops=0,timeouts=0,retries=0,strayData=0,parseErrors=0,queueDepth=0,maxQueueDepth=0,requestActive=0,lastGoogleLatencyMs=0,mtu=185,intervalUnits=12;};Diagnostics diagnostics(){return {};}} notifications;


class HostCanvas:public Arduino_GFX {public: std::vector<uint16_t> pixels; HostCanvas():Arduino_GFX(466,466),pixels(466*466){} bool begin(int32_t=0) override{return true;} void writePixelPreclipped(int16_t x,int16_t y,uint16_t c) override{if(x>=0&&y>=0&&x<466&&y<466)pixels[x*466+465-y]=c;} void drawPixel(int16_t x,int16_t y,uint16_t c){writePixelPreclipped(x,y,c);} void flush(){} uint16_t*getFramebuffer(){return pixels.data();}} host;
HostCanvas*canvas=&host;
void save(const char*name){std::ofstream f(std::string(name)+".ppm",std::ios::binary);f<<"P6\n466 466\n255\n";for(int y=0;y<466;++y)for(int x=0;x<466;++x){auto c=host.pixels[x*466+465-y];unsigned char rgb[]={uint8_t(((c>>11)&31)*255/31),uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31)};f.write((char*)rgb,3);}}

constexpr float FREE_RIDE_MAP_SCALES[]={0.32f,0.65f,1.35f};
struct File {operator bool(){return false;}size_t read(uint8_t*,size_t){return 0;}void close(){}};
struct GpxThumbnailHeader {uint8_t version=0;};
struct {unsigned routeCount=0,thumbnailRevision=0;struct {char name[129]="Hill Climb Clinic";uint32_t id=1;} routes[1];bool openThumbnail(uint32_t,File&,GpxThumbnailHeader&){return false;}} gpxLibrary;
struct SharedMapTileEntry {uint32_t payloadLength;};
struct SharedVectorTileHeader {char magic[4];uint16_t version,lineCount;};
struct SharedVectorLineHeader {uint8_t kind,reserved;uint16_t pointCount;};
struct SharedVectorPoint {uint16_t x,y;};
struct {uint32_t fingerprint(){return 0;}uint32_t revision(){return 0;}bool valid(){return false;}int zoom(){return 14;}bool openTile(uint32_t,uint32_t,File&,SharedMapTileEntry&){return false;}} sharedMap;
struct RiderNetwork {static constexpr int MAX_RIDERS=20;struct Rider {uint32_t id=0;char callSign[5]={};int32_t latitudeE7=0,longitudeE7=0;uint32_t lastSeenMs=0;int8_t rssi=0;};size_t snapshot(Rider*,size_t,uint32_t){return 0;}} riderNetwork;
