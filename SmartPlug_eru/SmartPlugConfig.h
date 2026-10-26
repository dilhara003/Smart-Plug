#pragma once
// Public client configuration. Never put a database password or service-role key here.
#define ERU_SUPABASE_URL "https://udmbcrnrdmxyhvgxcyfe.supabase.co"
#define ERU_SUPABASE_PUBLISHABLE_KEY "sb_publishable_SdL4E2M2PGyJLZ-DGfx_qQ_-yLw0cNh"
#define ERU_FIRMWARE_VERSION "1.0.0"
// Browser OTA stays disabled by default. Build/upload with USB, or explicitly
// enable for a trusted-LAN maintenance session after reviewing RUNNING.md.
#define ERU_ENABLE_LOCAL_OTA 0
#if __has_include("SmartPlugConfig.local.h")
#include "SmartPlugConfig.local.h"
#endif
