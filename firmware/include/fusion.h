#pragma once
#include <math.h>

struct Vec {
  float x=0, y=0, z=0;
  Vec operator+(Vec b) const { return {x+b.x,y+b.y,z+b.z}; }
  Vec operator-(Vec b) const { return {x-b.x,y-b.y,z-b.z}; }
  Vec operator*(float s) const { return {x*s,y*s,z*s}; }
  float norm() const { return sqrtf(x*x+y*y+z*z); }
};
struct Quat {
  float w=1,x=0,y=0,z=0;
  Quat operator*(Quat b) const {
    return {w*b.w-x*b.x-y*b.y-z*b.z,w*b.x+x*b.w+y*b.z-z*b.y,
      w*b.y-x*b.z+y*b.w+z*b.x,w*b.z+x*b.y-y*b.x+z*b.w};
  }
  Quat inverse() const { return {w,-x,-y,-z}; }
  void normalize() { float n=sqrtf(w*w+x*x+y*y+z*z); w/=n;x/=n;y/=n;z/=n; }
  Vec rotate(Vec a) const { Quat v=(*this)*Quat{0,a.x,a.y,a.z}*inverse(); return {v.x,v.y,v.z}; }
  static Quat fromGravity(Vec a) {
    float r=atan2f(a.y,a.z),p=atan2f(-a.x,sqrtf(a.y*a.y+a.z*a.z));
    float cr=cosf(r/2),sr=sinf(r/2),cp=cosf(p/2),sp=sinf(p/2);
    return {cr*cp,sr*cp,cr*sp,-sr*sp};
  }
  void update(Vec gyro, Vec accel, float dt) {
    float n=accel.norm();
    // Reject gravity correction during pronounced linear acceleration.
    if (fabsf(n-9.80665f)<1.2f) {
      Vec m=accel*(1/n), g=inverse().rotate({0,0,1});
      Vec error={m.y*g.z-m.z*g.y,m.z*g.x-m.x*g.z,m.x*g.y-m.y*g.x};
      gyro=gyro+error*1.5f;
    }
    Quat d=(*this)*Quat{0,gyro.x,gyro.y,gyro.z};
    w+=0.5f*d.w*dt;x+=0.5f*d.x*dt;y+=0.5f*d.y*dt;z+=0.5f*d.z*dt;
    normalize();
  }
};
