#pragma once
#include "fusion.h"
#include "movement_model.h"

enum MovementState : uint8_t { MOVEMENT_STOPPED=0,MOVEMENT_WARMING=1,MOVEMENT_NOT_ATTACHED=2,MOVEMENT_SHAKING=3,MOVEMENT_FAULT=4 };
struct MovementSample { Vec a,g; };
inline void movementFeatures(const MovementSample* samples,float* out) {
  Vec ma,mg;float magnitudeMean=0;
  for(int i=0;i<100;i++){ma=ma+samples[i].a;mg=mg+samples[i].g;magnitudeMean+=samples[i].a.norm();}
  ma=ma*.01f;mg=mg*.01f;magnitudeMean*=.01f;
  float av=0,gv=0,mv=0,astep=0,gstep=0,grms=0;
  for(int i=0;i<100;i++) {
    Vec da=samples[i].a-ma,dg=samples[i].g-mg;
    av+=da.x*da.x+da.y*da.y+da.z*da.z;gv+=dg.x*dg.x+dg.y*dg.y+dg.z*dg.z;
    float m=samples[i].a.norm()-magnitudeMean;mv+=m*m;
    grms+=samples[i].g.x*samples[i].g.x+samples[i].g.y*samples[i].g.y+samples[i].g.z*samples[i].g.z;
    if(i) {Vec a=samples[i].a-samples[i-1].a,g=samples[i].g-samples[i-1].g;astep+=a.x*a.x+a.y*a.y+a.z*a.z;gstep+=g.x*g.x+g.y*g.y+g.z*g.z;}
  }
  float values[]={av/100,gv/100,mv/100,astep/99,gstep/99,grms/100};
  for(int i=0;i<6;i++)out[i]=log1pf(sqrtf(fmaxf(0,values[i])));
}
struct MovementResult { MovementState state=MOVEMENT_STOPPED;bool running=false;float probability=.5f;uint32_t inferenceUs=0; };
struct MovementDetector {
  MovementSample samples[100];int count=0,since=0;bool hasPrediction=false;
  MovementResult result;
  void reset(bool running) {count=0;since=0;hasPrediction=false;result=MovementResult{};result.running=running;result.state=running?MOVEMENT_WARMING:MOVEMENT_STOPPED;}
  bool add(bool enabled,bool valid,Vec a,Vec g) {
    if(!enabled) {if(result.running)reset(false);return false;}
    if(!result.running)reset(true);
    if(!valid) {count=0;since=0;hasPrediction=false;result.state=MOVEMENT_FAULT;return false;}
    if(result.state==MOVEMENT_FAULT)result.state=MOVEMENT_WARMING;
    if(count<100)samples[count++]={a,g};
    else {for(int i=1;i<100;i++)samples[i-1]=samples[i];samples[99]={a,g};}
    if(count<100 || (hasPrediction&&++since<25))return false;
    float features[6];movementFeatures(samples,features);float p=movementProbability(features);
    result.probability=hasPrediction?.5f*result.probability+.5f*p:p;
    if(!hasPrediction)result.state=result.probability>=.5f?MOVEMENT_SHAKING:MOVEMENT_NOT_ATTACHED;
    else if(result.probability>=.6f)result.state=MOVEMENT_SHAKING;
    else if(result.probability<=.4f)result.state=MOVEMENT_NOT_ATTACHED;
    hasPrediction=true;since=0;return true;
  }
};
inline void movementColor(MovementState state,uint8_t& r,uint8_t& g,uint8_t& b) {
  r=state==MOVEMENT_SHAKING?255:0;g=0;
  b=(state==MOVEMENT_SHAKING||state==MOVEMENT_NOT_ATTACHED)?255:0;
}
