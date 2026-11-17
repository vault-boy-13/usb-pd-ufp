#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint32_t Result;
typedef struct {int id;} Service;
typedef struct {int id;} I2cSession;
typedef struct {u32 buffer_attrs[1]; struct {void *ptr; size_t size;} buffers[1];} TestDispatch;
#define R_FAILED(r) ((r)!=0)
#define SfBufferAttr_Out 1
#define SfBufferAttr_HipcMapAlias 2
#define Module_Libnx 345
#define LibnxError_BadInput 3
#define MAKERESULT(m,e) (((e)<<9)|(m))
#define I2cTransactionOption_All 3
Result i2csessionExecuteCommandList(I2cSession *,void *,size_t,const void *,size_t);
Result i2csessionSendAuto(I2cSession *,const void *,size_t,u32);
Result test_dispatch(Service *,u32,TestDispatch);
Result test_out(Service *,u32,void *,size_t);
#define serviceDispatch(s,id,...) test_dispatch(s,id,(TestDispatch){__VA_ARGS__})
#define serviceDispatchOut(s,id,out) test_out(s,id,&(out),sizeof(out))
