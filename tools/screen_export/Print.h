#pragma once
#include "Arduino.h"
class Print {public: virtual size_t write(uint8_t)=0; size_t print(const char*s){size_t n=0;while(*s)n+=write(*s++);return n;} size_t print(const String&s){return print(s.c_str());}};
