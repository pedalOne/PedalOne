#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <algorithm>
#include <string>
#define isDigit isdigit
#define isAlpha isalpha
using std::min; using std::max;
#define PROGMEM
#define GFX_NOT_DEFINED -1
#define PI 3.14159265358979323846
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))
#define pgm_read_dword(p) (*(const uint32_t*)(p))
#define pgm_read_pointer(p) (*(void* const*)(p))
#define portENTER_CRITICAL(p)
#define portEXIT_CRITICAL(p)
#define F(s) s
class __FlashStringHelper;
#define MSB_16_SET(a,b) a=__builtin_bswap16(b)
template<class T> T constrain(T x,T a,T b){return min(max(x,a),b);}
inline uint32_t millis(){return 100000;}
inline void delay(int){}
class String:public std::string { public: int lastIndexOf(char c,int p)const{auto n=rfind(c,p);return n==npos?-1:int(n);} void replace(char a,char b){std::replace(begin(),end(),a,b);} using std::string::string; String(std::string s):std::string(s){} String():std::string(){} int indexOf(const char*s,int p=0)const {auto n=find(s,p);return n==npos?-1:int(n);} int indexOf(char s,int p=0)const {auto n=find(s,p);return n==npos?-1:int(n);} String substring(int a,int b=99999)const{return substr(a,min(int(size()),b)-a);} void trim(){auto a=find_first_not_of(" \r\n\t");*this=a==npos?"":substr(a,find_last_not_of(" \r\n\t")-a+1);} void toLowerCase(){for(auto &c:*this)c=tolower(c);} bool startsWith(const char*s)const{return rfind(s,0)==0;} bool endsWith(const char*s)const{return size()>=strlen(s)&&compare(size()-strlen(s),strlen(s),s)==0;} void remove(int a,int n=99999){erase(a,n);} void replace(const char*a,const char*b){size_t p=0;while((p=find(a,p))!=npos){std::string::replace(p,strlen(a),b);p+=strlen(b);}}};

#define DEG_TO_RAD (PI/180.0)
#define ps_malloc malloc
