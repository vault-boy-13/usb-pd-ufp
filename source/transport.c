#include "transport.h"
#include <string.h>

static u16 le16(const u8 *p) { return p[0] | ((u16)p[1]<<8); }
static u32 le32(const u8 *p) { return p[0] | ((u32)p[1]<<8) | ((u32)p[2]<<16) | ((u32)p[3]<<24); }

Result up_read_register(I2cSession *session, u8 reg, void *dst, u8 size) {
    const bool small=reg==0x03 || reg==0x06 || reg==0x17 || reg==0x18 || reg==0x27 || (reg>=0x4b && reg<=0x4e);
    if (!((small && size==2) || (reg==0x08 && size==29) || ((reg==0x28 || reg==0x2b) && size==5)))
        return MAKERESULT(Module_Libnx,LibnxError_BadInput);
    const u8 command[]={0x40,1,reg,0xc1,size};
    memset(dst,0,size);
    return i2csessionExecuteCommandList(session,dst,size,command,sizeof(command));
}

bool up_collect(I2cSession *controller, Service *pm, Service *omm, bool enabled, UpSample *s, UpDiagnostics *d) {
    memset(s,0,sizeof(*s)); memset(d,0,sizeof(*d)); s->enabled=enabled;
    d->identity_rc=d->pm_rc=d->omm_rc=d->hardware_rc=UINT32_MAX;
    u8 value[29];
    const u8 id_regs[]={0x4b,0x4c,0x4d,0x4e};
    u16 *id_out[]={&d->fw_type,&d->fw_revision,&d->manufacturer,&d->device};
    for(unsigned i=0;i<4;++i) {
        d->identity_rc=up_read_register(controller,id_regs[i],value,2);
        if(R_FAILED(d->identity_rc)) return false;
        *id_out[i]=le16(value);
    }
    s->identity_ok=d->manufacturer==0x04b5 && d->device==0x03b0 && d->fw_type==0x0603 && d->fw_revision==0x0d7c;
    if (!s->identity_ok) return false;
    UpPowerState power={0};
    d->pm_rc=serviceDispatch(pm,1,.buffer_attrs={SfBufferAttr_Out|SfBufferAttr_HipcMapAlias},.buffers={{&power,sizeof(power)}});
    if(R_FAILED(d->pm_rc)) return false;
    s->power_role=power.power_role; s->charger_type=power.charger_type;
    /* os_data_role is diagnostic only: it remained stale after the successful test. */
    Result role_rc=serviceDispatchOut(pm,3,d->os_data_role);
    if(R_FAILED(role_rc)) { d->pm_rc=role_rc; return false; }
    d->omm_rc=serviceDispatchOut(omm,0,s->operation_mode);
    if(R_FAILED(d->omm_rc)) return false;
    d->omm_rc=serviceDispatchOut(omm,5,s->cradle_status);
    if(R_FAILED(d->omm_rc)) return false;
    const u8 registers[]={0x03,0x06,0x17,0x27,0x18};
    u16 *fields[]={&s->status,&s->config1,&s->config2,&s->sysconfig2,&s->dp_status};
    for(unsigned i=0;i<5;++i) {
        d->hardware_rc=up_read_register(controller,registers[i],value,2);
        if(R_FAILED(d->hardware_rc)) return false;
        *fields[i]=le16(value);
    }
    d->hardware_rc=up_read_register(controller,0x08,value,29);
    if(R_FAILED(d->hardware_rc)) return false;
    s->source_bytes=value[0];
    for(unsigned i=0;i<7;++i) s->source[i]=le32(value+1+4*i);
    d->hardware_rc=up_read_register(controller,0x28,value,5);
    if(R_FAILED(d->hardware_rc)) return false;
    s->selected_bytes=value[0]; s->selected_pdo=le32(value+1);
    d->hardware_rc=up_read_register(controller,0x2b,value,5);
    if(R_FAILED(d->hardware_rc)) return false;
    s->rdo_bytes=value[0]; s->rdo=le32(value+1);
    s->valid=true;
    return true;
}

Result up_send_swap(I2cSession *controller) {
    /* The only write-capable operation in this module. No arbitrary register API. */
    const u8 command[]={0x05,0x18,0x18};
    return i2csessionSendAuto(controller,command,sizeof(command),I2cTransactionOption_All);
}
