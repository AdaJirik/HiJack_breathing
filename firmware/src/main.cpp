#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include "fusion.h"
#include "calibration.h"
#include "recorder.h"
#include "web_ui.h"

constexpr float G=9.80665f, DEG=0.01745329252f;
WebServer server(80);
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
struct Snapshot {
  Quat q; Vec a,gyro,linear;
  uint32_t seq=0,stamp=0,errors=0;
  bool ok=false,calibrated=false;
  int calibration=0; // 0 idle, 1 collecting, 2 success, -1 moved, -2 level, -3 invalid gravity
  int progress=0;
  float accelNoise=0,gyroNoise=0,accelScale=1;
} latest;
bool requested=false;
uint8_t address=0;
Recorder recorder;
uint32_t recordCounter=0;
RecordInfo recordInfo() {portENTER_CRITICAL(&mux);RecordInfo r=recorder.info;portEXIT_CRITICAL(&mux);return r;}
String recordJson() {
  RecordInfo r=recordInfo();char b[700];
  snprintf(b,sizeof(b),"{\"phase\":%d,\"count\":%u,\"elapsed_ms\":%lu,\"duration_ms\":10000,\"label\":\"%s\",\"id\":\"%s\",\"filename\":\"%s\",\"error\":\"%s\"}",
    r.phase,(unsigned)r.count,(unsigned long)(r.elapsedUs/1000),RECORD_LABELS[r.label],r.id,r.filename,r.error);
  return String(b);
}
String cleanFilename(String base) {
  base.trim();String lower=base;lower.toLowerCase();if(lower.endsWith(".csv"))base.remove(base.length()-4);
  String result;
  for(unsigned i=0;i<base.length();i++) {
    unsigned char c=base[i];
    if(c<32 || c==127 || c=='/' || c=='\\' || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|')result+='_';
    else result+=char(c);
  }
  return result.length()?result:String("measurement");
}
void setupRecorderRoutes() {
  server.on("/api/recording/start",HTTP_POST,[]{
    Snapshot s;portENTER_CRITICAL(&mux);s=latest;portEXIT_CRITICAL(&mux);
    if(!s.ok || millis()-s.stamp>500) {server.send(503,"application/json","{\"error\":\"Sensor unavailable\"}");return;}
    if(s.calibration==1) {server.send(409,"application/json","{\"error\":\"Wait for calibration to finish\"}");return;}
    int label=-1;for(int i=0;i<4;i++)if(server.arg("label")==RECORD_LABELS[i])label=i;
    if(label<0) {server.send(400,"application/json","{\"error\":\"Choose one of the four measurement types\"}");return;}
    RecordInfo next;next.phase=RECORD_ACTIVE;next.label=label;next.calibrated=s.calibrated;next.accelScale=s.accelScale;
    snprintf(next.id,sizeof(next.id),"%08lx-%08lx-%lu",(unsigned long)(ESP.getEfuseMac()&0xffffffff),(unsigned long)esp_random(),(unsigned long)++recordCounter);
    String filename=cleanFilename(server.arg("filename"))+"_"+RECORD_LABELS[label]+".csv";
    if(filename.length()>=sizeof(next.filename)) {server.send(400,"application/json","{\"error\":\"Please choose a shorter filename\"}");return;}
    snprintf(next.filename,sizeof(next.filename),"%s",filename.c_str());
    bool accepted=false;
    portENTER_CRITICAL(&mux);
    if(!requested && (recorder.info.phase==RECORD_IDLE||recorder.info.phase==RECORD_CONFIRMED)) {next.startUs=micros();recorder.info=next;accepted=true;}
    portEXIT_CRITICAL(&mux);
    if(!accepted) {server.send(409,"application/json","{\"error\":\"Confirm or discard the current measurement first\"}");return;}
    server.send(202,"application/json",recordJson());
  });
  server.on("/api/recording/status",HTTP_GET,[]{server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",recordJson());});
  server.on("/api/recording/discard",HTTP_POST,[]{
    String id=server.arg("id");
    bool accepted=false;
    portENTER_CRITICAL(&mux);
    if(id==recorder.info.id) {recorder.info=RecordInfo{};accepted=true;}
    portEXIT_CRITICAL(&mux);
    server.send(accepted?200:409,"application/json",accepted?"{\"discarded\":true}":"{\"error\":\"Measurement changed; refresh the page\"}");
  });
  server.on("/api/recording/confirm",HTTP_POST,[]{
    String id=server.arg("id");
    bool accepted=false;
    portENTER_CRITICAL(&mux);
    if(id==recorder.info.id && recorder.info.phase==RECORD_REVIEW && recorder.info.count>0) {recorder.info.phase=RECORD_CONFIRMED;accepted=true;}
    portEXIT_CRITICAL(&mux);
    server.send(accepted?200:409,"application/json",accepted?recordJson():String("{\"error\":\"Measurement is not ready for confirmation\"}"));
  });
  server.on("/api/recording/download",HTTP_GET,[]{
    RecordInfo r=recordInfo();
    if(r.phase!=RECORD_CONFIRMED || server.arg("id")!=r.id) {server.send(409,"text/plain","Confirm this measurement before downloading.");return;}
    server.sendHeader("Cache-Control","no-store");server.sendHeader("Content-Disposition",String("attachment; filename=\"")+r.filename+"\"");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);server.send(200,"text/csv; charset=utf-8","");
    server.sendContent("timestamp_us,ax_m_s2,ay_m_s2,az_m_s2,gx_deg_s,gy_deg_s,gz_deg_s,session_id,sample_index,device_sample_seq,calibrated,accel_scale\r\n");
    String chunk;chunk.reserve(2200);char row[256];
    for(size_t i=0;i<r.count;i++) {
      const RecordSample& s=recorder.samples[i];
      snprintf(row,sizeof(row),"%lu,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%s,%u,%lu,%d,%.6f\r\n",
        (unsigned long)s.timestampUs,s.acceleration.x,s.acceleration.y,s.acceleration.z,s.gyro.x,s.gyro.y,s.gyro.z,r.id,(unsigned)i,(unsigned long)s.sequence,r.calibrated?1:0,r.accelScale);
      chunk+=row;if(chunk.length()>1800) {server.sendContent(chunk);chunk="";delay(1);}
    }
    if(chunk.length())server.sendContent(chunk);server.sendContent("");
  });
}

bool writeReg(uint8_t reg,uint8_t val) {
  Wire.beginTransmission(address);Wire.write(reg);Wire.write(val);
  return Wire.endTransmission()==0;
}
bool readRegs(uint8_t reg,uint8_t* data,size_t count) {
  Wire.beginTransmission(address);Wire.write(reg);
  if(Wire.endTransmission(false)!=0) return false;
  if(Wire.requestFrom(address,(uint8_t)count)!=(int)count) return false;
  for(size_t i=0;i<count;i++) data[i]=Wire.read();
  return true;
}
bool initSensor() {
  for(uint8_t addr: {uint8_t(0x68),uint8_t(0x69)}) {
    address=addr;uint8_t id;
    if(readRegs(0x75,&id,1) && id==0x68) {
      if(!writeReg(0x6B,0x01)) continue; // awake, gyro clock
      delay(50);
      return writeReg(0x1A,3)&&writeReg(0x19,9)&&writeReg(0x1B,0)&&writeReg(0x1C,0);
    }
  }
  address=0;return false;
}
bool readSample(Vec& a,Vec& g) {
  uint8_t b[14]; if(!readRegs(0x3B,b,14)) return false;
  auto val=[&](int i){return (int16_t)((uint16_t(b[i])<<8)|b[i+1]);};
  a={val(0)*G/16384.f,val(2)*G/16384.f,val(4)*G/16384.f};
  g={val(8)*DEG/131.f,val(10)*DEG/131.f,val(12)*DEG/131.f};
  return true;
}
void sampling(void*) {
  Wire.begin(8,9);Wire.setClock(400000);Wire.setTimeOut(20);
  Quat orientation,reference;
  Vec bias,filtered;
  float accelScale=1;
  bool initialized=false,calibrated=false;
  int status=0;CalibrationWindow calibration;
  TickType_t wake=xTaskGetTickCount();uint32_t previous=micros(),retry=0;
  while(true) {
    vTaskDelayUntil(&wake,pdMS_TO_TICKS(10));
    if(!address && millis()-retry>=1000) {retry=millis();initSensor();initialized=false;calibrated=false;}
    Vec a,g;
    if(!address || !readSample(a,g)) {
      portENTER_CRITICAL(&mux);recorder.capture(micros(),false,{},{},0);latest.ok=false;latest.calibrated=false;if(address)latest.errors++;latest.calibration=0;latest.progress=0;portEXIT_CRITICAL(&mux);
      if(address) {address=0;status=0;calibration.reset();portENTER_CRITICAL(&mux);requested=false;portEXIT_CRITICAL(&mux);}
      previous=micros();continue;
    }
    uint32_t now=micros();float dt=(now-previous)*1e-6f;previous=now;
    if(dt>0.1f || dt<=0)dt=0.01f;
    if(!initialized) {orientation=Quat::fromGravity(a);reference=orientation;initialized=true;}
    bool start=false;
    portENTER_CRITICAL(&mux);start=requested;requested=false;portEXIT_CRITICAL(&mux);
    if(start) {status=1;calibration.reset();}
    if(status==1) {
      if(calibration.add(a,g)) {
        status=calibration.result();
        if(status==2) {
          bias=calibration.meanG;accelScale=G/calibration.meanA.norm();
          orientation=Quat::fromGravity(calibration.meanA);reference=orientation;
          filtered={};calibrated=true;
        }
      }
    }
    Vec corrected=a*accelScale;
    orientation.update(g-bias,corrected,dt);
    Vec world=orientation.rotate(corrected)-Vec{0,0,G};
    Vec linear=reference.inverse().rotate(world);
    filtered=filtered*0.8f+linear*0.2f;
    portENTER_CRITICAL(&mux);
    latest.q=reference.inverse()*orientation;latest.a=a;latest.gyro=(g-bias)*(1/DEG);latest.linear=filtered;
    latest.ok=true;latest.calibrated=calibrated;latest.stamp=millis();latest.seq++;
    latest.calibration=status;latest.progress=status==1?calibration.progress():0;
    latest.accelNoise=calibration.accelRms();latest.gyroNoise=calibration.gyroRms()/DEG;latest.accelScale=accelScale;
    recorder.capture(now,true,a,g*(1/DEG),latest.seq);
    portEXIT_CRITICAL(&mux);
  }
}
Snapshot snapshot() {portENTER_CRITICAL(&mux);Snapshot s=latest;portEXIT_CRITICAL(&mux);return s;}
String json() {
  Snapshot s=snapshot();char b[900];
  snprintf(b,sizeof(b),"{\"ok\":%s,\"calibrated\":%s,\"calibration\":%d,\"progress\":%d,\"seq\":%lu,\"age\":%lu,\"q\":[%.6f,%.6f,%.6f,%.6f],\"a\":[%.4f,%.4f,%.4f],\"gyro\":[%.3f,%.3f,%.3f],\"linear\":[%.4f,%.4f,%.4f],\"errors\":%lu,\"accelNoise\":%.4f,\"gyroNoise\":%.4f,\"accelScale\":%.5f}",
    s.ok?"true":"false",s.calibrated?"true":"false",s.calibration,s.progress,(unsigned long)s.seq,(unsigned long)(millis()-s.stamp),
    s.q.w,s.q.x,s.q.y,s.q.z,s.a.x,s.a.y,s.a.z,s.gyro.x,s.gyro.y,s.gyro.z,s.linear.x,s.linear.y,s.linear.z,(unsigned long)s.errors,s.accelNoise,s.gyroNoise,s.accelScale);
  String result(b);result.remove(result.length()-1);result+=",\"recording\":";result+=recordJson();result+='}';return result;
}
void setup() {
  Serial.begin(115200);
  xTaskCreatePinnedToCore(sampling,"imu",4096,nullptr,2,nullptr,1);
  WiFi.mode(WIFI_AP);WiFi.softAP("HiJack-Motion","hijack32");WiFi.setSleep(false);
  server.on("/",HTTP_GET,[]{server.send_P(200,"text/html; charset=utf-8",WEB_UI);});
  server.on("/api/state",HTTP_GET,[]{server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",json());});
  server.on("/api/calibrate",HTTP_POST,[]{
    if(recordInfo().phase==RECORD_ACTIVE) {server.send(409,"application/json","{\"error\":\"Wait for the 10-second recording to finish\"}");return;}
    Snapshot s=snapshot();
    if(!s.ok) {server.send(503,"application/json","{\"error\":\"Sensor not connected\"}");return;}
    if(s.calibration==1) {server.send(409,"application/json","{\"error\":\"Already calibrating\"}");return;}
    portENTER_CRITICAL(&mux);requested=true;portEXIT_CRITICAL(&mux);
    server.send(202,"application/json","{\"accepted\":true}");
  });
  setupRecorderRoutes();
  server.onNotFound([]{server.send(404,"text/plain","Not found");});server.begin();
  Serial.printf("\nHIJACK MOTION READY | Wi-Fi: HiJack-Motion | password: hijack32 | http://%s\n",WiFi.softAPIP().toString().c_str());
}
void loop() {
  server.handleClient();
  // On-demand development diagnostics; acquisition/fusion remain on the chip.
  if(Serial.available()) {
    char c=Serial.read();if(c=='?')Serial.println(json());
    if(c=='c' && snapshot().ok && recordInfo().phase!=RECORD_ACTIVE) {portENTER_CRITICAL(&mux);requested=true;portEXIT_CRITICAL(&mux);}
  }
  delay(1);
}
