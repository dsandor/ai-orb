#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <string>
extern uint32_t g_millis;
inline uint32_t millis() { return g_millis; }
inline uint32_t micros() { return g_millis * 1000; }
inline bool psramFound() { return false; }
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
class String : public std::string {
 public:
  String() {}
  String(const char* s) : std::string(s ? s : "") {}
  String(const std::string& s) : std::string(s) {}
  String(int v) : std::string(std::to_string(v)) {}
  String(unsigned v) : std::string(std::to_string(v)) {}
  String(long v) : std::string(std::to_string(v)) {}
  String(unsigned long v) : std::string(std::to_string(v)) {}
  String operator+(const String& o) const { return String(std::string(*this) + std::string(o)); }
  String operator+(const char* o) const { return String(std::string(*this) + o); }
};
struct EspShim { uint32_t getFreeHeap() { return 201234; } uint32_t getFreePsram() { return 0; } };
extern EspShim ESP;
struct SerialShim { void printf(const char*, ...) {} void println(const char*) {} };
extern SerialShim Serial;
