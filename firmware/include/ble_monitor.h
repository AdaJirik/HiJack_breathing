#pragma once
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <esp32-hal-rgb-led.h>
#include "movement.h"

constexpr const char* BLE_SERVICE_UUID="e40d1000-7c60-4c1a-8bbc-42a4a91131c0";
constexpr const char* BLE_STATE_UUID="e40d1001-7c60-4c1a-8bbc-42a4a91131c0";
constexpr const char* BLE_CONTROL_UUID="e40d1002-7c60-4c1a-8bbc-42a4a91131c0";
// Board in the supplied photo is the GPIO48 RGB variant. Override to 38 for
// newer DevKitC-1 revisions; never drive both pins without checking the board.
#ifndef MOTION_RGB_PIN
#define MOTION_RGB_PIN 48
#endif
extern portMUX_TYPE mux;
bool detectionRequested=false,bleConnected=false,advertiseAgain=false;
MovementResult movement;
BLECharacteristic* bleState=nullptr;
class MonitorServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {portENTER_CRITICAL(&mux);bleConnected=true;portEXIT_CRITICAL(&mux);}
  void onDisconnect(BLEServer*) override {portENTER_CRITICAL(&mux);bleConnected=false;advertiseAgain=true;portEXIT_CRITICAL(&mux);}
};
class MonitorControlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    std::string command=characteristic->getValue();
    if(command!="START"&&command!="STOP")return;
    portENTER_CRITICAL(&mux);detectionRequested=command=="START";portEXIT_CRITICAL(&mux);
  }
};
void setupBleMonitor() {
  BLEDevice::init("HiJack-Detect");
  auto server=BLEDevice::createServer();server->setCallbacks(new MonitorServerCallbacks());
  auto service=server->createService(BLE_SERVICE_UUID);
  bleState=service->createCharacteristic(BLE_STATE_UUID,BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_NOTIFY);
  bleState->addDescriptor(new BLE2902());
  auto control=service->createCharacteristic(BLE_CONTROL_UUID,BLECharacteristic::PROPERTY_WRITE);
  control->setCallbacks(new MonitorControlCallbacks());
  uint8_t initial[20]={1};bleState->setValue(initial,sizeof(initial));
  service->start();auto advertising=BLEDevice::getAdvertising();advertising->addServiceUUID(BLE_SERVICE_UUID);advertising->setScanResponse(true);
  BLEAdvertisementData response;response.setName("HiJack-Detect");advertising->setScanResponseData(response);
  BLEDevice::startAdvertising();
}
void updateBleMonitor() {
  static uint8_t previousState=255;static uint32_t lastNotification=0,sequence=0;
  MovementResult m;bool connected,restart;
  portENTER_CRITICAL(&mux);m=movement;connected=bleConnected;restart=advertiseAgain;advertiseAgain=false;portEXIT_CRITICAL(&mux);
  if(restart)BLEDevice::startAdvertising();
  uint8_t r,g,b;movementColor(m.state,r,g,b);
  bool changed=previousState!=m.state;
  if(changed) {neopixelWrite(MOTION_RGB_PIN,r/8,g/8,b/8);previousState=m.state;}
  if(changed||millis()-lastNotification>=200) {
    uint8_t packet[20]={1,uint8_t(m.state),r,g,b,uint8_t(m.probability*255+.5f),uint8_t(m.running),uint8_t(m.state!=MOVEMENT_FAULT)};
    uint32_t now=millis();++sequence;
    memcpy(packet+8,&sequence,4);memcpy(packet+12,&now,4);memcpy(packet+16,&m.inferenceUs,4);
    bleState->setValue(packet,sizeof(packet));if(connected)bleState->notify();lastNotification=now;
  }
}
