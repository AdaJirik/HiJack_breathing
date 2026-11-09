#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cassert>
#include "recorder.h"
#include "calibration.h"

static Recorder capture;
void reset(uint32_t start=123456) {
  capture.info=RecordInfo{};capture.info.phase=RECORD_ACTIVE;capture.info.startUs=start;
}
void tenSeconds(uint32_t start) {
  reset(start);
  capture.capture(start-1,true,{},{},0);
  assert(capture.info.count==0);
  for(uint32_t i=0;i<1000;i++)capture.capture(start+i*10000,true,{1,2,3},{4,5,6},i+100);
  assert(capture.info.phase==RECORD_ACTIVE&&capture.info.count==1000);
  capture.capture(start+10000000,true,{},{},1100);
  assert(capture.info.phase==RECORD_REVIEW&&capture.info.elapsedUs==10000000);
  assert(capture.info.count==1000);
  for(size_t i=0;i<capture.info.count;i++) {
    assert(capture.samples[i].timestampUs==i*10000);
    assert(capture.samples[i].sequence==i+100);
    assert(capture.samples[i].acceleration.z==3&&capture.samples[i].gyro.z==6);
  }
  capture.capture(start+11000000,true,{},{},1200);
  assert(capture.info.count==1000); // Pending capture must remain immutable.
}
int main() {
  tenSeconds(123456);tenSeconds(UINT32_MAX-50000); // micros() rollover
  reset();capture.capture(capture.info.startUs+1000,false,{},{},0);
  assert(capture.info.phase==RECORD_ERROR);
  reset();for(size_t i=0;i<=RECORD_CAPACITY;i++)capture.capture(capture.info.startUs+(uint32_t)i,true,{},{},0);
  assert(capture.info.phase==RECORD_ERROR&&capture.info.count==RECORD_CAPACITY);
  CalibrationWindow stationary,moving,upsideDown;
  for(int i=0;i<310;i++) {
    float noise=(i%2?1.f:-1.f);
    stationary.add({1.89f+noise*.015f,-.34f,8.93f},{-.0115f,.0173f,-.012f});
    moving.add({noise*2,0,9.8f},{0,0,0});
    upsideDown.add({0,0,-9.8f},{0,0,0});
  }
  assert(stationary.result()==2);assert(moving.result()==-1);assert(upsideDown.result()==-2);
  puts("Recorder: full-rate 10-second capture, timestamp rollover, immutable review, fault/overflow handling and calibration regression checks passed.");
}
