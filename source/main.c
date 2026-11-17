#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "policy.h"
#include "transport.h"
#include "config.h"
#include "profile.h"

#define VERSION "0.1.2"
#define DIRECTORY "sdmc:/config/usb-pd-ufp"
#define LOG_PATH DIRECTORY "/log.txt"

u32 __nx_applet_type=AppletType_None;
u32 __nx_fs_num_sessions=1;
static Result init_rc;
static bool fs_up, mounted;
static SetSysProductModel product_model=SetSysProductModel_Invalid;
static Result model_rc=UINT32_MAX;
_Static_assert(SetSysProductModel_Hoag==4,"Lite profile product model ABI");

void __libnx_initheap(void) {
    static u8 heap[0x10000] __attribute__((aligned(0x1000)));
    extern void *fake_heap_start, *fake_heap_end;
    fake_heap_start=heap; fake_heap_end=heap+sizeof(heap);
}
void __appInit(void) {
    init_rc=smInitialize();
    if(R_FAILED(init_rc)) return;
    init_rc=setsysInitialize();
    if(R_FAILED(init_rc)) return;
    SetSysFirmwareVersion fw;
    init_rc=setsysGetFirmwareVersion(&fw);
    if(R_SUCCEEDED(init_rc)) {
        hosversionSet(MAKEHOSVERSION(fw.major,fw.minor,fw.micro));
        model_rc=setsysGetProductModel(&product_model);
    }
    setsysExit();
    if(R_FAILED(init_rc)) return;
    hosversionSet(MAKEHOSVERSION(fw.major,fw.minor,fw.micro));
    init_rc=fsInitialize();
    if(R_FAILED(init_rc)) return;
    fs_up=true;
    init_rc=fsdevMountSdmc();
    mounted=R_SUCCEEDED(init_rc);
}
void __appExit(void) {
    if(mounted) fsdevUnmountAll();
    if(fs_up) fsExit();
    smExit();
}

static uint64_t now_ms(void) { return armTicksToNs(armGetSystemTick())/1000000u; }
static bool actions_enabled(const UpConfig *config) {
    if(!config->enabled) return false;
    errno=0;
    return access(DIRECTORY "/disable.flag",F_OK)!=0 && errno==ENOENT;
}

static void log_line(const char *format, ...) {
    struct stat info;
    if(stat(LOG_PATH,&info)==0 && info.st_size>1024*1024) {
        remove(DIRECTORY "/log.prev.txt");
        rename(LOG_PATH,DIRECTORY "/log.prev.txt");
    }
    FILE *file=fopen(LOG_PATH,"a");
    if(!file) return;
    fprintf(file,"[%llu] ",(unsigned long long)now_ms());
    va_list args; va_start(args,format); vfprintf(file,format,args); va_end(args);
    fputc('\n',file); fclose(file);
}

static void log_sample(UpReason reason, const UpSample *s, const UpDiagnostics *d) {
    log_line("state=%s hw=%04x dp=%04x omm=%u/%u pdo=%08x rdo=%08x src0=%08x os_role=%u rc=%08x/%08x/%08x/%08x id=%04x/%04x fw=%04x/%04x",
        up_reason_name(reason),s->status,s->dp_status,s->operation_mode,s->cradle_status,
        s->selected_pdo,s->rdo,s->source[0],d->os_data_role,d->identity_rc,d->pm_rc,d->omm_rc,d->hardware_rc,
        d->manufacturer,d->device,d->fw_type,d->fw_revision);
}

int main(int argc,char **argv) {
    (void)argc; (void)argv;
    if(R_FAILED(init_rc) || !mounted) return 1;
    mkdir(DIRECTORY,0777);
    log_line("usb-pd-ufp %s start; no port configuration writes",VERSION);
    UpConfig config;
    if(!up_load_config(&config)) {log_line("invalid config; module exits without touching PD");return 1;}
    const bool lite=up_is_lite(R_SUCCEEDED(model_rc),product_model);
    up_apply_profile(&config,lite);
    log_line("product_model=%u model_rc=%08x profile=%s poll_ms=%u settle_ms=%u stable_ms=%u cooldown_ms=%u",
        (unsigned)product_model,model_rc,lite?"lite_immediate":"dock_compatible",config.poll_ms,
        config.timing.settle_ms,config.timing.stable_ms,config.timing.cooldown_ms);
    /* Lite starts as soon as services are available, with no fixed boot delay. */
    if(!lite) svcSleepThread(20000000000ULL);

    Service pm={0},omm={0};
    I2cSession controller={0};
    bool pm_up=false,omm_up=false,i2c_up=false,controller_up=false,psc_up=false,registered=false;
    PscPmModule psc={0};
    Result rc=smGetService(&pm,"usb:pm");
    if(R_FAILED(rc)) {log_line("usb:pm open failed %08x",rc);goto cleanup;}
    pm_up=true;
    rc=smGetService(&omm,"omm");
    if(R_FAILED(rc)) {log_line("omm open failed %08x; dock state unknown, no action",rc);goto cleanup;}
    omm_up=true;
    rc=i2cInitialize();
    if(R_FAILED(rc)) {log_line("i2c init failed %08x",rc);goto cleanup;}
    i2c_up=true;
    rc=i2cOpenSession(&controller,I2cDevice_Bm92t30mwv);
    if(R_FAILED(rc)) {log_line("controller open failed %08x",rc);goto cleanup;}
    controller_up=true;
    rc=pscmInitialize();
    if(R_FAILED(rc)) {log_line("psc init failed %08x",rc);goto cleanup;}
    psc_up=true;
    const u32 dependencies[]={PscPmModuleId_Usb,PscPmModuleId_I2c,PscPmModuleId_Fs,PscPmModuleId_Omm};
    rc=pscmGetPmModule(&psc,(PscPmModuleId)config.psc_module_id,dependencies,4,true);
    if(R_FAILED(rc)) {log_line("psc registration id=%x failed %08x; no action",config.psc_module_id,rc);goto cleanup;}
    registered=true;
    UpPolicy policy={0};
    bool paused=false,was_sleeping=false,running=true;
    uint64_t config_at=0,heartbeat_at=0;
    UpReason previous=UP_SWAP;
    UpSample sample={0}; UpDiagnostics diagnostics={0};
    while(running) {
        const Result wait_rc=eventWait(&psc.event,(u64)config.poll_ms*1000000u);
        if(R_SUCCEEDED(wait_rc)) {
            PscPmState state; u32 flags;
            rc=pscPmModuleGetRequest(&psc,&state,&flags);
            if(R_FAILED(rc)) {log_line("psc request failed %08x; stopping",rc);break;}
            (void)flags;
            /* Never touch I2C after acknowledging sleep or before fully awake. */
            if(state==PscPmState_ReadySleep || state==PscPmState_ReadySleepCritical) {
                paused=true; was_sleeping=true;
            } else if(state==PscPmState_ReadyShutdown) {paused=true;running=false;}
            else if(state==PscPmState_Awake) {
                paused=false;
                if(was_sleeping) {up_resume(&policy,now_ms());was_sleeping=false;previous=UP_SWAP;}
            } else paused=true;
            rc=pscPmModuleAcknowledge(&psc,state);
            if(R_FAILED(rc)) break;
        } else if(wait_rc!=KERNELRESULT(TimedOut)) {
            log_line("psc wait failed %08x; stopping",wait_rc);break;
        }
        if(paused || !running) continue;
        const uint64_t now=now_ms();
        if(now>=config_at) {
            UpConfig updated;
            if(!up_load_config(&updated)) log_line("config invalid; actions disabled");
            up_apply_profile(&updated,lite);
            if(updated.psc_module_id!=config.psc_module_id) {
                updated.enabled=false; log_line("PSC id changed; restart required, actions disabled");
            }
            config=updated; config_at=now+5000;
        }
        const bool enabled=actions_enabled(&config);
        memset(&sample,0,sizeof(sample)); memset(&diagnostics,0,sizeof(diagnostics));
        sample.enabled=enabled;
        if(enabled) up_collect(&controller,&pm,&omm,true,&sample,&diagnostics);
        const UpReason reason=up_step(&policy,&sample,now,&config.timing);
        if(reason!=previous || now>=heartbeat_at) {
            log_sample(reason,&sample,&diagnostics);previous=reason;heartbeat_at=now+60000;
        }
        if(reason==UP_SWAP) {
            /* Live recheck closes the gap after settling; any refusal consumes the opportunity. */
            UpSample check; UpDiagnostics detail;
            up_collect(&controller,&pm,&omm,true,&check,&detail);
            const bool still_enabled=actions_enabled(&config);
            if(!still_enabled || up_validate(&check)!=UP_SWAP || up_signature(&check)!=up_signature(&sample)) {
                if(lite) {
                    /* Nothing was sent. A transient early contract/read change
                     * must not consume the only opportunity in fast mode. */
                    policy.attempted=false; policy.stable=false;
                    log_line("swap not sent: live recheck changed; waiting for eligible sample");
                } else log_line("swap refused on live recheck; no retries on this attach");
                continue;
            }
            if(config.dry_run) {log_line("dry_run: would send one DR_SWAP; no command sent");continue;}
            rc=up_send_swap(&controller);
            log_line("DR_SWAP sent once rc=%08x; no retries until detach/resume",rc);
        }
    }
    rc=0;
cleanup:
    if(registered) {pscPmModuleFinalize(&psc);pscPmModuleClose(&psc);eventClose(&psc.event);}
    if(psc_up) pscmExit();
    if(controller_up) i2csessionClose(&controller);
    if(i2c_up) i2cExit();
    if(omm_up) serviceClose(&omm);
    if(pm_up) serviceClose(&pm);
    return R_FAILED(rc)?1:0;
}
