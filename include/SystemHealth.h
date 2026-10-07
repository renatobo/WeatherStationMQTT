#pragma once

// Project modifications by Renato Bonomini (renatobo); MIT, see LICENSE.
// Observed heap health shared by HTTP diagnostics and TLS guards.
// Only sampleSystemHealth() updates these values and their sampled minima.
#include <Arduino.h>

// Diagnostic state is written only by the owning module.
extern uint32_t healthLastSampleAt, heapFree, heapLargestBlock;
extern uint32_t heapMinimum, heapMinimumLargestBlock;
extern uint8_t heapFragmentation;
void sampleSystemHealth();
