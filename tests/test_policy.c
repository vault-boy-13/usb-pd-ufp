#include "policy.h"
#include "profile.h"
#include <assert.h>
#include <stdio.h>

static UpSample target(void) {
    UpSample s={.valid=true,.enabled=true,.identity_ok=true,.status=0x4e80,
        .config1=0x00e0,.sysconfig2=0x0244,.power_role=1,.charger_type=1,.dp_status=0x4000,
        .source_bytes=8,.selected_bytes=4,.rdo_bytes=4,
        .source={0x0a019096,0x0004b104},.selected_pdo=0x0004b104,.rdo=0x20041104};
    return s;
}
#define REFUSE(change) do { UpSample bad=s; change; assert(up_validate(&bad)!=UP_SWAP); } while(0)
int main(void) {
    const UpTiming t={10000,2000,30000};
    UpSample s=target();
    assert(up_validate(&s)==UP_SWAP);
    /* Regression: the deployed module mistook INSERT alone for a dock. */
    for(unsigned bit=0;bit<16;++bit) {
        UpSample dp=s;
        dp.dp_status|=1u<<bit;
        assert(up_validate(&dp)==(bit==14?UP_SWAP:UP_DOCK));
    }
    UpSample no_dp=s;no_dp.dp_status=0;assert(up_validate(&no_dp)==UP_SWAP);
    REFUSE(bad.enabled=false); REFUSE(bad.valid=false); REFUSE(bad.identity_ok=false);
    REFUSE(bad.status=0); REFUSE(bad.status=0x4d80);
    REFUSE(bad.operation_mode=1); REFUSE(bad.cradle_status=1); REFUSE(bad.dp_status=2);
    REFUSE(bad.source[0]|=1u<<26); REFUSE(bad.source[0]&=~(1u<<25));
    REFUSE(bad.power_role=2); REFUSE(bad.charger_type=0);
    REFUSE(bad.config1^=2); REFUSE(bad.config2=1); REFUSE(bad.sysconfig2=0);
    REFUSE(bad.source_bytes=0); REFUSE(bad.source_bytes=29); REFUSE(bad.source_bytes=7);
    REFUSE(bad.selected_bytes=0); REFUSE(bad.rdo_bytes=3);
    REFUSE(bad.selected_pdo^=1); REFUSE(bad.rdo=0); REFUSE(bad.rdo|=1u<<27);
    REFUSE(bad.rdo|=1u<<26); REFUSE(bad.rdo|=1023);
    REFUSE(bad.rdo|=1u<<31); REFUSE(bad.rdo=0x20041103);
    REFUSE(bad.status|=1u<<13); REFUSE(bad.status|=1u<<12); REFUSE(bad.status|=1);
    UpPolicy p={0};
    assert(up_step(&p,&s,0,&t)==UP_WAIT_ATTACH);
    assert(up_step(&p,&s,9999,&t)==UP_WAIT_ATTACH);
    assert(up_step(&p,&s,10000,&t)==UP_WAIT_STABLE);
    assert(up_step(&p,&s,11999,&t)==UP_WAIT_STABLE);
    assert(up_step(&p,&s,12000,&t)==UP_SWAP);
    for(unsigned i=12001;i<100000;++i) assert(up_step(&p,&s,i,&t)==UP_ATTEMPTED);
    s.enabled=false; assert(up_step(&p,&s,100000,&t)==UP_DISABLED);
    s.enabled=true; assert(up_step(&p,&s,120000,&t)==UP_ATTEMPTED);
    s.status=0x4d80; assert(up_step(&p,&s,120001,&t)==UP_ALREADY_UFP);
    s.status=0x4e80; up_resume(&p,120002);
    assert(up_step(&p,&s,130002,&t)==UP_WAIT_STABLE);
    assert(up_step(&p,&s,132002,&t)==UP_SWAP);
    up_resume(&p,132003);
    assert(up_step(&p,&s,142003,&t)==UP_WAIT_STABLE);
    assert(up_step(&p,&s,144003,&t)==UP_COOLDOWN);
    assert(up_step(&p,&s,162002,&t)==UP_SWAP);
    p=(UpPolicy){0}; s.cradle_status=1;
    assert(up_step(&p,&s,0,&t)==UP_DOCK);
    s.cradle_status=0;
    assert(up_step(&p,&s,50000,&t)==UP_DOCK);
    /* Lite acts on the first eligible sample; every other/unknown model
     * retains configured dock timing, including after a config reload. */
    {
    UpPolicy p;
    UpSample s;
    for(unsigned model=0;model<=8;++model) {
        assert(up_is_lite(true,model)==(model==4));
        assert(!up_is_lite(false,model));
        UpConfig cfg={.enabled=true,.poll_ms=500,.timing=t};
        up_apply_profile(&cfg,up_is_lite(true,model));
        assert(cfg.poll_ms==(model==4?100:500));
        p=(UpPolicy){0};s=target();
        assert(up_step(&p,&s,0,&cfg.timing)==(model==4?UP_SWAP:UP_WAIT_ATTACH));
        if(model!=4) continue;
        assert(up_step(&p,&s,100,&cfg.timing)==UP_ATTEMPTED);
        s.status=0x4d80;assert(up_step(&p,&s,200,&cfg.timing)==UP_ALREADY_UFP);
        up_resume(&p,300);s.status=0x4e80;
        assert(up_step(&p,&s,300,&cfg.timing)==UP_SWAP);
        s.status=0;up_step(&p,&s,400,&cfg.timing);up_step(&p,&s,1400,&cfg.timing);
        s.status=0x4e80;assert(up_step(&p,&s,1500,&cfg.timing)==UP_SWAP);
        p=(UpPolicy){0};s.valid=false;
        assert(up_step(&p,&s,1600,&cfg.timing)==UP_READ_ERROR);
        s.valid=true;s.status|=1u<<13;
        assert(up_step(&p,&s,1700,&cfg.timing)==UP_UNSAFE);
        s=target();s.cradle_status=1;
        assert(up_step(&p,&s,1800,&cfg.timing)==UP_DOCK);
        cfg=(UpConfig){.enabled=false,.poll_ms=500,.timing=t};
        up_apply_profile(&cfg,true);
        assert(!cfg.enabled && cfg.timing.settle_ms==0 && cfg.timing.stable_ms==0 && cfg.timing.cooldown_ms==0);
    }
    }
    up_resume(&p,60000); assert(up_step(&p,&s,90000,&t)==UP_DOCK);
    s.status=0; assert(up_step(&p,&s,90001,&t)==UP_DETACHED);
    s.status=0x4e80; assert(up_step(&p,&s,90500,&t)==UP_DOCK);
    s.status=0; up_step(&p,&s,91000,&t); up_step(&p,&s,92000,&t);
    s.status=0x4e80; assert(up_step(&p,&s,93000,&t)==UP_WAIT_ATTACH);
    assert(up_step(&p,&s,103000,&t)==UP_WAIT_STABLE);
    s.valid=false; assert(up_step(&p,&s,104000,&t)==UP_READ_ERROR);
    s.valid=true; assert(up_step(&p,&s,105000,&t)==UP_WAIT_STABLE);
    assert(up_step(&p,&s,107000,&t)==UP_SWAP);
    p=(UpPolicy){0}; s.valid=false; s.cradle_status=1;
    assert(up_step(&p,&s,0,&t)==UP_READ_ERROR);
    s.valid=true; s.cradle_status=0;
    assert(up_step(&p,&s,50000,&t)==UP_DOCK);
    puts("PASS: guards, hardware UFP, one-shot, reload, resume, cooldown, dock latch, detach debounce, read failure");
    return 0;
}
