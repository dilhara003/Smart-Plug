import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { execFileSync } from 'node:child_process';

test('saved enterprise Wi-Fi retries without interrupting authentication or manual setup',()=>{
  const dir=mkdtempSync(join(tmpdir(),'eru-wifi-recovery-'));
  try {
    const code=`#include <string>
#include <cstdint>
#include <cassert>
using String=std::string;
uint32_t tick=0,lastWiFiReconnectAttempt=0;
uint32_t millis(){return tick;}
const uint32_t WIFI_RECONNECT_INTERVAL=10000;
const int WL_CONNECTED=3,WIFI_LED_PIN=13,HIGH=1,LOW=0;
bool setupApActive=false,cloudOnline=true;
String wifiSSID="Kelani_WiFi",wifiAuthMode="PEAP";
int led=-1,attempts=0,disconnects=0;
void digitalWrite(int,int value){led=value;}
struct Log{void println(const char*){}} Serial;
struct Network{int state=0;int status(){return state;}void disconnect(){disconnects++;}} WiFi;
bool beginSavedWiFi(){attempts++;return true;}
${fn('SmartPlugWiFi.cpp','maintainWiFi').replaceAll('unsigned long','uint32_t')}
int main(){
  tick=10000;maintainWiFi();assert(attempts==0&&!cloudOnline);
  tick=89999;maintainWiFi();assert(attempts==0);
  tick=90000;maintainWiFi();assert(attempts==1&&disconnects==1);
  tick=100000;maintainWiFi();assert(attempts==1);
  tick=180000;maintainWiFi();assert(attempts==2);
  WiFi.state=WL_CONNECTED;tick=300000;maintainWiFi();assert(attempts==2&&led==HIGH);
  WiFi.state=0;setupApActive=true;maintainWiFi();assert(attempts==2);
  setupApActive=false;wifiSSID="";maintainWiFi();assert(attempts==2);
  wifiSSID="Personal network";wifiAuthMode="PERSONAL";lastWiFiReconnectAttempt=tick;
  tick+=9999;maintainWiFi();assert(attempts==2);
  tick++;maintainWiFi();assert(attempts==3);
  wifiAuthMode="PEAP";lastWiFiReconnectAttempt=UINT32_MAX-44999;
  tick=44999;maintainWiFi();assert(attempts==3);
  tick++;maintainWiFi();assert(attempts==4);
}`;
    writeFileSync(join(dir,'recovery.cpp'),code);
    execFileSync(process.env.CXX||'clang++',['-std=c++17',join(dir,'recovery.cpp'),'-o',join(dir,'recovery')],{stdio:'pipe'});
    execFileSync(join(dir,'recovery'),[],{stdio:'pipe'});
  }finally{rmSync(dir,{recursive:true,force:true});}
});

function fn(file, name) {
  const source=readFileSync(new URL('../SmartPlug_eru/'+file,import.meta.url),'utf8');
  const start=source.indexOf(`void ${name}(`)>=0?source.indexOf(`void ${name}(`):source.indexOf(`bool ${name}(`);
  assert.ok(start>=0, name+' missing');
  let end=source.indexOf('{',start)+1,depth=1;
  while(depth && end<source.length){if(source[end]==='{')depth++;else if(source[end]==='}')depth--;end++;}
  return source.slice(start,end);
}
test('actual firmware controller functions enforce boot, timer, sensor and rearm behavior',()=>{
  const dir=mkdtempSync(join(tmpdir(),'eru-controller-'));
  try {
    const code=`#include <string>
#include <cassert>
#include <cstdio>
#include <iostream>
#include <atomic>
#include "${resolve('SmartPlug_eru/SafetyRules.h')}"
using String=std::string;
uint32_t tick=10000; uint32_t millis(){return tick;}
int pins[40]={}; const int RELAY1_PIN=32,RELAY2_PIN=4,RELAY_ON=1,RELAY_OFF=0;
void digitalWrite(int pin,int value){pins[pin]=value;}
struct Log { template<class T> void print(T){} template<class T> void println(T){} template<class... T> void printf(const char*,T...){} } Serial;
void updateOutputIndicators(){} void beep(int,int){} void saveSettings(){} void flushSettings(){}
bool actualPowerState=false,plugTripped=false,maintenanceMode=false,plugOverrideActive=false,fallbackMode=false;
bool humanDetected=true,maxOnEnabled=true;
unsigned long powerOnStartTime=0,plugOverrideStart=0,lastHumanTime=0,humanOnSeconds=10,maxOnSeconds=2;
bool telemetryUrgent=false; String powerReason,mode="AUTO";
std::atomic<bool> controllerStallTrip{false};std::atomic<uint32_t> controllerHeartbeat{0};
bool bmi160Detected=true,bmi160Healthy=true,ironFault=false,ironPowerState=false,ironTripped=false,ironMotionDetected=true,ironMaxOnEnabled=true;
unsigned long ironPowerOnStartTime=0,ironLastMotionTime=0,ironInactivitySeconds=300,ironMaxOnSeconds=2;
String ironMode="AUTO",ironPowerReason;
${fn('SmartPlugHardware.cpp','setRelay')}
${fn('IronController.cpp','setIronPower')}
${fn('SmartPlugControl.cpp','processAutoMode')}
${fn('SmartPlugControl.cpp','processHybridMode')}
${fn('SmartPlugControl.cpp','processMaximumOnTime')}
${fn('SmartPlugControl.cpp','processSmartEngine')}
${fn('IronController.cpp','processIronController')}
${fn('SmartPlugControl.cpp','rearmOutput')}
${fn('SmartPlugControl.cpp','enterMaintenance')}
${fn('SmartPlugSafety.cpp','safetyMonitorTick')}
int main(){
  setRelay(false,"Safe startup OFF"); assert(!actualPowerState && pins[32]==0);
  setRelay(true,"Manual ON"); assert(actualPowerState && pins[32]==1 && powerOnStartTime==tick);
  tick+=2000; processMaximumOnTime(); assert(plugTripped && !actualPowerState && pins[32]==0);
  for(int i=0;i<50;i++){processSmartEngine();setRelay(true,"Remote ON");tick++;}
  assert(!actualPowerState && pins[32]==0); assert(rearmOutput(false)); assert(!actualPowerState && mode=="MANUAL");
  mode="HYBRID";plugOverrideActive=true;plugOverrideStart=tick;humanDetected=true;
  processHybridMode();assert(!actualPowerState);tick+=10000;processHybridMode();assert(actualPowerState);
  ironMode="AUTO";processIronController();assert(ironPowerState);tick+=2000;processIronController();assert(ironTripped&&!ironPowerState);
  for(int i=0;i<50;i++){processIronController();setIronPower(true,"Remote ON");tick++;}
  assert(!ironPowerState && pins[4]==0);assert(rearmOutput(true));assert(!ironPowerState&&ironMode=="OFF");
  ironMode="MANUAL";setIronPower(true,"Manual ON");bmi160Healthy=false;processIronController();assert(!ironPowerState&&ironTripped);assert(!rearmOutput(true));
  bmi160Healthy=true;ironMode="AUTO";processIronController();assert(!ironPowerState);assert(rearmOutput(true));
  enterMaintenance("Maintenance");setRelay(true,"Rejected ON");setIronPower(true,"Rejected ON");assert(!actualPowerState||pins[32]==0);assert(!ironPowerState);
  controllerHeartbeat=tick;tick+=1001;digitalWrite(32,1);digitalWrite(4,1);safetyMonitorTick();assert(controllerStallTrip&&pins[32]==0&&pins[4]==0);
  assert(SafetyRules::elapsed(1500U,UINT32_MAX-499U,2));
  assert(SafetyRules::commandFresh(1000U,0U,2000U));assert(!SafetyRules::commandFresh(2000U,0U,2000U));
  assert(!SafetyRules::commandFresh(100U,0U,0));assert(!SafetyRules::commandFresh(100U,0U,UINT32_MAX));
  std::cout<<"PASS: boot, plug/iron cutoff latch, AUTO retry, rearm, hybrid override, sensor recovery, maintenance and wraparound\\n";
}
`;
    writeFileSync(join(dir,'controller.cpp'),code);
    execFileSync(process.env.CXX||'clang++',['-std=c++17',join(dir,'controller.cpp'),'-o',join(dir,'controller')],{stdio:'pipe'});
    const output=execFileSync(join(dir,'controller'),{encoding:'utf8'});
    assert.match(output,/PASS: boot/);
  } finally { rmSync(dir,{recursive:true,force:true}); }
});
