#include "profile.h"

bool up_is_lite(bool model_read_ok, uint32_t model) {
    return model_read_ok && model==4;
}

void up_apply_profile(UpConfig *config, bool lite) {
    if(!lite) return;
    config->poll_ms=100;
    config->timing=(UpTiming){0,0,0};
}
