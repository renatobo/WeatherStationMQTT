/**The MIT License (MIT)

Copyright (c) 2018 by Daniel Eichhorn - ThingPulse

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See more at https://thingpulse.com
*/

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
