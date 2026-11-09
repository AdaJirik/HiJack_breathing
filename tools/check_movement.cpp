#include <cstdint>
#include <cstdio>
#include <cassert>
#include <fstream>
#include <sstream>
#include <string>
#include "movement.h"
int main() {
  std::ifstream file("models/movement_reference.csv");assert(file.good());std::string line;std::getline(file,line);
  MovementSample samples[100];int count=0,refs=0;float expected=0;
  while(std::getline(file,line)) {
    std::stringstream row(line);std::string cell;float values[8];int n=0;
    while(std::getline(row,cell,',')){assert(n<8);values[n++]=std::stof(cell);}assert(n==8);
    expected=values[1];samples[count++]={{values[2],values[3],values[4]},{values[5],values[6],values[7]}};
    if(count==100) {
      float f[6];movementFeatures(samples,f);float actual=movementProbability(f);
      assert(fabsf(actual-expected)<.00003f);
      MovementDetector detector;
      for(int i=0;i<100;i++)detector.add(true,true,samples[i].a,samples[i].g);
      assert(fabsf(detector.result.probability-expected)<.00003f);
      detector.add(true,false,{},{});assert(detector.result.state==MOVEMENT_FAULT);
      detector.add(false,true,{},{});assert(detector.result.state==MOVEMENT_STOPPED);
      count=0;refs++;
    }
  }
  assert(refs==3&&count==0);
  uint8_t r,g,b;movementColor(MOVEMENT_NOT_ATTACHED,r,g,b);assert(r==0&&g==0&&b==255);
  movementColor(MOVEMENT_SHAKING,r,g,b);assert(r==255&&g==0&&b==255);
  puts("Movement: exported C++ probabilities match Python within 3e-5 on held-out windows; detector reset/fault and LED mapping checks passed.");
}
