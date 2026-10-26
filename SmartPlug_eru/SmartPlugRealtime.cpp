#include "SmartPlugShared.h"
#include <WebSocketsServer.h>
static WebSocketsServer localSocket(81);
static bool authenticated[WEBSOCKETS_SERVER_CLIENT_MAX] = {};
static uint32_t connectedAt[WEBSOCKETS_SERVER_CLIENT_MAX] = {};
static bool started=false;
static void onLocalEvent(uint8_t num,WStype_t type,uint8_t* payload,size_t length) {
  if(num>=WEBSOCKETS_SERVER_CLIENT_MAX) return;
  if(type==WStype_CONNECTED) { authenticated[num]=false; connectedAt[num]=millis(); }
  else if(type==WStype_DISCONNECTED) authenticated[num]=false;
  else if(type==WStype_TEXT) {
    if(length>256) { localSocket.disconnect(num); return; }
    JsonDocument d;
    if(deserializeJson(d,payload,length) || d["type"]!="auth" || d["token"].as<String>()!=localSessionToken) { localSocket.disconnect(num); return; }
    authenticated[num]=true;
  }
}
void startLocalRealtime() {
  if(started) return;
  localSocket.begin(); localSocket.onEvent(onLocalEvent);
  localSocket.enableHeartbeat(15000,3000,2); started=true;
}
void processLocalRealtime() {
  if(!started) return;
  localSocket.loop();
  for(uint8_t i=0;i<WEBSOCKETS_SERVER_CLIENT_MAX;i++)
    if(localSocket.clientIsConnected(i) && !authenticated[i] && millis()-connectedAt[i]>5000) localSocket.disconnect(i);
  static uint32_t lastReport=0;
  if(millis()-lastReport<1000) return;
  lastReport=millis();
  String out="{\"type\":\"status\",\"state\":"+statusPayload()+"}";
  for(uint8_t i=0;i<WEBSOCKETS_SERVER_CLIENT_MAX;i++) if(authenticated[i]) localSocket.sendTXT(i,out);
}
void revokeLocalSessions() {
  localSessionToken=randomString(32,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
  if(started) localSocket.disconnect();
  for(bool &a:authenticated) a=false;
}
