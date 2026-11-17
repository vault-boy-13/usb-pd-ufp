#include "transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned reads,writes;
static int fail_register=-1;
static bool dock,fail_omm;
static void put16(void *dst,u16 v) {u8 *p=dst;p[0]=v;p[1]=v>>8;}
static void put32(void *dst,u32 v) {u8 *p=dst;for(unsigned i=0;i<4;++i)p[i]=v>>(i*8);}
Result i2csessionExecuteCommandList(I2cSession *s,void *dst,size_t size,const void *command,size_t len) {
    (void)s;
    const u8 *c=command;
    assert(len==5 && c[0]==0x40 && c[1]==1 && c[3]==0xc1 && c[4]==size);
    ++reads;
    if(c[2]==fail_register) return 1;
    switch(c[2]) {
        case 0x4b:put16(dst,0x0603);break;case 0x4c:put16(dst,0x0d7c);break;
        case 0x4d:put16(dst,0x04b5);break;case 0x4e:put16(dst,0x03b0);break;
        case 0x03:put16(dst,0x4e80);break;case 0x06:put16(dst,0x00e0);break;
        case 0x27:put16(dst,0x0244);break;case 0x17:put16(dst,0);break;
        case 0x18:put16(dst,0x4000);break;
        case 0x08:assert(size==29);((u8 *)dst)[0]=8;put32((u8 *)dst+1,0x0a019096);put32((u8 *)dst+5,0x0004b104);break;
        case 0x28:assert(size==5);((u8 *)dst)[0]=4;put32((u8 *)dst+1,0x0004b104);break;
        case 0x2b:assert(size==5);((u8 *)dst)[0]=4;put32((u8 *)dst+1,0x20041104);break;
        default:assert(0);
    }
    return 0;
}
Result i2csessionSendAuto(I2cSession *s,const void *data,size_t size,u32 option) {
    (void)s;
    const u8 expected[]={5,0x18,0x18};
    assert(size==sizeof(expected) && memcmp(data,expected,size)==0 && option==I2cTransactionOption_All);
    ++writes;return 0;
}
Result test_dispatch(Service *s,u32 id,TestDispatch p) {
    assert(s->id==1 && id==1 && p.buffers[0].size==24);
    UpPowerState *power=p.buffers[0].ptr;
    power->power_role=1;power->charger_type=1;return 0;
}
Result test_out(Service *s,u32 id,void *dst,size_t size) {
    if(s->id==1) {assert(id==3 && size==4);*(u32 *)dst=1;return 0;}
    assert(s->id==2 && (id==0 || id==5) && size==1);
    if(fail_omm) return 1;
    *(u8 *)dst=(id==5 && dock)?1:0;return 0;
}
int main(void) {
    I2cSession i={0};Service pm={1},omm={2};UpSample s;UpDiagnostics d;
    assert(up_collect(&i,&pm,&omm,true,&s,&d));
    assert(up_validate(&s)==UP_SWAP && s.selected_pdo==0x4b104 && s.rdo==0x20041104);
    assert(d.os_data_role==1 && writes==0);
    dock=true;assert(up_collect(&i,&pm,&omm,true,&s,&d));assert(up_validate(&s)==UP_DOCK);
    fail_omm=true;assert(!up_collect(&i,&pm,&omm,true,&s,&d));assert(up_validate(&s)==UP_READ_ERROR);
    fail_omm=false;dock=false;
    const u8 registers[]={0x4b,0x4c,0x4d,0x4e,3,6,0x17,0x27,0x18,8,0x28,0x2b};
    for(unsigned n=0;n<sizeof(registers);++n) {
        fail_register=registers[n];assert(!up_collect(&i,&pm,&omm,true,&s,&d));
        assert(up_validate(&s)!=UP_SWAP);
    }
    u8 scratch[32];const unsigned before=reads;
    assert(R_FAILED(up_read_register(&i,2,scratch,2)));
    assert(R_FAILED(up_read_register(&i,5,scratch,2)));
    assert(R_FAILED(up_read_register(&i,8,scratch,30)));
    assert(reads==before && writes==0);
    assert(up_send_swap(&i)==0 && writes==1);
    puts("PASS: serialized read bytes, PDO/RDO decode, OMM u8 ABI, every read failure, register allowlist, exact DR_SWAP bytes");
    return 0;
}
