#include "SmartPlugShared.h"
#include "KelaniWiFiCA.h"
#include "WiFiProvisioningRules.h"
#include <esp_eap_client.h>
#include <sys/time.h>

static std::atomic<int> lastDisconnectReason{0};
static void reply(JsonDocument& d) {
  Serial.print("ERU_WIFI_RESULT "); serializeJson(d,Serial); Serial.println();
}
static void errorReply(const String& message) {
  JsonDocument d; d["kind"]="error"; d["message"]=message; reply(d);
}
static void statusReply() {
  JsonDocument d; d["kind"]="status"; d["firmware"]=ERU_FIRMWARE_VERSION;
  d["enterprise_supported"]=true; d["connected"]=WiFi.status()==WL_CONNECTED;
  d["ssid"]=wifiSSID; d["auth"]=wifiAuthMode;
  d["ip"]=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String("");
  d["cloud_online"]=cloudOnline.load(); d["cloud_realtime"]=cloudSocketOnline.load();
  d["disconnect_reason"]=lastDisconnectReason.load(); reply(d);
}
bool beginSavedWiFi() {
  static bool eventsRegistered=false;
  if(!eventsRegistered) {
    WiFi.onEvent([](WiFiEvent_t,WiFiEventInfo_t info) {
      lastDisconnectReason=info.wifi_sta_disconnected.reason;
    },ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    eventsRegistered=true;
  }
  // Disable resets previous EAP configuration; every reconnect re-applies all
  // trust and identity parameters. No username/password is written to logs.
  esp_wifi_sta_enterprise_disable();
  if(wifiAuthMode!="PEAP") return WiFi.begin(wifiSSID.c_str(),wifiPassword.c_str())!=WL_CONNECT_FAILED;
  if(!WiFiProvisioningRules::validCredentials(true,wifiSSID.length(),wifiEapUsername.length(),wifiPassword.length())) {
    Serial.println("[WIFI] Missing enterprise credentials; use USB setup"); return false;
  }
  if(WiFiProvisioningRules::validEpoch(wifiClockEpoch) && time(nullptr)<(time_t)wifiClockEpoch) {
    timeval tv{(time_t)wifiClockEpoch,0}; settimeofday(&tv,nullptr);
  }
  if(time(nullptr)<1735689600) {
    Serial.println("[WIFI] Enterprise certificate validation needs a clock; run the USB setup helper"); return false;
  }
  esp_err_t settings[]={
    esp_eap_client_set_ca_cert((const unsigned char*)KELANI_WIFI_CA,strlen(KELANI_WIFI_CA)+1),
    esp_eap_client_set_disable_time_check(false),
    esp_eap_client_set_domain_name("kln.ac.lk"),
    esp_eap_client_set_eap_methods(ESP_EAP_TYPE_PEAP),
    esp_eap_client_set_identity((const unsigned char*)wifiEapUsername.c_str(),wifiEapUsername.length()),
    esp_eap_client_set_username((const unsigned char*)wifiEapUsername.c_str(),wifiEapUsername.length()),
    esp_eap_client_set_password((const unsigned char*)wifiPassword.c_str(),wifiPassword.length()),
  };
  for(esp_err_t code:settings) if(code!=ESP_OK) {
    Serial.printf("[WIFI] Enterprise setup failed: %s\n",esp_err_to_name(code)); return false;
  }
  if(esp_wifi_sta_enterprise_enable()!=ESP_OK) {
    Serial.println("[WIFI] Could not enable enterprise authentication"); return false;
  }
  Serial.println("[WIFI] PEAP / MSCHAPv2; university CA, server domain and validity checks enabled");
  WiFi.begin(wifiSSID.c_str());
  return true;
}

bool configureWiFiProfile(const String& ssid,const String& password,const String& auth,const String& username,uint64_t epoch,String& error) {
  bool enterprise=auth=="PEAP";
  if(auth!="PERSONAL" && !enterprise) { error="Unsupported Wi-Fi authentication"; return false; }
  if(!WiFiProvisioningRules::validCredentials(enterprise,ssid.length(),username.length(),password.length())) {
    error="Use a 1–32 byte SSID and a password of at most 64 bytes. Enterprise also requires a 1–64 byte username and nonempty password."; return false;
  }
  if(enterprise && !WiFiProvisioningRules::validEpoch(epoch)) {
    error="Device clock missing. Use the USB Wi-Fi helper or reload the setup page."; return false;
  }
  wifiPrefs.begin("wifi",false);
  wifiPrefs.putString("ssid",ssid); wifiPrefs.putString("password",password);
  wifiPrefs.putString("auth",auth); wifiPrefs.putString("eapUser",enterprise?username:String(""));
  if(WiFiProvisioningRules::validEpoch(epoch)) wifiPrefs.putULong64("clock",epoch);
  wifiPrefs.end();
  wifiSSID=ssid; wifiPassword=password; wifiAuthMode=auth; wifiEapUsername=enterprise?username:String("");
  wifiClockEpoch=epoch;
  return true;
}

static void handleSerialLine(const String& line) {
  if(line=="ERU_WIFI_STATUS") { statusReply(); return; }
  if(line=="ERU_WIFI_SCAN") {
    static bool started=false;
    int n=WiFi.scanComplete();
    JsonDocument d; d["kind"]="scan";
    if(n<0) {
      if(!started || n==WIFI_SCAN_FAILED) { WiFi.scanNetworks(true); started=true; }
      d["pending"]=true;
    } else {
      d["pending"]=false; JsonArray a=d["networks"].to<JsonArray>();
      for(int i=0;i<n && a.size()<8;i++) {
        String name=WiFi.SSID(i);
        if(!name.equalsIgnoreCase("Kelani_WIFI")) continue;
        bool duplicate=false; for(JsonObject entry:a) if(entry["ssid"].as<String>()==name) duplicate=true;
        if(!duplicate) { JsonObject entry=a.add<JsonObject>(); entry["ssid"]=name; entry["rssi"]=WiFi.RSSI(i); }
      }
    }
    reply(d); return;
  }
  if(!line.startsWith("ERU_WIFI_CONFIG ")) return;
  JsonDocument d;
  if(deserializeJson(d,line.substring(16)) || !d.is<JsonObject>()) { errorReply("Invalid setup JSON"); return; }
  String error;
  // This USB operation is physical commissioning authority. It never stores
  // university credentials in cloud telemetry, source files, or serial output.
  enterMaintenance("USB Wi-Fi configuration; outputs OFF");
  if(!configureWiFiProfile(d["ssid"]|"",d["password"]|"",d["auth"]|"PEAP",d["username"]|"",d["epoch"]|0ULL,error)) {
    errorReply(error); return;
  }
  JsonDocument ack; ack["kind"]="saved"; ack["ssid"]=wifiSSID; ack["auth"]=wifiAuthMode;
  reply(ack); Serial.flush(); delay(250); ESP.restart();
}
void processSerialWiFi() {
  static String line;
  static bool discard=false;
  int budget=256;
  while(Serial.available() && budget-->0) {
    char c=Serial.read();
    if(c=='\n') { if(!discard) handleSerialLine(line); line=""; discard=false; }
    else if(c!='\r' && !discard) {
      if(line.length()>=1536) { line=""; discard=true; errorReply("USB setup request too large"); }
      else line+=c;
    }
  }
  static uint32_t lastClockSave=0;
  if(wifiAuthMode=="PEAP" && WiFi.status()==WL_CONNECTED && millis()-lastClockSave>=3600000UL) {
    uint64_t now=time(nullptr);
    if(WiFiProvisioningRules::validEpoch(now) && now>wifiClockEpoch) {
      wifiPrefs.begin("wifi",false); wifiPrefs.putULong64("clock",now); wifiPrefs.end(); wifiClockEpoch=now;
    }
    lastClockSave=millis();
  }
}
