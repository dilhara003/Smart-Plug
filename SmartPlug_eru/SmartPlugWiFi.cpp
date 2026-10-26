#include "SmartPlugShared.h"
#include <Update.h>
#include "SmartPlugLocalPage.h"

static Preferences webAuthPrefs;
static String activeWebUsername;
static String activeWebPassword;
static bool webAuthLoaded = false;
static bool otaAuthorized = false;
static bool otaCompleted = false;

static void loadWebAuth() {
  if (webAuthLoaded) return;
  webAuthPrefs.begin("webauth", true);
  activeWebUsername = webAuthPrefs.getString("username", LOCAL_WEB_USERNAME);
  activeWebPassword = webAuthPrefs.getString("password", setupApPassword);
  webAuthPrefs.end();
  if (activeWebUsername.length() == 0) activeWebUsername = "admin";
  if (activeWebPassword.length() < 8) activeWebPassword = setupApPassword;
  webAuthLoaded = true;
}

bool connectWiFi() {

  if (wifiSSID.length() == 0) {
    Serial.println("[WIFI] No saved WiFi");
    return false;
  }

  Serial.println();
  Serial.println("[WIFI] Connecting...");
  Serial.print("[WIFI] SSID: ");
  Serial.println(wifiSSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);     // modem sleep adds 100-300 ms latency
  lastWiFiReconnectAttempt = millis();
  if(!beginSavedWiFi()) return false;

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < (wifiAuthMode=="PEAP"?60000UL:WIFI_CONNECT_TIMEOUT)) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("[WIFI] CONNECTED");
    Serial.print("[WIFI] IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WIFI] RSSI: ");
    Serial.println(WiFi.RSSI());

    digitalWrite(WIFI_LED_PIN, HIGH);
    return true;
  }

  Serial.println("[WIFI] Connection failed");
  digitalWrite(WIFI_LED_PIN, LOW);
  return false;
}

void maintainWiFi() {

  // Do not fight the setup hotspot while the user is entering a new password.
  if (setupApActive) return;

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(WIFI_LED_PIN, HIGH);
    return;
  }

  digitalWrite(WIFI_LED_PIN, LOW);
  cloudOnline = false;

  // With no saved SSID, remain offline until the user opens setup mode.
  if (wifiSSID.length() == 0) return;

  unsigned long now = millis();

  // PEAP can take considerably longer than personal Wi-Fi. Restarting it
  // every 10 seconds can repeatedly interrupt the authentication handshake.
  const unsigned long retryInterval = wifiAuthMode == "PEAP" ? 90000UL : WIFI_RECONNECT_INTERVAL;
  if (now - lastWiFiReconnectAttempt < retryInterval) {
    return;
  }

  lastWiFiReconnectAttempt = now;

  Serial.println("[WIFI] Reconnecting...");

  WiFi.disconnect();
  beginSavedWiFi();
}

bool requireWebAuth() {
  loadWebAuth();
  if (!setupServer.authenticate(activeWebUsername.c_str(), activeWebPassword.c_str())) {
    setupServer.requestAuthentication(); return false;
  }
  if (setupServer.method()!=HTTP_GET) {
    String token=setupServer.header("X-CSRF-Token");
    if(token.length()==0) token=setupServer.arg("csrf");
    if(token!=localSessionToken) { setupServer.send(403,"text/plain","Session expired. Reload the local dashboard."); return false; }
  }
  return true;
}
static String escapeHtml(String value) {
  value.replace("&","&amp;"); value.replace("<","&lt;"); value.replace(">","&gt;");
  value.replace("\"","&quot;"); value.replace("'","&#39;"); return value;
}
String protectHtml(String html) {
  int from=0;
  while((from=html.indexOf("<form",from))>=0) {
    int end=html.indexOf('>',from); if(end<0) break;
    String token="<input type='hidden' name='csrf' value='"+localSessionToken+"'>";
    html=html.substring(0,end+1)+token+html.substring(end+1);
    from=end+1+token.length();
  }
  return html;
}

String htmlHeader() {

  return
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Smart Plug Setup</title>"
    "<style>"
    "body{font-family:Arial;padding:25px;background:#f4f4f4}"
    "div{max-width:500px;margin:auto;background:white;padding:25px;border-radius:12px}"
    "input,button,select{width:100%;padding:12px;margin:7px 0;box-sizing:border-box}"
    "button{background:#222;color:white;border:0;border-radius:6px}"
    "</style>"
    "</head>"
    "<body>"
    "<div>";
}

String htmlFooter() {
  return "</div></body></html>";
}

void handleSetupRoot() {

  if (!requireWebAuth()) return;

  // Scan asynchronously so controller timing does not depend on radio scanning.
  int n = WiFi.scanComplete();
  if(n==WIFI_SCAN_FAILED) { WiFi.scanNetworks(true); n=0; }
  if(n==WIFI_SCAN_RUNNING) n=0;

  String html = htmlHeader();

  html += "<h2>Smart Plug Setup</h2>";
  html += "<p>Device ID:</p>";
  html += "<b>" + deviceID + "</b>";
  html += "<form action='/connect' method='POST'>";
  html += "<p>Select Wi-Fi</p>";
  html += "<select name='ssid'>";

  for (int i = 0; i < n; i++) {
    html += "<option value='" + escapeHtml(WiFi.SSID(i)) + "'>";
    html += escapeHtml(WiFi.SSID(i));
    html += " (" + String(WiFi.RSSI(i)) + " dBm)";
    html += "</option>";
  }

  html += "</select><p>Scanning is asynchronous. Refresh in a few seconds if the list is empty.</p>";
  html += "<p>Network security</p><select name='auth'><option value='PERSONAL'>Personal / shared password</option><option value='PEAP'>Kelani Wi-Fi: PEAP + MSCHAPv2</option></select>";
  html += "<p>University username (PEAP only)</p><input name='eapUser' maxlength='64' autocomplete='username' placeholder='Your Kelani Net ID'>";
  html += "<p>Password</p>";
  html += "<input type='password' name='password' placeholder='Wi-Fi password'>";
  html += "<input id='clockEpoch' type='hidden' name='epoch'><script>document.getElementById('clockEpoch').value=Math.floor(Date.now()/1000)</script>";
  html += "<p>PEAP verifies the university CA and kln.ac.lk server domain. Enter the Net ID you normally use; no credentials go to Supabase.</p>";
  html += "<button type='submit'>CONNECT</button>";
  html += "</form>";
  html += "<p><a href='/wifi'>Refresh Wi-Fi list</a></p>";
  html += "<form action='/factory-reset' method='post'><button>Reset Wi-Fi and settings (keeps identity and local login)</button></form>";
  html += htmlFooter();

  setupServer.send(200, "text/html", protectHtml(html));
}

void handleWiFiPage() {
  handleSetupRoot();
}

void handleWiFiConnect() {

  if (!requireWebAuth()) return;

  String ssid = setupServer.arg("ssid");
  String password = setupServer.arg("password");

  if (ssid.length() == 0) {
    setupServer.send(400, "text/plain", "SSID required");
    return;
  }

  enterMaintenance("Wi-Fi configuration");
  String error;
  String auth=setupServer.arg("auth"); if(auth.length()==0) auth="PERSONAL";
  uint64_t epoch=strtoull(setupServer.arg("epoch").c_str(),nullptr,10);
  if(!configureWiFiProfile(ssid,password,auth,setupServer.arg("eapUser"),epoch,error)) {
    setupServer.send(400,"text/plain",error); return;
  }
  setupServer.send(200,"text/html","<h2>Wi-Fi saved.</h2><p>Restarting with both outputs OFF. If connection fails, the setup network returns automatically.</p>");
  delay(500);
  ESP.restart();

}

void handleFactoryReset() {

  if (!requireWebAuth()) return;

  // Allowed only while the plug is in its setup AP mode.
  if (WiFi.getMode() != WIFI_AP) {
    setupServer.send(403, "text/plain", "Factory reset is available only in setup mode.");
    return;
  }

  enterMaintenance("Reset Wi-Fi and settings");
  nvsSavePending = false;

  wifiPrefs.begin("wifi", false);
  wifiPrefs.clear();
  wifiPrefs.end();

  devicePrefs.begin("settings", false);
  devicePrefs.clear();
  devicePrefs.end();

  setupServer.send(
    200,
    "text/html",
    "<html><body><h2>Factory reset complete.</h2><p>Restarting...</p></body></html>"
  );

  delay(1000);
  ESP.restart();
}

void handleRootPage() {
  if(!requireWebAuth()) return;
  String html=FPSTR(LOCAL_DASHBOARD);
  html.replace("{{CSRF}}",localSessionToken);
  setupServer.sendHeader("Cache-Control","no-store");
  setupServer.sendHeader("X-Frame-Options","DENY");
  setupServer.sendHeader("X-Content-Type-Options","nosniff");
  setupServer.send(200,"text/html",html);
}
void handleLocalApiCommand() {
  if(!requireWebAuth()) return;
  if(setupServer.arg("plain").length()>2048) { setupServer.send(413,"text/plain","Command too large"); return; }
  JsonDocument d;
  if(deserializeJson(d,setupServer.arg("plain")) || !d["args"].is<JsonObject>()) { setupServer.send(400,"text/plain","Invalid command"); return; }
  String target=d["target"]|"";
  if(target!="plug" && target!="iron") { setupServer.send(400,"text/plain","Unknown output"); return; }
  String reason; bool ok=executeCommand(target=="iron",d["action"]|"",d["args"].as<JsonObject>(),reason);
  setupServer.send(ok?200:409,"text/plain",reason);
}

void handleLocalSettings() {
  if (!requireWebAuth()) return;
  String requestedMode = setupServer.arg("mode");
  if (requestedMode == "MANUAL" || requestedMode == "AUTO" || requestedMode == "HYBRID" || requestedMode == "NORMAL") setMode(requestedMode);
  int pir = setupServer.arg("pir").toInt();
  if (pir < 0) pir = 0; if (pir > 2) pir = 2;
  pirSensitivity = pir;
  long human = setupServer.arg("human").toInt(); if (human < 1) human = 1; if (human > 86400) human = 86400;
  long delaySec = setupServer.arg("delay").toInt(); if (delaySec < 0) delaySec = 0; if (delaySec > 3600) delaySec = 3600;
  long maxSec = setupServer.arg("maxsec").toInt(); if (maxSec < 1) maxSec = 1; if (maxSec > 86400) maxSec = 86400;
  humanOnSeconds = (unsigned long)human;
  detectionDelaySeconds = (unsigned long)delaySec;
  maxOnSeconds = (unsigned long)maxSec;
  maxOnEnabled = setupServer.arg("maxen") == "1";
  saveSettings();
  setupServer.sendHeader("Location", "/");
  setupServer.send(303, "text/plain", "Settings saved");
}

void handleChangeWebLogin() {
  if (!requireWebAuth()) return;
  String username = setupServer.arg("username");
  String password = setupServer.arg("password");
  String confirm = setupServer.arg("confirm");
  if (username.length() < 1 || username.length() > 32 || password.length() < 8 || password.length() > 64 || password != confirm) {
    setupServer.send(400, "text/html", "<h2>Login not changed</h2><p>Use a username and matching password of at least 8 characters.</p><p><a href='/'>Back</a></p>");
    return;
  }
  webAuthPrefs.begin("webauth", false);
  webAuthPrefs.putString("username", username);
  webAuthPrefs.putString("password", password);
  webAuthPrefs.end();
  activeWebUsername = username;
  activeWebPassword = password;
  revokeLocalSessions();
  setupServer.send(200, "text/html", "<h2>Login updated</h2><p>The new login is saved. Your browser may need you to authenticate again.</p><p><a href='/'>Return to dashboard</a></p>");
}

void handleFirmwareUpload() {
#if ERU_ENABLE_LOCAL_OTA
  HTTPUpload& upload=setupServer.upload();
  if(upload.status==UPLOAD_FILE_START) {
    otaAuthorized=false; otaCompleted=false;
    if(!requireWebAuth()) return;
    enterMaintenance("Firmware update; outputs OFF");
    otaAuthorized=Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if(upload.status==UPLOAD_FILE_WRITE && otaAuthorized) {
    if(Update.write(upload.buf,upload.currentSize)!=upload.currentSize) { Update.abort(); otaAuthorized=false; }
  } else if(upload.status==UPLOAD_FILE_END && otaAuthorized) otaCompleted=Update.end(true);
  else if(upload.status==UPLOAD_FILE_ABORTED) { Update.abort(); otaAuthorized=false; }
#endif
}
void handleFirmwareUploadResult() {
  if(!requireWebAuth()) return;
#if ERU_ENABLE_LOCAL_OTA
  if(!otaAuthorized || !otaCompleted || Update.hasError()) { setupServer.send(400,"text/plain","Update failed or no valid firmware uploaded; outputs remain OFF"); return; }
  setupServer.send(200,"text/plain","Firmware updated; restarting with outputs OFF");
  delay(500); ESP.restart();
#else
  setupServer.send(403,"text/plain","Browser OTA is disabled in this build. Upload the compiled firmware using USB.");
#endif
}

void handleLocalPowerCommand() {
  if(!requireWebAuth()) return;
  JsonDocument args; String reason;
  String action=setupServer.uri()=="/local/on"?"on":setupServer.uri()=="/local/off"?"off":actualPowerState?"off":"on";
  bool ok=executeCommand(false,action,args.to<JsonObject>(),reason);
  setupServer.send(ok?200:409,"text/plain",reason);
}
void handleLocalStatus() {
  if(!requireWebAuth()) return;
  setupServer.sendHeader("Cache-Control","no-store");
  setupServer.send(200,"application/json",statusPayload());
}

void configureWebServer() {

  if (webServerStarted) return;

  const char* headers[]={"X-CSRF-Token"};
  setupServer.collectHeaders(headers,1);
  setupServer.on("/", HTTP_GET, handleRootPage);
  setupServer.on("/api/command", HTTP_POST, handleLocalApiCommand);
  setupServer.on("/wifi", HTTP_GET, handleWiFiPage);
  setupServer.on("/connect", HTTP_POST, handleWiFiConnect);
  setupServer.on("/factory-reset", HTTP_POST, handleFactoryReset);
  setupServer.on("/local/on", HTTP_POST, handleLocalPowerCommand);
  setupServer.on("/local/off", HTTP_POST, handleLocalPowerCommand);
  setupServer.on("/local/toggle", HTTP_POST, handleLocalPowerCommand);
  setupServer.on("/iron/on", HTTP_POST, handleIronPowerCommand);
  setupServer.on("/iron/off", HTTP_POST, handleIronPowerCommand);
  setupServer.on("/iron/settings", HTTP_POST, handleIronSettings);
  setupServer.on("/api/status", HTTP_GET, handleLocalStatus);
  setupServer.on("/settings", HTTP_POST, handleLocalSettings);
  setupServer.on("/change-login", HTTP_POST, handleChangeWebLogin);
  setupServer.on("/update", HTTP_POST, handleFirmwareUploadResult, handleFirmwareUpload);

  setupServer.begin();
  webServerStarted = true;

  Serial.println("[WEB] Local web server started on port 80");
}

void startSetupAP() {

  Serial.println();
  Serial.println("==============================");
  Serial.println(" WIFI SETUP MODE");
  Serial.println("==============================");

  enterMaintenance("Wi-Fi setup; outputs OFF");
  WiFi.disconnect(true);
  delay(500);

  WiFi.mode(WIFI_AP);
  bool apStarted = WiFi.softAP(setupApName.c_str(), setupApPassword.c_str());

  setupApActive = apStarted;

  if (!apStarted) {
    Serial.println("[AP] ERROR: Could not start setup hotspot.");
    WiFi.mode(WIFI_STA);
    return;
  }

  Serial.print("[AP] SSID: ");
  Serial.println(setupApName);
  Serial.print("[AP] IP: ");
  Serial.println(WiFi.softAPIP());

  configureWebServer();

  Serial.println("[AP] Web server ready");
}
