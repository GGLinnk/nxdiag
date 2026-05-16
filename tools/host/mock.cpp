// Host stub implementations for NX Diag's UI preview build. Every libnx call
// here returns dummy data - just enough for the modes to run, fill their
// reports and let the UI be previewed on a desktop.
#include "switch.h"
#include "deko3d.h"
#include "json-c/json.h"
#include <thread>
#include <chrono>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// --- threads (backed by std::thread) ----------------------------------------
Result threadCreate(Thread* t, ThreadFunc entry, void* arg, void*, size_t, int, int) {
    t->fn = entry; t->arg = arg; t->impl = nullptr; return 0;
}
Result threadStart(Thread* t) { t->impl = new std::thread(t->fn, t->arg); return 0; }
Result threadWaitForExit(Thread* t) {
    auto* th = static_cast<std::thread*>(t->impl);
    if (th && th->joinable()) th->join();
    return 0;
}
Result threadClose(Thread* t) {
    delete static_cast<std::thread*>(t->impl); t->impl = nullptr; return 0;
}
void svcSleepThread(s64 ns) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}

// One host mutex stands in for libnx's: NX Diag uses a single report lock.
static std::mutex g_hostMutex;
void mutexLock(Mutex*)   { g_hostMutex.lock(); }
void mutexUnlock(Mutex*) { g_hostMutex.unlock(); }

// --- svc / arm --------------------------------------------------------------
Result svcGetInfo(u64* out, u32 id, Handle, u64) {
    u64 v;
    switch (id) {
        case InfoType_CoreMask:                v = 0x7; break;
        case InfoType_TotalMemorySize:
        case InfoType_TotalNonSystemMemorySize: v = 0x1A000000; break;
        case InfoType_UsedMemorySize:
        case InfoType_UsedNonSystemMemorySize:  v = 0x10000000; break;
        case InfoType_HeapRegionSize:           v = 0x1A000000; break;
        case InfoType_AslrRegionSize:           v = 0x7FF8000000ull; break;
        case InfoType_AslrRegionAddress:        v = 0x8000000; break;
        case InfoType_ProgramId:                v = 0x010000000000100Dull; break;
        case InfoType_IsApplication:            v = 0; break;
        case InfoType_FreeThreadCount:          v = 0x40; break;
        default:                                v = (u64)id * 0x1000 + 0x80000000ull;
    }
    if (out) *out = v;
    return 0;
}
u32 svcGetCurrentProcessorNumber(void) { return 1; }
Result svcQueryMemory(MemoryInfo* mi, u32* pi, u64 addr) {
    static int n = 0;
    if (n >= 24) { n = 0; return MAKERESULT(1, 1); }   // end the walk
    n++;
    if (mi) {
        mi->addr = addr;
        mi->size = 0x200000;
        mi->type = (n == 1) ? MemType_CodeStatic
                 : (n % 5 == 0) ? MemType_Heap : MemType_Unmapped;
        mi->attr = mi->perm = mi->ipc_refcount = mi->device_refcount = mi->padding = 0;
    }
    if (pi) *pi = 0;
    return 0;
}
u64 svcGetSystemTick(void)      { return armGetSystemTick(); }
Result svcGetThreadPriority(s32* p, Handle) { if (p) *p = 0x2C; return 0; }
Result svcGetProcessId(u64* id, Handle)     { if (id) *id = 0x85; return 0; }
Result svcGetThreadId(u64* id, Handle)      { if (id) *id = 0x2A0; return 0; }
u64 armGetSystemTickFreq(void)  { return 19200000; }
void randomGet(void* buf, size_t len) {
    for (size_t i = 0; i < len; i++) static_cast<u8*>(buf)[i] = (u8)rand();
}

// --- env --------------------------------------------------------------------
bool   envIsNso(void)            { return false; }
bool   envHasArgv(void)          { return true; }
bool   envHasHeapOverride(void)  { return true; }
bool   envHasNextLoad(void)      { return true; }
bool   envIsSyscallHinted(unsigned) { return true; }
Handle envGetOwnProcessHandle(void) { return 0x4001; }
Handle envGetMainThreadHandle(void) { return 0x4002; }

// --- applet / apm -----------------------------------------------------------
AppletOperationMode appletGetOperationMode(void) { return AppletOperationMode_Handheld; }
ApmPerformanceMode  appletGetPerformanceMode(void) { return ApmPerformanceMode_Normal; }
Result apmInitialize(void) { return 0; }  void apmExit(void) {}
Result apmGetPerformanceMode(ApmPerformanceMode* o) { if (o) *o = ApmPerformanceMode_Normal; return 0; }
Result apmGetPerformanceConfiguration(ApmPerformanceMode, u32* o) { if (o) *o = 0x00020003; return 0; }

// --- setsys / set -----------------------------------------------------------
Result setsysInitialize(void) { return 0; }  void setsysExit(void) {}
Result setsysGetFirmwareVersion(SetSysFirmwareVersion* o) {
    if (o) { *o = SetSysFirmwareVersion{}; o->major = 20; o->minor = 0; o->micro = 0;
             o->revision_major = 1;
             strcpy(o->platform, "NX"); strcpy(o->display_version, "20.0.0-host");
             strcpy(o->version_hash, "0000000000000000hostmock"); }
    return 0;
}
Result setsysGetFirmwareVersionDigest(SetSysFirmwareVersionDigest* o) {
    if (o) strcpy(o->digest, "host-mock-digest");
    return 0;
}
Result setsysGetColorSetId(ColorSetId* o) { if (o) *o = ColorSetId_Dark; return 0; }
Result setsysGetSerialNumber(SetSysSerialNumber* o) { if (o) strcpy(o->number, "HOST0000000000"); return 0; }
Result setInitialize(void) { return 0; }  void setExit(void) {}
Result setGetRegionCode(SetRegion* o) { if (o) *o = SetRegion_EUR; return 0; }
Result setGetSystemLanguage(u64* o) { if (o) { char l[8] = "en-GB"; memcpy(o, l, 8); } return 0; }
Result setGetLanguageCode(u64* o)   { return setGetSystemLanguage(o); }

// --- spl --------------------------------------------------------------------
Result splInitialize(void) { return 0; }  void splExit(void) {}
Result splGetConfig(SplConfigItem item, u64* o) {
    if ((unsigned)item >= 65000) return MAKERESULT(26, 2);   // no Exosphere
    u64 v = 0;
    switch (item) {
        case SplConfigItem_HardwareType: v = 3; break;
        case SplConfigItem_IsRetail:     v = 1; break;
        case SplConfigItem_DeviceId:     v = 0x00C0FFEE0BADF00Dull; break;
        default:                         v = (u64)item;
    }
    if (o) *o = v;
    return 0;
}
Result splGetRandomBytes(void* o, size_t n) { randomGet(o, n); return 0; }
Result splIsDevelopment(bool* o) { if (o) *o = false; return 0; }

// --- psm / ts / fan / lbl ---------------------------------------------------
Result psmInitialize(void) { return 0; }  void psmExit(void) {}
Result psmGetBatteryChargePercentage(u32* o) { if (o) *o = 76; return 0; }
Result psmGetChargerType(PsmChargerType* o) { if (o) *o = PsmChargerType_EnoughPower; return 0; }
Result psmIsBatteryChargingEnabled(bool* o) { if (o) *o = true; return 0; }

Result tsInitialize(void) { return 0; }  void tsExit(void) {}
Result tsGetTemperatureMilliC(TsLocation, s32* o) { if (o) *o = 42000; return 0; }
Result tsOpenSession(TsSession*, u32) { return 0; }
Result tsSessionGetTemperature(TsSession*, float* o) { if (o) *o = 43.5f; return 0; }
void   tsSessionClose(TsSession*) {}

Result fanInitialize(void) { return 0; }  void fanExit(void) {}
Result fanOpenController(FanController*, u32) { return 0; }
Result fanControllerGetRotationSpeedLevel(FanController*, float* l) { if (l) *l = 0.30f; return 0; }
void   fanControllerClose(FanController*) {}

Result lblInitialize(void) { return 0; }  void lblExit(void) {}
Result lblGetCurrentBrightnessSetting(float* o) { if (o) *o = 0.65f; return 0; }
Result lblGetBrightnessSettingAppliedToBacklight(float* o) { if (o) *o = 0.66f; return 0; }
Result lblGetBacklightSwitchStatus(LblBacklightSwitchStatus* o) { if (o) *o = LblBacklightSwitchStatus_Enabled; return 0; }

// --- audout / nifm / csrng --------------------------------------------------
Result audoutInitialize(void) { return 0; }  void audoutExit(void) {}
u32 audoutGetSampleRate(void)   { return 48000; }
u32 audoutGetChannelCount(void) { return 2; }
Result audoutListAudioOuts(char* names, s32, u32* c) {
    if (names) strcpy(names, "DeviceOut");
    if (c) *c = 1;
    return 0;
}
Result audoutGetAudioOutState(AudioOutState* o) { if (o) *o = AudioOutState_Stopped; return 0; }

Result nifmInitialize(NifmServiceType) { return 0; }  void nifmExit(void) {}
Result nifmGetCurrentIpAddress(u32* o) { if (o) *o = 0x0F01A8C0; return 0; }   // 192.168.1.15
Result nifmIsWirelessCommunicationEnabled(bool* o) { if (o) *o = true; return 0; }
Result nifmIsEthernetCommunicationEnabled(bool* o) { if (o) *o = false; return 0; }
Result nifmGetInternetConnectionStatus(NifmInternetConnectionType* t, u32* s, NifmInternetConnectionStatus* st) {
    if (t) *t = NifmInternetConnectionType_WiFi;
    if (s) *s = 3;
    if (st) *st = NifmInternetConnectionStatus_Connected;
    return 0;
}

Result csrngInitialize(void) { return 0; }  void csrngExit(void) {}
Result csrngGetRandomBytes(void* o, size_t n) { randomGet(o, n); return 0; }

// --- clkrst / pcv -----------------------------------------------------------
Result clkrstInitialize(void) { return 0; }  void clkrstExit(void) {}
Result clkrstOpenSession(ClkrstSession*, PcvModuleId, u32) { return 0; }
void   clkrstCloseSession(ClkrstSession*) {}
Result clkrstGetClockRate(ClkrstSession*, u32* o) {
    if (o) *o = 1020000000u;   // ~1.02 GHz dummy
    return 0;
}
Result clkrstGetPossibleClockRates(ClkrstSession*, u32* rates, s32 max,
                                   PcvClockRatesListType* type, s32* count) {
    static const u32 kRates[] = { 204000000, 408000000, 1020000000, 1785000000 };
    s32 n = 4; if (n > max) n = max;
    for (s32 i = 0; i < n; i++) rates[i] = kRates[i];
    if (type) *type = PcvClockRatesListType_Discrete;
    if (count) *count = n;
    return 0;
}
Result pcvInitialize(void) { return 0; }  void pcvExit(void) {}
Result pcvGetClockRate(PcvModule, u32*) { return MAKERESULT(345, 37); }   // removed on 8.0.0+

// --- i2c / gpio -------------------------------------------------------------
Result i2cInitialize(void) { return 0; }  void i2cExit(void) {}
Result i2cOpenSession(I2cSession*, I2cDevice) { return 0; }
void   i2csessionClose(I2cSession*) {}
Result gpioInitialize(void) { return 0; }  void gpioExit(void) {}
Result gpioOpenSession(GpioPadSession*, GpioPadName) { return 0; }
Result gpioPadGetDirection(GpioPadSession*, GpioDirection* o) { if (o) *o = GpioDirection_Input; return 0; }
void   gpioPadClose(GpioPadSession*) {}

// --- account / ns / mii / pctl ----------------------------------------------
Result accountInitialize(AccountServiceType) { return 0; }  void accountExit(void) {}
Result accountGetUserCount(s32* o) { if (o) *o = 2; return 0; }
Result accountListAllUsers(AccountUid*, s32, s32* a) { if (a) *a = 2; return 0; }

Result nsInitialize(void) { return 0; }  void nsExit(void) {}
Result nsGetTotalSpaceSize(NcmStorageId, s64* s) { if (s) *s = 0x770000000ll; return 0; }
Result nsGetFreeSpaceSize(NcmStorageId, s64* s)  { if (s) *s = 0x300000000ll; return 0; }

Result miiInitialize(MiiServiceType) { return 0; }  void miiExit(void) {}
Result miiOpenDatabase(MiiDatabase*, MiiSpecialKeyCode) { return 0; }
Result miiDatabaseGetCount(MiiDatabase*, s32* o, MiiSourceFlag) { if (o) *o = 3; return 0; }
void   miiDatabaseClose(MiiDatabase*) {}

Result pctlInitialize(void) { return 0; }  void pctlExit(void) {}
Result pctlIsRestrictionEnabled(bool* o) { if (o) *o = false; return 0; }

// --- console (dead code on host: the framebuffer never fails) ---------------
void consoleInit(void*)   {}
void consoleUpdate(void*) {}
void consoleExit(void*)   {}

// --- deko3d -----------------------------------------------------------------
namespace {
struct HostMemBlock { void* ptr; };
int g_dkDevice;
}
DkDevice dkDeviceCreate(const DkDeviceMaker*) { return reinterpret_cast<DkDevice>(&g_dkDevice); }
void     dkDeviceDestroy(DkDevice) {}
uint64_t dkDeviceGetCurrentTimestamp(DkDevice)     { return armGetSystemTick(); }
uint64_t dkDeviceGetCurrentTimestampInNs(DkDevice) { return armGetSystemTick(); }
DkMemBlock dkMemBlockCreate(const DkMemBlockMaker* m) {
    auto* b = new HostMemBlock;
    b->ptr = malloc(m ? m->size : 0);
    return reinterpret_cast<DkMemBlock>(b);
}
void dkMemBlockDestroy(DkMemBlock obj) {
    auto* b = reinterpret_cast<HostMemBlock*>(obj);
    if (b) { free(b->ptr); delete b; }
}
void* dkMemBlockGetCpuAddr(DkMemBlock obj) {
    auto* b = reinterpret_cast<HostMemBlock*>(obj);
    return b ? b->ptr : nullptr;
}

// --- json-c (placeholder serialiser) ----------------------------------------
// json_object is opaque and the handles are never dereferenced, so a non-null
// sentinel is enough.
static json_object* kJsonHandle = reinterpret_cast<json_object*>(1);
json_object* json_object_new_object(void) { return kJsonHandle; }
json_object* json_object_new_array(void)  { return kJsonHandle; }
json_object* json_object_new_string(const char*) { return kJsonHandle; }
json_object* json_object_new_double(double)      { return kJsonHandle; }
int json_object_object_add(json_object*, const char*, json_object*) { return 0; }
int json_object_array_add(json_object*, json_object*) { return 0; }
const char* json_object_to_json_string_ext(json_object*, int) {
    return "{ \"tool\": \"nxdiag\", \"note\": \"host UI preview - no real data\" }";
}
int json_object_put(json_object*) { return 0; }
