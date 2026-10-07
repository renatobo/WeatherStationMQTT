#pragma once
#include <Arduino.h>

// Diagnostic state is written only by the owning module.
extern uint32_t healthLastSampleAt, heapFree, heapLargestBlock;
extern uint32_t heapMinimum, heapMinimumLargestBlock;
extern uint8_t heapFragmentation;
void sampleSystemHealth();
