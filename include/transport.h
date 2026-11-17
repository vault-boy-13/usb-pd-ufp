#pragma once
#include <switch.h>
#include "policy.h"

typedef struct {
    Result identity_rc, pm_rc, omm_rc, hardware_rc;
    uint32_t os_data_role;
    uint16_t fw_type, fw_revision, manufacturer, device;
} UpDiagnostics;

typedef struct { uint32_t power_role, charger_type, voltage_raw, current_raw, pdo, rdo; } UpPowerState;
_Static_assert(sizeof(UpPowerState)==24, "UsbPowerState ABI");

Result up_read_register(I2cSession *session, u8 reg, void *dst, u8 size);
bool up_collect(I2cSession *controller, Service *pm, Service *omm, bool enabled,
                UpSample *sample, UpDiagnostics *diag);
Result up_send_swap(I2cSession *controller);
