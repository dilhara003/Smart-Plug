#include "SmartPlugShared.h"
std::atomic<bool> controllerStallTrip{false};
std::atomic<uint32_t> controllerHeartbeat{0};
// Independent of HTTP clients, LCD traffic and the controller's main loop.
// A stalled loop latches outputs OFF; only a restart leaves maintenance.
void safetyMonitorTick() {
  if(uint32_t(millis()-controllerHeartbeat.load())>1000U) controllerStallTrip=true;
  if(controllerStallTrip.load()) {
    digitalWrite(RELAY1_PIN,RELAY_OFF);
    digitalWrite(RELAY2_PIN,RELAY_OFF);
  }
}
static void safetyTask(void*) {
  for(;;) { safetyMonitorTick(); vTaskDelay(pdMS_TO_TICKS(20)); }
}
void startSafetyMonitor() {
  controllerHeartbeat=millis();
  if(xTaskCreatePinnedToCore(safetyTask,"safetyMonitor",2048,NULL,3,NULL,1)!=pdPASS) {
    controllerStallTrip=true;
    serviceSafetyTrip();
    Serial.println("[SAFETY] Monitor unavailable; outputs locked OFF");
  }
}
void serviceSafetyTrip() {
  if(controllerStallTrip.load() && !maintenanceMode) {
    plugTripped=true; ironTripped=true;
    enterMaintenance("Control response timeout; restart and rearm required");
  }
}
