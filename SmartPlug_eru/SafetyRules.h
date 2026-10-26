#pragma once
#include <stdint.h>
namespace SafetyRules {
inline bool elapsed(uint32_t now, uint32_t since, uint32_t seconds) {
  return seconds > 0 && seconds <= 86400 && uint32_t(now - since) >= seconds * 1000U;
}
inline bool mayEnergize(bool maintenance, bool tripped, bool sensorRequired, bool sensorHealthy) {
  return !maintenance && !tripped && (!sensorRequired || sensorHealthy);
}
inline uint32_t remaining(uint32_t now, uint32_t since, uint32_t seconds) {
  const uint32_t used = uint32_t(now - since) / 1000U;
  return used >= seconds ? 0 : seconds - used;
}
inline bool commandFresh(uint32_t now, uint32_t receivedAt, uint32_t ttlMs) {
  return ttlMs > 0 && ttlMs <= 30000U && uint32_t(now - receivedAt) < ttlMs;
}
}
