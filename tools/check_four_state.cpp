#include <cstdint>
#include <cstdio>
#include <cassert>
#include <fstream>
#include <sstream>
#include <string>
#include "four_state.h"
static MovementSample samples[FOUR_WINDOW];
int main() {
  static FourStateInput input;
  for(int i=0;i<FOUR_WINDOW+50;i++)input.feed(true,true,{float(i),0,0},{});
  assert(input.count==FOUR_WINDOW&&input.job==2);input.copyTo(samples);
  assert(samples[0].a.x==50&&samples[FOUR_WINDOW-1].a.x==449);
  input.feed(false,true,{},{});assert(input.count==0);
  std::ifstream file("models/four_state_reference.csv");assert(file.good());std::string line;std::getline(file,line);
  int count=0,references=0;float expected[4];
  while(std::getline(file,line)) {
    std::stringstream row(line);std::string cell;float values[11];int n=0;
    while(std::getline(row,cell,',')){assert(n<11);values[n++]=std::stof(cell);}assert(n==11);
    for(int j=0;j<4;j++)expected[j]=values[j+1];
    samples[count++]={{values[5],values[6],values[7]},{values[8],values[9],values[10]}};
    if(count==FOUR_WINDOW) {
      float features[FOUR_FEATURES],probabilities[4];fourFeatures(samples,features);fourProbabilities(features,probabilities);
      for(int j=0;j<4;j++){printf("class %d: Python %.8f C++ %.8f\n",j,expected[j],probabilities[j]);assert(fabsf(probabilities[j]-expected[j])<.0001f);}
      FourStateDetector detector;
      for(int i=0;i<FOUR_WINDOW;i++)detector.add(true,true,samples[i].a,samples[i].g);
      for(int j=0;j<4;j++)assert(fabsf(detector.result.probabilities[j]-expected[j])<.0001f);
      detector.add(true,false,{},{});assert(detector.result.state==MOVEMENT_FAULT);
      detector.add(true,true,samples[0].a,samples[0].g);assert(detector.result.state==MOVEMENT_WARMING);
      detector.add(false,true,{},{});assert(detector.result.state==MOVEMENT_STOPPED);
      count=0;references++;
    }
  }
  assert(references==4&&count==0);uint8_t r,g,b;
  movementColor(MOVEMENT_CALM_BREATHING,r,g,b);assert(r==0&&g==255&&b==0);
  movementColor(MOVEMENT_FAST_BREATHING,r,g,b);assert(r==255&&g==0&&b==0);
  movementColor(MOVEMENT_NOT_ATTACHED,r,g,b);assert(r==0&&g==0&&b==255);
  movementColor(MOVEMENT_SHAKING,r,g,b);assert(r==255&&g==0&&b==255);
  puts("PASS: all four class probabilities match Python within 1e-4; all LED colors, warm-up and fault resets verified.");
}
