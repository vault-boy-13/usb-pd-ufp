#pragma once
#include "config.h"

/* Product model values from libnx SetSysProductModel. Only confirmed Hoag
 * enables fast timing; unknown/read errors keep the dock-capable profile. */
bool up_is_lite(bool model_read_ok, uint32_t model);
void up_apply_profile(UpConfig *config, bool lite);
