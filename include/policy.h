#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid, enabled, identity_ok;
    uint16_t status, config1, config2, sysconfig2, dp_status;
    uint8_t operation_mode, cradle_status, source_bytes, selected_bytes, rdo_bytes;
    uint32_t power_role, charger_type, source[7], selected_pdo, rdo;
} UpSample;

typedef enum {
    UP_DISABLED, UP_READ_ERROR, UP_DETACHED, UP_UNSUPPORTED,
    UP_DOCK, UP_DATA_PARTNER, UP_NO_PD, UP_UNSAFE, UP_NO_DRD,
    UP_ALREADY_UFP, UP_NOT_DFP, UP_WAIT_ATTACH, UP_WAIT_STABLE,
    UP_COOLDOWN, UP_ATTEMPTED, UP_SWAP
} UpReason;

typedef struct {
    uint32_t settle_ms, stable_ms, cooldown_ms;
} UpTiming;

typedef struct {
    bool attached, attempted, protected_peer, stable, has_sent, was_enabled, seeing_detach;
    uint64_t attached_at, stable_at, last_sent_at, detached_at;
    uint32_t signature;
} UpPolicy;

UpReason up_validate(const UpSample *s);
uint32_t up_signature(const UpSample *s);
UpReason up_step(UpPolicy *p, const UpSample *s, uint64_t now_ms, const UpTiming *timing);
void up_resume(UpPolicy *p, uint64_t now_ms);
const char *up_reason_name(UpReason reason);
