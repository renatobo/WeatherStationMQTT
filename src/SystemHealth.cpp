// SPDX-License-Identifier: MIT
// Derived from the ThingPulse weather station; see LICENSE for copyright and attribution.
// Project modifications and modernization by Renato Bonomini (renatobo).
//
// Sampled heap diagnostics and weather allocation guards.
// Minima describe observed samples, not every allocator operation. Weather
// refreshes them around TLS work; the main loop also samples once per second.

#include "SystemHealth.h"

uint32_t healthLastSampleAt = 0, heapFree = 0, heapLargestBlock = 0;
uint32_t heapMinimum = UINT32_MAX, heapMinimumLargestBlock = UINT32_MAX;
uint8_t heapFragmentation = 0;

void sampleSystemHealth() {
  ESP.getHeapStats(&heapFree, &heapLargestBlock, &heapFragmentation);
  if (heapFree < heapMinimum) heapMinimum = heapFree;
  if (heapLargestBlock < heapMinimumLargestBlock) heapMinimumLargestBlock = heapLargestBlock;
  healthLastSampleAt = millis();
}
