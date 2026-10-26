#pragma once
#include <stddef.h>
#include <stdint.h>
namespace WiFiProvisioningRules {
inline bool validEpoch(uint64_t epoch) { return epoch>=1735689600ULL && epoch<4102444800ULL; }
inline bool validCredentials(bool enterprise,size_t ssidLength,size_t userLength,size_t passwordLength) {
  if(ssidLength<1 || ssidLength>32 || passwordLength>64) return false;
  return !enterprise || (userLength>=1 && userLength<=64 && passwordLength>=1);
}
}
