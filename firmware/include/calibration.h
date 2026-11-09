#pragma once
#include "fusion.h"

// Welford's algorithm measures motion around the mean, without confusing a
// constant sensor offset (which calibration is meant to remove) with motion.
struct CalibrationWindow {
  static constexpr int SETTLE_SAMPLES=60;
  static constexpr int SAMPLE_COUNT=250;
  static constexpr float MAX_ACCEL_RMS=0.35f; // m/s^2, combined XYZ variation
  static constexpr float MAX_GYRO_RMS=0.02618f; // 1.5 degrees/s
  int elapsed=0,count=0;
  Vec meanA,meanG;
  float m2a=0,m2g=0;
  void reset() { *this=CalibrationWindow{}; }
  bool add(Vec a,Vec g) {
    if(++elapsed<=SETTLE_SAMPLES) return false;
    ++count;
    Vec da=a-meanA,dg=g-meanG;
    meanA=meanA+da*(1.f/count);meanG=meanG+dg*(1.f/count);
    Vec da2=a-meanA,dg2=g-meanG;
    m2a+=da.x*da2.x+da.y*da2.y+da.z*da2.z;
    m2g+=dg.x*dg2.x+dg.y*dg2.y+dg.z*dg2.z;
    return count>=SAMPLE_COUNT;
  }
  float accelRms() const { return count>1?sqrtf(fmaxf(0,m2a/(count-1))):0; }
  float gyroRms() const { return count>1?sqrtf(fmaxf(0,m2g/(count-1))):0; }
  int result() const {
    float gravity=meanA.norm();
    if(accelRms()>MAX_ACCEL_RMS || gyroRms()>MAX_GYRO_RMS || meanG.norm()>0.2618f) return -1;
    // Broad plausibility bounds; do not reject ordinary gravity-scale error.
    if(gravity<6.5f || gravity>13.f) return -3;
    if(meanA.z<0.9063f*gravity) return -2; // within 25 degrees of face-up level
    return 2;
  }
  int progress() const { return elapsed*100/(SETTLE_SAMPLES+SAMPLE_COUNT); }
};
