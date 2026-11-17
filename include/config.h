#pragma once
#include "policy.h"
typedef struct { bool enabled, dry_run; uint32_t poll_ms, psc_module_id; UpTiming timing; } UpConfig;
bool up_load_config(UpConfig *out);
