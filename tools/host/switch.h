// Host (PC) mock <switch.h> for NX Diag's UI preview build.
//
// It is a UI mock only: every libnx call here is a stub that returns dummy
// data. The point is to preview the menus, report views and navigation on a
// desktop without a Switch - the probe numbers are meaningless.
#pragma once
#include "nxd_host_libnx.h"   // framework base: types, framebuffer, pad, ...
#include <stdbool.h>

// --- result helpers ---------------------------------------------------------
typedef u32 Handle;
#define CUR_PROCESS_HANDLE 0xFFFF8001
#define CUR_THREAD_HANDLE  0xFFFF8000
#define INVALID_HANDLE     0
#define MAKERESULT(module, desc) ((u32)(((module) & 0x1FF) | (((desc) & 0x1FFF) << 9)))
#define R_MODULE(rc)        ((rc) & 0x1FF)
#define R_DESCRIPTION(rc)   (((rc) >> 9) & 0x1FFF)
enum { Module_Libnx = 345 };
enum { LibnxError_IoError = 25 };

// --- threads (real std::thread under the hood) ------------------------------
typedef void (*ThreadFunc)(void*);
typedef struct Thread {
    void*      impl;
    ThreadFunc fn;
    void*      arg;
} Thread;
Result threadCreate(Thread* t, ThreadFunc entry, void* arg, void* stack,
                    size_t stack_sz, int prio, int cpuid);
Result threadStart(Thread* t);
Result threadWaitForExit(Thread* t);
Result threadClose(Thread* t);
void   svcSleepThread(s64 ns);

typedef u32 Mutex;
void mutexLock(Mutex* m);
void mutexUnlock(Mutex* m);

#define FS_MAX_PATH 0x301

// --- svc --------------------------------------------------------------------
typedef enum {
    InfoType_CoreMask = 0, InfoType_PriorityMask = 1,
    InfoType_AliasRegionAddress = 2, InfoType_AliasRegionSize = 3,
    InfoType_HeapRegionAddress = 4, InfoType_HeapRegionSize = 5,
    InfoType_TotalMemorySize = 6, InfoType_UsedMemorySize = 7,
    InfoType_DebuggerAttached = 8, InfoType_ResourceLimit = 9,
    InfoType_IdleTickCount = 10, InfoType_RandomEntropy = 11,
    InfoType_AslrRegionAddress = 12, InfoType_AslrRegionSize = 13,
    InfoType_StackRegionAddress = 14, InfoType_StackRegionSize = 15,
    InfoType_SystemResourceSizeTotal = 16, InfoType_SystemResourceSizeUsed = 17,
    InfoType_ProgramId = 18, InfoType_UserExceptionContextAddress = 20,
    InfoType_TotalNonSystemMemorySize = 21, InfoType_UsedNonSystemMemorySize = 22,
    InfoType_IsApplication = 23, InfoType_FreeThreadCount = 24,
} InfoType;

typedef enum {
    MemType_Unmapped = 0, MemType_Io = 1, MemType_Normal = 2,
    MemType_CodeStatic = 3, MemType_CodeMutable = 4, MemType_Heap = 5,
    MemType_SharedMem = 6, MemType_ModuleCodeStatic = 8,
    MemType_ModuleCodeMutable = 9, MemType_Reserved = 0x10,
    MemType_KernelStack = 0x13,
} MemoryType;

typedef struct MemoryInfo {
    u64 addr, size;
    u32 type, attr, perm, ipc_refcount, device_refcount, padding;
} MemoryInfo;

Result svcGetInfo(u64* out, u32 id, Handle handle, u64 sub);
u32    svcGetCurrentProcessorNumber(void);
Result svcQueryMemory(MemoryInfo* mi, u32* pageinfo, u64 addr);
u64    svcGetSystemTick(void);
Result svcGetThreadPriority(s32* prio, Handle h);
Result svcGetProcessId(u64* pid, Handle h);
Result svcGetThreadId(u64* tid, Handle h);
u64    armGetSystemTickFreq(void);
void   randomGet(void* buf, size_t len);

// --- libnx environment ------------------------------------------------------
bool   envIsNso(void);
bool   envHasArgv(void);
bool   envHasHeapOverride(void);
bool   envHasNextLoad(void);
bool   envIsSyscallHinted(unsigned svc);
Handle envGetOwnProcessHandle(void);
Handle envGetMainThreadHandle(void);

// --- applet / apm -----------------------------------------------------------
typedef enum { AppletOperationMode_Handheld = 0, AppletOperationMode_Console = 1 } AppletOperationMode;
typedef enum { ApmPerformanceMode_Invalid = -1, ApmPerformanceMode_Normal = 0, ApmPerformanceMode_Boost = 1 } ApmPerformanceMode;
AppletOperationMode appletGetOperationMode(void);
ApmPerformanceMode  appletGetPerformanceMode(void);
Result apmInitialize(void);          void apmExit(void);
Result apmGetPerformanceMode(ApmPerformanceMode* out);
Result apmGetPerformanceConfiguration(ApmPerformanceMode mode, u32* out);

// --- setsys / set -----------------------------------------------------------
typedef struct {
    u8 major, minor, micro, padding1, revision_major, revision_minor, padding2, padding3;
    char platform[0x20], version_hash[0x40], display_version[0x18], display_title[0x80];
} SetSysFirmwareVersion;
typedef struct { char digest[0x40]; } SetSysFirmwareVersionDigest;
typedef struct { char number[0x18]; } SetSysSerialNumber;
typedef enum { ColorSetId_Light = 0, ColorSetId_Dark = 1 } ColorSetId;
typedef enum { SetRegion_JPN=0, SetRegion_USA=1, SetRegion_EUR=2, SetRegion_AUS=3, SetRegion_HTK=4, SetRegion_CHN=5 } SetRegion;
Result setsysInitialize(void);  void setsysExit(void);
Result setsysGetFirmwareVersion(SetSysFirmwareVersion* out);
Result setsysGetFirmwareVersionDigest(SetSysFirmwareVersionDigest* out);
Result setsysGetColorSetId(ColorSetId* out);
Result setsysGetSerialNumber(SetSysSerialNumber* out);
Result setInitialize(void);  void setExit(void);
Result setGetRegionCode(SetRegion* out);
Result setGetSystemLanguage(u64* out);
Result setGetLanguageCode(u64* out);

// --- spl --------------------------------------------------------------------
typedef enum {
    SplConfigItem_DramId = 2, SplConfigItem_HardwareType = 5, SplConfigItem_IsRetail = 6,
    SplConfigItem_IsRecoveryBoot = 7, SplConfigItem_DeviceId = 8, SplConfigItem_BootReason = 9,
    SplConfigItem_MemoryArrange = 10, SplConfigItem_IsDebugMode = 11,
    SplConfigItem_KernelMemoryConfiguration = 12, SplConfigItem_IsKiosk = 14,
    SplConfigItem_NewHardwareType = 15,
} SplConfigItem;
Result splInitialize(void);  void splExit(void);
Result splGetConfig(SplConfigItem item, u64* out);
Result splGetRandomBytes(void* out, size_t size);
Result splIsDevelopment(bool* out);

// --- psm / ts / fan / lbl ---------------------------------------------------
typedef enum { PsmChargerType_Unconnected=0, PsmChargerType_EnoughPower=1, PsmChargerType_LowPower=2, PsmChargerType_NotSupported=3 } PsmChargerType;
Result psmInitialize(void);  void psmExit(void);
Result psmGetBatteryChargePercentage(u32* out);
Result psmGetChargerType(PsmChargerType* out);
Result psmIsBatteryChargingEnabled(bool* out);

typedef enum { TsLocation_Internal = 0, TsLocation_External = 1 } TsLocation;
typedef enum { TsDeviceCode_LocationInternal = 0x41000001u, TsDeviceCode_LocationExternal = 0x41000002u } TsDeviceCode;
typedef struct { int unused; } TsSession;
Result tsInitialize(void);  void tsExit(void);
Result tsGetTemperatureMilliC(TsLocation loc, s32* out);
Result tsOpenSession(TsSession* s, u32 device_code);
Result tsSessionGetTemperature(TsSession* s, float* out);
void   tsSessionClose(TsSession* s);

typedef struct { int unused; } FanController;
Result fanInitialize(void);  void fanExit(void);
Result fanOpenController(FanController* out, u32 device_code);
Result fanControllerGetRotationSpeedLevel(FanController* c, float* level);
void   fanControllerClose(FanController* c);

typedef enum { LblBacklightSwitchStatus_Disabled = 0, LblBacklightSwitchStatus_Enabled = 1 } LblBacklightSwitchStatus;
Result lblInitialize(void);  void lblExit(void);
Result lblGetCurrentBrightnessSetting(float* out);
Result lblGetBrightnessSettingAppliedToBacklight(float* out);
Result lblGetBacklightSwitchStatus(LblBacklightSwitchStatus* out);

// --- audout / nifm / csrng --------------------------------------------------
typedef enum { AudioOutState_Started = 0, AudioOutState_Stopped = 1 } AudioOutState;
Result audoutInitialize(void);  void audoutExit(void);
u32    audoutGetSampleRate(void);
u32    audoutGetChannelCount(void);
Result audoutListAudioOuts(char* names, s32 count, u32* out_count);
Result audoutGetAudioOutState(AudioOutState* out);

typedef enum { NifmServiceType_User = 0 } NifmServiceType;
typedef enum { NifmInternetConnectionType_WiFi = 1 } NifmInternetConnectionType;
typedef enum { NifmInternetConnectionStatus_Connected = 4 } NifmInternetConnectionStatus;
Result nifmInitialize(NifmServiceType t);  void nifmExit(void);
Result nifmGetCurrentIpAddress(u32* out);
Result nifmIsWirelessCommunicationEnabled(bool* out);
Result nifmIsEthernetCommunicationEnabled(bool* out);
Result nifmGetInternetConnectionStatus(NifmInternetConnectionType* t, u32* strength, NifmInternetConnectionStatus* s);

Result csrngInitialize(void);  void csrngExit(void);
Result csrngGetRandomBytes(void* out, size_t size);

// --- clkrst / pcv -----------------------------------------------------------
typedef enum {
    PcvModuleId_CpuBus = 0x40000001, PcvModuleId_GPU = 0x40000002, PcvModuleId_EMC = 0x40000039,
} PcvModuleId;
typedef enum { PcvModule_CpuBus = 0, PcvModule_GPU = 1 } PcvModule;
typedef enum { PcvClockRatesListType_Discrete = 0, PcvClockRatesListType_Range = 1 } PcvClockRatesListType;
typedef struct { int unused; } ClkrstSession;
Result clkrstInitialize(void);  void clkrstExit(void);
Result clkrstOpenSession(ClkrstSession* s, PcvModuleId module_id, u32 unk);
void   clkrstCloseSession(ClkrstSession* s);
Result clkrstGetClockRate(ClkrstSession* s, u32* out_hz);
Result clkrstGetPossibleClockRates(ClkrstSession* s, u32* rates, s32 max_count,
                                   PcvClockRatesListType* out_type, s32* out_count);
Result pcvInitialize(void);  void pcvExit(void);
Result pcvGetClockRate(PcvModule module, u32* out_hz);

// --- i2c / gpio -------------------------------------------------------------
typedef enum { I2cDevice_Tmp451 = 2 } I2cDevice;
typedef struct { int unused; } I2cSession;
Result i2cInitialize(void);  void i2cExit(void);
Result i2cOpenSession(I2cSession* out, I2cDevice dev);
void   i2csessionClose(I2cSession* s);

typedef enum { GpioPadName_SdCd = 14 } GpioPadName;
typedef enum { GpioDirection_Input = 0, GpioDirection_Output = 1 } GpioDirection;
typedef struct { int unused; } GpioPadSession;
Result gpioInitialize(void);  void gpioExit(void);
Result gpioOpenSession(GpioPadSession* out, GpioPadName name);
Result gpioPadGetDirection(GpioPadSession* p, GpioDirection* out);
void   gpioPadClose(GpioPadSession* p);

// --- account / ns / mii / pctl ----------------------------------------------
typedef enum { AccountServiceType_Application = 0 } AccountServiceType;
typedef struct { u64 uid[2]; } AccountUid;
Result accountInitialize(AccountServiceType t);  void accountExit(void);
Result accountGetUserCount(s32* out);
Result accountListAllUsers(AccountUid* uids, s32 max, s32* actual);

typedef enum { NcmStorageId_SdCard = 5 } NcmStorageId;
Result nsInitialize(void);  void nsExit(void);
Result nsGetTotalSpaceSize(NcmStorageId id, s64* size);
Result nsGetFreeSpaceSize(NcmStorageId id, s64* size);

typedef enum { MiiServiceType_System = 0, MiiServiceType_User = 1 } MiiServiceType;
typedef enum { MiiSpecialKeyCode_Normal = 0 } MiiSpecialKeyCode;
typedef enum { MiiSourceFlag_Database = 1 } MiiSourceFlag;
typedef struct { int unused; } MiiDatabase;
Result miiInitialize(MiiServiceType t);  void miiExit(void);
Result miiOpenDatabase(MiiDatabase* out, MiiSpecialKeyCode key);
Result miiDatabaseGetCount(MiiDatabase* db, s32* out, MiiSourceFlag flag);
void   miiDatabaseClose(MiiDatabase* db);

Result pctlInitialize(void);  void pctlExit(void);
Result pctlIsRestrictionEnabled(bool* out);

// --- console (only the unreachable framebuffer-failure fallback uses it) -----
void consoleInit(void* console);
void consoleUpdate(void* console);
void consoleExit(void* console);
