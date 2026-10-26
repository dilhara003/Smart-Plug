#include "SmartPlugShared.h"
#include "SmartPlugTrustedRoots.h"
#include <WebSocketsClient.h>
#include <mbedtls/sha256.h>
#include <time.h>

static WiFiClientSecure cloudClient;
static HTTPClient cloudHttp;
static WebSocketsClient cloudSocket;
static String pendingAck, lastAck, lastCommandId;
static bool wakeRequested = true;
static bool socketStarted = false;
static unsigned long lastExchange = 0, lastPhoenixBeat = 0;
static String wakeTopic;

String statusPayload() {
  JsonDocument d;
  const unsigned long now = millis();
  d["power_state"] = actualPowerState ? "ON" : "OFF";
  d["mode"] = mode;
  d["human_detected"] = humanDetected;
  d["current_amps"] = currentAmps;
  d["apparent_va"] = estimatedPowerWatts;
  d["power_reason"] = powerReason;
  d["rssi"] = WiFi.RSSI();
  d["firmware_version"] = ERU_FIRMWARE_VERSION;
  d["uptime_seconds"] = now/1000;
  d["free_heap"] = ESP.getFreeHeap();
  d["human_on_seconds"] = humanOnSeconds;
  d["detection_delay_seconds"] = detectionDelaySeconds;
  d["max_on_enabled"] = maxOnEnabled;
  d["max_on_seconds"] = maxOnSeconds;
  d["plug_tripped"] = plugTripped;
  d["plug_remaining_seconds"] = actualPowerState && maxOnEnabled ? SafetyRules::remaining(now,powerOnStartTime,maxOnSeconds) : 0;
  d["iron_supported"] = bmi160Detected;
  d["bmi160_healthy"] = bmi160Healthy;
  d["iron_power_state"] = ironPowerState ? "ON" : "OFF";
  d["iron_mode"] = ironMode;
  d["iron_motion_detected"] = ironMotionDetected;
  d["iron_inactivity_seconds"] = ironInactivitySeconds;
  d["iron_max_on_enabled"] = ironMaxOnEnabled;
  d["iron_max_on_seconds"] = ironMaxOnSeconds;
  d["iron_fault"] = ironFault;
  d["iron_power_reason"] = ironPowerReason;
  d["iron_tripped"] = ironTripped;
  unsigned long remaining = ironPowerState ? SafetyRules::remaining(now,ironLastMotionTime,ironInactivitySeconds) : 0;
  if (ironPowerState && ironMaxOnEnabled) remaining = min(remaining,SafetyRules::remaining(now,ironPowerOnStartTime,ironMaxOnSeconds));
  d["iron_remaining_seconds"] = remaining;
  d["maintenance"] = maintenanceMode;
  d["ota_enabled"] = bool(ERU_ENABLE_LOCAL_OTA);
  // These fields remain local; sp_device_exchange whitelists cloud state.
  d["device_id"] = deviceID;
  d["wifi_connected"] = WiFi.status()==WL_CONNECTED;
  d["cloud_online"] = cloudOnline.load();
  d["cloud_realtime"] = cloudSocketOnline.load();
  d["ip_address"] = WiFi.status()==WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  String out; serializeJson(d,out); return out;
}
String buildPayload(bool full) { return statusPayload(); }

void applyCloudSnapshot() {
  CloudData c;
  if (xSemaphoreTake(cloudMutex,0)!=pdTRUE) return;
  if (!cloudIncomingReady) { xSemaphoreGive(cloudMutex); return; }
  c=cloudIncoming; cloudIncomingReady=false;
  xSemaphoreGive(cloudMutex);
  if (!c.valid || c.commandId.length()!=36) return;
  JsonDocument ack;
  if (c.commandId==lastCommandId && lastAck.length()) {
    if (xSemaphoreTake(cloudMutex,pdMS_TO_TICKS(2))==pdTRUE) { pendingAck=lastAck; outboxPending=true; xSemaphoreGive(cloudMutex); }
    return;
  }
  String reason;
  bool ok=false;
  JsonDocument args;
  if (!SafetyRules::commandFresh(millis(),c.receivedAt,c.ttlMs)) reason="Command expired before application";
  else if (deserializeJson(args,c.args) || !args.is<JsonObject>()) reason="Invalid command arguments";
  else if (c.target!="plug" && c.target!="iron") reason="Invalid output";
  else ok=executeCommand(c.target=="iron",c.action,args.as<JsonObject>(),reason);
  ack["id"]=c.commandId; ack["status"]=ok?"applied":"rejected"; ack["reason"]=reason;
  lastCommandId=c.commandId;
  lastAck=""; serializeJson(ack,lastAck);
  if (xSemaphoreTake(cloudMutex,pdMS_TO_TICKS(2))==pdTRUE) { pendingAck=lastAck; outboxPending=true; xSemaphoreGive(cloudMutex); }
  telemetryUrgent=true;
}

void processCloud() {
  if (!cloudMutex) return;
  applyCloudSnapshot();
  static unsigned long lastSnapshot=0;
  if (!telemetryUrgent && millis()-lastSnapshot<1000) return;
  lastSnapshot=millis();
  String state=statusPayload();
  if (xSemaphoreTake(cloudMutex,0)==pdTRUE) {
    outboxPayload=state;
    if (telemetryUrgent) outboxPending=true;
    telemetryUrgent=false;
    xSemaphoreGive(cloudMutex);
  }
}

int cloudRpc(const char* functionName,const String& body,String& response) {
  if (time(nullptr)<1700000000) return -10; // Wait for SNTP; never disable TLS verification.
  String url=String(SUPABASE_URL)+"/rest/v1/rpc/"+functionName;
  cloudHttp.setConnectTimeout(4000); cloudHttp.setTimeout(4000); cloudHttp.setReuse(true);
  if (!cloudHttp.begin(cloudClient,url)) return -1;
  cloudHttp.addHeader("apikey",SUPABASE_KEY);
  cloudHttp.addHeader("Content-Type","application/json");
  int code=cloudHttp.POST(body);
  if (code>0 && cloudHttp.getSize()<=16384) response=cloudHttp.getString();
  cloudHttp.end();
  if (code<0) cloudClient.stop();
  cloudOnline=code>=200 && code<300;
  return code;
}

bool cloudRegister() {
  JsonDocument d; d["p_device_id"]=deviceID; d["p_secret"]=deviceSecret;
  d["p_claim_code"]=claimCode; d["p_boot_id"]=bootID;
  String body,response; serializeJson(d,body);
  int code=cloudRpc("sp_register_device",body,response);
  JsonDocument reply;
  if (code!=200 || deserializeJson(reply,response) || reply["status"]!="ok") return false;
  deviceOwned=reply["owned"]|false;
  return true;
}

bool cloudFetch() {
  String state,ack;
  if (xSemaphoreTake(cloudMutex,pdMS_TO_TICKS(20))!=pdTRUE) return false;
  state=outboxPayload; ack=pendingAck; outboxPending=false;
  xSemaphoreGive(cloudMutex);
  if (state.length()==0) return false;
  JsonDocument d,s,a;
  if (deserializeJson(s,state)) return false;
  // The independent monitor can drop GPIOs while loop() is stalled. Never keep
  // reporting the cached pre-trip ON state as a fresh cloud heartbeat.
  if(controllerStallTrip.load()) {
    s["power_state"]="OFF"; s["iron_power_state"]="OFF";
    s["plug_tripped"]=true; s["iron_tripped"]=true; s["maintenance"]=true;
    s["power_reason"]="Control response timeout; restart required";
    s["iron_power_reason"]="Control response timeout; restart required";
  }
  d["p_device_id"]=deviceID; d["p_secret"]=deviceSecret; d["p_boot_id"]=bootID; d["p_state"]=s;
  if (ack.length() && !deserializeJson(a,ack)) d["p_ack"]=a;
  String body,response; serializeJson(d,body);
  // Use request start as the TTL base: network delays can only shorten validity.
  const uint32_t requestStart=millis();
  const int code=cloudRpc("sp_device_exchange",body,response);
  JsonDocument reply;
  if (code!=200 || deserializeJson(reply,response) || reply["status"]!="ok") {
    cloudOnline=false;
    if (xSemaphoreTake(cloudMutex,pdMS_TO_TICKS(20))==pdTRUE) { outboxPending=true; xSemaphoreGive(cloudMutex); }
    return false;
  }
  deviceOwned=reply["owned"]|false;
  if (xSemaphoreTake(cloudMutex,pdMS_TO_TICKS(20))==pdTRUE) {
    if (pendingAck==ack) pendingAck="";
    if (reply["command"].is<JsonObject>() && !cloudIncomingReady) {
      JsonObject command=reply["command"];
      CloudData c; c.valid=true;
      c.commandId=command["id"].as<String>(); c.target=command["target"].as<String>(); c.action=command["action"].as<String>();
      serializeJson(command["args"],c.args);
      c.receivedAt=requestStart; c.ttlMs=command["ttl_ms"]|0U;
      cloudIncoming=c; cloudIncomingReady=true;
    }
    xSemaphoreGive(cloudMutex);
  }
  return true;
}
bool cloudSend(const String& payload) { return cloudFetch(); }

static void socketEvent(WStype_t type,uint8_t* payload,size_t length) {
  if (type==WStype_CONNECTED) {
    JsonDocument d; d["topic"]="realtime:"+wakeTopic; d["event"]="phx_join"; d["ref"]="1";
    d["payload"]["config"]["broadcast"]["self"]=false;
    d["payload"]["config"]["private"]=false;
    String out; serializeJson(d,out); cloudSocket.sendTXT(out);
    lastPhoenixBeat=millis(); wakeRequested=true;
  } else if (type==WStype_DISCONNECTED) cloudSocketOnline=false;
  else if (type==WStype_TEXT && length<2048) {
    JsonDocument d; if (deserializeJson(d,payload,length)) return;
    String event=d["event"]|"";
    if (event=="phx_reply" && d["ref"]=="1") cloudSocketOnline=d["payload"]["status"]=="ok";
    else if (event=="broadcast" && d["payload"]["event"]=="wake") wakeRequested=true;
    else if (event=="phx_error" || event=="phx_close") { cloudSocketOnline=false; cloudSocket.disconnect(); }
  }
}

void cloudTask(void* param) {
  cloudClient.setCACert(SMARTPLUG_ROOT_CA);
  cloudClient.setHandshakeTimeout(8);
  unsigned long retryAt=0, backoff=1000, lastGoodExchange=0;
  uint8_t hash[32]; char hex[65];
  mbedtls_sha256((const unsigned char*)deviceSecret.c_str(),deviceSecret.length(),hash,0);
  for(int i=0;i<32;i++) sprintf(hex+i*2,"%02x",hash[i]);
  wakeTopic=String("sp:wake:")+hex;
  for (;;) {
    const unsigned long now=millis();
    if (WiFi.status()!=WL_CONNECTED || !isValidCloudConfig() || time(nullptr)<1700000000) {
      cloudOnline=false; cloudSocketOnline=false;
      vTaskDelay(pdMS_TO_TICKS(100)); continue;
    }
    if (!socketStarted) {
      String host=String(SUPABASE_URL); host.replace("https://","");
      const int slash=host.indexOf('/'); if(slash>=0) host=host.substring(0,slash);
      String path=String("/realtime/v1/websocket?apikey=")+SUPABASE_KEY+"&vsn=1.0.0";
      cloudSocket.beginSslWithCA(host.c_str(),443,path.c_str(),SMARTPLUG_ROOT_CA,"");
      cloudSocket.onEvent(socketEvent); cloudSocket.setReconnectInterval(5000);
      cloudSocket.enableHeartbeat(20000,5000,2); socketStarted=true;
    }
    cloudSocket.loop();
    if (cloudSocketOnline && now-lastPhoenixBeat>=25000) {
      cloudSocket.sendTXT("{\"topic\":\"phoenix\",\"event\":\"heartbeat\",\"payload\":{},\"ref\":\"hb\"}");
      lastPhoenixBeat=now;
    }
    if (lastGoodExchange && now-lastGoodExchange>45000) cloudOnline=false;
    if ((int32_t)(now-retryAt)>=0) {
      if (!cloudRegistered) {
        cloudRegistered=cloudRegister();
        if (!cloudRegistered) { retryAt=millis()+backoff; backoff=min(backoff*2,30000UL); }
        else { backoff=1000; wakeRequested=true; }
      }
      if (cloudRegistered) {
        bool push=false;
        if(xSemaphoreTake(cloudMutex,0)==pdTRUE) { push=outboxPending; xSemaphoreGive(cloudMutex); }
        unsigned long interval=cloudSocketOnline?10000UL:2000UL;
        // Rate-limit wake hints: notifications never carry executable commands.
        if ((wakeRequested || push || now-lastExchange>=interval) && now-lastExchange>=350) {
          wakeRequested=false; lastExchange=now;
          bool ok=cloudFetch();
          if(ok) { lastGoodExchange=millis(); backoff=1000; }
          else { retryAt=millis()+backoff; backoff=min(backoff*2,30000UL); }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(15));
  }
}
void startCloudTask() {
  cloudMutex=xSemaphoreCreateMutex();
  if(!cloudMutex) { Serial.println("[CLOUD] Mutex allocation failed; local mode remains available"); return; }
  processCloud();
  if(xTaskCreatePinnedToCore(cloudTask,"cloudTask",20480,NULL,1,NULL,0)!=pdPASS)
    Serial.println("[CLOUD] Task allocation failed; local mode remains available");
}
void printStatus() {
  Serial.println("[STATUS] "+statusPayload());
}
