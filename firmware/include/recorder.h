#pragma once
#include "fusion.h"

constexpr uint32_t RECORD_DURATION_US=10000000;
constexpr size_t RECORD_CAPACITY=1100;
enum RecordPhase { RECORD_IDLE, RECORD_ACTIVE, RECORD_REVIEW, RECORD_CONFIRMED, RECORD_ERROR };
const char* const RECORD_LABELS[]={"not_attached","shaking","calm_breathing","fast_breathing"};
struct RecordSample {
  uint32_t timestampUs,sequence;
  Vec acceleration,gyro;
};
struct RecordInfo {
  RecordPhase phase=RECORD_IDLE;
  uint32_t startUs=0,elapsedUs=0;
  size_t count=0;
  unsigned int label=0;
  bool calibrated=false;
  float accelScale=1;
  char id[48]={0},filename[150]={0},error[96]={0};
};
// The sampler writes only while ACTIVE; completed data are immutable until
// explicitly discarded or replaced after confirmation. No browser polling loss.
struct Recorder {
  RecordInfo info;
  RecordSample samples[RECORD_CAPACITY];
  void capture(uint32_t now,bool valid,Vec a,Vec rawGyro,uint32_t seq) {
    if(info.phase!=RECORD_ACTIVE) return;
    if(int32_t(now-info.startUs)<0) return; // sample read just before Start
    info.elapsedUs=now-info.startUs;
    if(info.elapsedUs>=RECORD_DURATION_US) {info.elapsedUs=RECORD_DURATION_US;info.phase=RECORD_REVIEW;return;}
    if(!valid) {fail("Sensor read failed during recording. Please discard and retry.");return;}
    if(info.count>=RECORD_CAPACITY) {fail("Recording buffer filled unexpectedly. Please discard and retry.");return;}
    samples[info.count++]={info.elapsedUs,seq,a,rawGyro};
  }
  void fail(const char* reason) {info.phase=RECORD_ERROR;snprintf(info.error,sizeof(info.error),"%s",reason);}
};
