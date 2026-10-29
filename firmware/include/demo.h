#pragma once
// Demo mode: scripted mock sessions for showing off every screen (see src/demo.cpp).
#include <stdint.h>

#ifndef ORB_DEMO
#define ORB_DEMO 0
#endif

#if ORB_DEMO
void demoBegin();
void demoTick(uint32_t now);
#endif
