#pragma once
#include "movement.h"
#include "four_state_model.h"

inline void fourFeatures(const MovementSample* samples,float* out) {
  double mean[6]={0},variance[6]={0};
  double meanMagnitude=0,magnitudeVariance=0,astep=0,gstep=0,grms=0;
  for(int i=0;i<FOUR_WINDOW;i++) {
    const Vec& a=samples[i].a;const Vec& g=samples[i].g;
    double values[]={a.x,a.y,a.z,g.x,g.y,g.z};
    for(int j=0;j<6;j++)mean[j]+=values[j];
    meanMagnitude+=sqrt(double(a.x)*a.x+double(a.y)*a.y+double(a.z)*a.z);
  }
  for(int j=0;j<6;j++)mean[j]/=FOUR_WINDOW;meanMagnitude/=FOUR_WINDOW;
  for(int i=0;i<FOUR_WINDOW;i++) {
    const Vec& a=samples[i].a;const Vec& g=samples[i].g;
    double values[]={a.x,a.y,a.z,g.x,g.y,g.z};
    for(int j=0;j<6;j++){double delta=values[j]-mean[j];variance[j]+=delta*delta;}
    double dm=sqrt(double(a.x)*a.x+double(a.y)*a.y+double(a.z)*a.z)-meanMagnitude;magnitudeVariance+=dm*dm;
    grms+=double(g.x)*g.x+double(g.y)*g.y+double(g.z)*g.z;
    if(i){Vec da=a-samples[i-1].a,dg=g-samples[i-1].g;astep+=double(da.x)*da.x+double(da.y)*da.y+double(da.z)*da.z;gstep+=double(dg.x)*dg.x+double(dg.y)*dg.y+double(dg.z)*dg.z;}
  }
  for(int j=0;j<6;j++)variance[j]/=FOUR_WINDOW;
  double base[]={variance[0]+variance[1]+variance[2],variance[3]+variance[4]+variance[5],magnitudeVariance/FOUR_WINDOW,astep/(FOUR_WINDOW-1),gstep/(FOUR_WINDOW-1),grms/FOUR_WINDOW};
  for(int j=0;j<6;j++){out[j]=float(log1p(sqrt(base[j])));out[6+j]=float(log1p(sqrt(variance[j])));}
  // Goertzel + Hann matches the low-frequency Fourier powers used in Python.
  // Double accumulation avoids cancellation at the slow breathing frequencies.
  static double hann[FOUR_WINDOW];static bool initialized=false;
  if(!initialized){for(int i=0;i<FOUR_WINDOW;i++)hann[i]=.5-.5*cos(2*3.14159265358979323846*i/(FOUR_WINDOW-1));initialized=true;}
  for(int kind=0;kind<2;kind++)for(int f=0;f<6;f++) {
    double coeff=2*cos(2*3.14159265358979323846*FOUR_FREQUENCIES[f]/100),energy=0;
    for(int axis=0;axis<3;axis++) {
      double q1=0,q2=0;
      for(int i=0;i<FOUR_WINDOW;i++) {
        const Vec& v=kind?samples[i].g:samples[i].a;
        double value=(axis==0?v.x:axis==1?v.y:v.z)-mean[kind*3+axis];
        double q=value*hann[i]+coeff*q1-q2;q2=q1;q1=q;
      }
      energy+=q1*q1+q2*q2-coeff*q1*q2;
    }
    out[12+kind*6+f]=float(log1p(fmax(0.,energy)/(FOUR_WINDOW*FOUR_WINDOW)));
  }
}

struct FourStateDetector {
  MovementSample samples[FOUR_WINDOW];int count=0,since=0,pending=-1,pendingCount=0;bool hasPrediction=false;
  MovementResult result;
  void reset(bool running){count=0;since=0;pending=-1;pendingCount=0;hasPrediction=false;result=MovementResult{};result.running=running;result.state=running?MOVEMENT_WARMING:MOVEMENT_STOPPED;}
  bool add(bool enabled,bool valid,Vec a,Vec g) {
    if(!enabled){if(result.running)reset(false);return false;}
    if(!result.running)reset(true);
    if(!valid){reset(true);result.state=MOVEMENT_FAULT;return false;}
    if(result.state==MOVEMENT_FAULT)result.state=MOVEMENT_WARMING;
    if(count<FOUR_WINDOW)samples[count++]={a,g};
    else {for(int i=1;i<FOUR_WINDOW;i++)samples[i-1]=samples[i];samples[FOUR_WINDOW-1]={a,g};}
    if(count<FOUR_WINDOW||(hasPrediction&&++since<FOUR_STRIDE))return false;
    return classify();
  }
  bool classify() {
    float features[FOUR_FEATURES],probabilities[4];fourFeatures(samples,features);fourProbabilities(features,probabilities);
    int winner=0;
    for(int i=0;i<4;i++){result.probabilities[i]=hasPrediction?.5f*result.probabilities[i]+.5f*probabilities[i]:probabilities[i];if(result.probabilities[i]>result.probabilities[winner])winner=i;}
    static const MovementState states[]={MOVEMENT_NOT_ATTACHED,MOVEMENT_SHAKING,MOVEMENT_CALM_BREATHING,MOVEMENT_FAST_BREATHING};
    if(!hasPrediction)result.state=states[winner];
    else if(result.state!=states[winner]) {
      if(pending==winner)pendingCount++;else {pending=winner;pendingCount=1;}
      if(pendingCount>=2){result.state=states[winner];pending=-1;pendingCount=0;}
    } else {pending=-1;pendingCount=0;}
    result.probability=result.probabilities[1];hasPrediction=true;since=0;return true;
  }
};

struct FourStateInput {
  MovementSample samples[FOUR_WINDOW];
  int count=0,write=0,since=0;
  bool enabled=false,valid=false;
  uint32_t generation=0,job=0;
  void feed(bool nextEnabled,bool nextValid,Vec a,Vec g) {
    if(nextEnabled!=enabled||nextValid!=valid){generation++;count=0;write=0;since=0;}
    enabled=nextEnabled;valid=nextValid;
    if(!enabled||!valid)return;
    samples[write]={a,g};write=(write+1)%FOUR_WINDOW;
    if(count<FOUR_WINDOW){count++;if(count==FOUR_WINDOW){job++;since=0;}}
    else if(++since>=FOUR_STRIDE){job++;since=0;}
  }
  void copyTo(MovementSample* destination) const {
    for(int i=0;i<FOUR_WINDOW;i++)destination[i]=samples[(write+i)%FOUR_WINDOW];
  }
};
