#include "policy.h"

static bool connected(const UpSample *s) { return (s->status & (1u << 7)) != 0; }
/* Bit 14 is INSERT, observed with ordinary charger connections (0x4000).
 * It alone cannot identify a dock or DP mode. Keep all other bits protected,
 * including unknown bits, PIN_CFG_DONE, SIGNAL_ON and DP_EN. */
static bool dp_protected(const UpSample *s) { return (s->dp_status & 0xbfffu) != 0; }

UpReason up_validate(const UpSample *s) {
    if (!s->enabled) return UP_DISABLED;
    if (!s->valid) return UP_READ_ERROR;
    if (!connected(s)) return UP_DETACHED;
    if (!s->identity_ok) return UP_UNSUPPORTED;
    if (s->operation_mode != 0 || s->cradle_status != 0 || dp_protected(s)) return UP_DOCK;
    if (s->power_role != 1 || s->charger_type != 1) return UP_NO_PD;
    if (s->source_bytes < 4 || s->source_bytes > 28 || s->source_bytes % 4) return UP_UNSAFE;
    const uint32_t first = s->source[0];
    if ((first >> 30) != 0 || ((first >> 10) & 1023) * 50 != 5000) return UP_UNSAFE;
    if (first & (1u << 26)) return UP_DATA_PARTNER; /* includes USB docks/hubs */
    if (!(first & (1u << 25))) return UP_NO_DRD;
    if (s->status & ((1u << 12) | (1u << 13) | 3u)) return UP_UNSAFE;
    if (!(s->status & (1u << 14)) || ((s->status >> 4) & 7) != 0) return UP_UNSAFE;
    /* Restrict this first release to the configuration measured on the target.
     * No attempt to repair controller configuration or suppress port policy. */
    if (s->config1 != 0x00e0 || s->config2 != 0 || s->sysconfig2 != 0x0244) return UP_UNSUPPORTED;
    if (s->selected_bytes != 4 || s->rdo_bytes != 4 || (s->selected_pdo >> 30) != 0) return UP_UNSAFE;
    const unsigned index = (s->rdo >> 28) & 7;
    const unsigned count = s->source_bytes / 4;
    if (!index || index > count || s->source[index - 1] != s->selected_pdo) return UP_UNSAFE;
    const unsigned mv = ((s->selected_pdo >> 10) & 1023) * 50;
    const unsigned ma_units = s->selected_pdo & 1023;
    if (mv < 5000 || mv > 20000 || ma_units == 0 || (s->rdo & ((1u << 31)|(1u << 27)|(1u << 26)))) return UP_UNSAFE;
    if (((s->rdo >> 10) & 1023)==0 || (s->rdo & 1023)==0) return UP_UNSAFE;
    if (((s->rdo >> 10) & 1023) > ma_units || (s->rdo & 1023) > ma_units) return UP_UNSAFE;
    if (((s->rdo >> 10) & 1023) > (s->rdo & 1023)) return UP_UNSAFE;
    const unsigned role = (s->status >> 8) & 3;
    if (role == 1) return UP_ALREADY_UFP;
    if (role != 2) return UP_NOT_DFP;
    return UP_SWAP;
}

uint32_t up_signature(const UpSample *s) {
    uint32_t hash = 2166136261u;
    const uint32_t fields[] = {s->selected_pdo, s->rdo, s->source_bytes, s->config1,
        s->config2, s->sysconfig2, s->power_role, s->charger_type};
    for (unsigned i=0; i<sizeof(fields)/sizeof(fields[0]); ++i) hash = (hash ^ fields[i]) * 16777619u;
    for (unsigned i=0; i<s->source_bytes/4 && i<7; ++i) hash = (hash ^ s->source[i]) * 16777619u;
    return hash;
}

void up_resume(UpPolicy *p, uint64_t now_ms) {
    /* One new opportunity after a completed sleep/wake, retaining cooldown. */
    p->attempted = false;
    p->stable = false;
    p->attached_at = now_ms;
    /* Keep dock protection until an observed detach. */
}

UpReason up_step(UpPolicy *p, const UpSample *s, uint64_t now, const UpTiming *t) {
    if (!s->enabled) { p->stable=false; p->was_enabled=false; p->seeing_detach=false; return UP_DISABLED; }
    if (!p->was_enabled) {
        p->was_enabled=true;
        p->attached_at=now;
        p->stable=false;
        /* A config reload cannot rearm a used attempt on the same attach. */
    }
    /* Preserve positively observed protection even if a later register read
     * failed: an incomplete sample can prohibit a swap, never authorize it. */
    if(s->identity_ok && (s->operation_mode || s->cradle_status || dp_protected(s) ||
       (s->source_bytes>=4 && s->source_bytes<=28 && s->source_bytes%4==0 && (s->source[0]&(1u<<26)))))
        p->protected_peer=true;
    if (!s->valid) { p->stable=false; p->seeing_detach=false; return UP_READ_ERROR; }
    if (!connected(s)) {
        p->stable=false;
        if (!p->seeing_detach) { p->seeing_detach=true; p->detached_at=now; }
        if (now-p->detached_at>=1000) {
            p->attached=false; p->attempted=false; p->protected_peer=false;
        }
        return UP_DETACHED;
    }
    p->seeing_detach=false;
    if (!p->attached) { p->attached=true; p->attached_at=now; p->stable=false; }
    const UpReason reason=up_validate(s);
    if (reason==UP_DOCK || reason==UP_DATA_PARTNER) p->protected_peer=true;
    if (p->protected_peer) { p->stable=false; return UP_DOCK; }
    if (reason!=UP_SWAP) { p->stable=false; return reason; }
    if (p->attempted) return UP_ATTEMPTED;
    if (now-p->attached_at < t->settle_ms) { p->stable=false; return UP_WAIT_ATTACH; }
    const uint32_t signature=up_signature(s);
    if (!p->stable || p->signature!=signature) {
        p->stable=true; p->signature=signature; p->stable_at=now;
        if(t->stable_ms) return UP_WAIT_STABLE;
    }
    if (now-p->stable_at < t->stable_ms) return UP_WAIT_STABLE;
    if (p->has_sent && now-p->last_sent_at < t->cooldown_ms) return UP_COOLDOWN;
    /* Consume before live recheck/send, including refusal or I2C failure. */
    p->attempted=true; p->has_sent=true; p->last_sent_at=now;
    return UP_SWAP;
}

const char *up_reason_name(UpReason r) {
    static const char *const names[]={"disabled","read_error","detached","unsupported_controller_config",
        "dock_or_data_partner_protected","data_partner","not_pd_sink","unsafe_or_busy","partner_has_no_drd",
        "already_ufp","not_dfp","settling_attach","waiting_stable_contract","cooldown","attempt_already_used","swap_once"};
    return (unsigned)r<sizeof(names)/sizeof(names[0])?names[r]:"unknown";
}
