#include "services_mode.hpp"
#include "debug.hpp"
#include <switch.h>
#include <cstdio>
#include <cstring>

// Each service is not merely initialised: after init nxdiag actually calls
// representative functions on it (queries, session opens, reads) and validates
// what they return. Init only proves the service is reachable; the function
// calls prove it answers.

namespace {

// Read one temperature sensor. FW 14.0.0+ removed tsGetTemperature*, so use
// the session API first and fall back to the legacy call on old firmware.
void probeTemp(report::Section& s, const char* key, u32 deviceCode,
               TsLocation legacy, double hi) {
    debug::log("[services]   probeTemp(%s): tsOpenSession(0x%08X)...", key, deviceCode);
    TsSession sess;
    Result orc = tsOpenSession(&sess, deviceCode);
    debug::log("[services]   probeTemp(%s): tsOpenSession -> 0x%08X", key, orc);
    if (R_SUCCEEDED(orc)) {
        debug::log("[services]   probeTemp(%s): tsSessionGetTemperature...", key);
        float tc = 0.0f;
        Result grc = tsSessionGetTemperature(&sess, &tc);
        debug::log("[services]   probeTemp(%s): tsSessionGetTemperature -> rc=0x%08X t=%.2f C",
                   key, grc, tc);
        tsSessionClose(&sess);
        if (R_SUCCEEDED(grc)) s.expect(key, tc, 0.0, hi, "C");
        else                  s.error(key, grc);
        return;
    }
    debug::log("[services]   probeTemp(%s): falling back to tsGetTemperatureMilliC(legacy=%d)",
               key, (int)legacy);
    s32 milli = 0;
    Result lrc = tsGetTemperatureMilliC(legacy, &milli);
    debug::log("[services]   probeTemp(%s): tsGetTemperatureMilliC -> rc=0x%08X milli=%d",
               key, lrc, milli);
    if (R_SUCCEEDED(lrc)) s.expect(key, milli / 1000.0, 0.0, hi, "C");
    else                  s.error(key, lrc);
}

const char* chargerName(PsmChargerType t) {
    switch (t) {
        case PsmChargerType_Unconnected: return "none";
        case PsmChargerType_EnoughPower: return "full power";
        case PsmChargerType_LowPower:    return "low power (USB-PD)";
        case PsmChargerType_NotSupported:return "unsupported charger";
        default:                         return "unknown";
    }
}

} // namespace

void ServicesMode::seedSkeleton() {
    // The static "Notes" section is intentionally not seeded: it uses
    // empty-key continuation lines that must stay interleaved with their
    // named anchors, and it completes instantly at the end of run() anyway.
    {
        report::Section& s = report_.add("Power & Thermal");
        s.info("psm: initialize",          "...");
        s.info("psm: battery charge",      "...");
        s.info("psm: charger type",        "...");
        s.info("psm: charging enabled",    "...");
        s.info("ts: initialize",           "...");
        s.info("ts: PCB temperature",      "...");
        s.info("ts: SoC temperature",      "...");
        s.info("fan: initialize",          "...");
        s.info("fan: rotation speed level","...");
        s.info("apm: initialize",          "...");
        s.info("apm: performance mode",    "...");
        s.info("apm: normal-mode config",  "...");
    }
    {
        report::Section& s = report_.add("Display & Audio");
        s.info("lbl: initialize",          "...");
        s.info("lbl: brightness setting",  "...");
        s.info("lbl: applied brightness",  "...");
        s.info("lbl: backlight switch",    "...");
        s.info("audout: initialize",       "...");
        s.info("audout: sample rate",      "...");
        s.info("audout: channel count",    "...");
        s.info("audout: list outputs",     "...");
        s.info("audout: output state",     "...");
    }
    {
        report::Section& s = report_.add("Network");
        s.info("nifm: initialize",         "...");
        s.info("nifm: current IP",         "...");
        s.info("nifm: wireless enabled",   "...");
        s.info("nifm: ethernet enabled",   "...");
        s.info("nifm: connection status",  "...");
    }
    {
        report::Section& s = report_.add("Crypto & Random");
        s.info("csrng: initialize",   "...");
        s.info("csrng: draws differ", "...");
        s.info("spl: initialize",     "...");
        s.info("spl: getRandomBytes", "...");
        s.info("spl: isDevelopment",  "...");
    }
    {
        report::Section& s = report_.add("Hardware Buses");
        s.info("i2c: initialize",              "...");
        s.info("i2c: open Tmp451 session",     "...");
        s.info("gpio: initialize",             "...");
        s.info("gpio: SD-detect pad direction","...");
        s.info("clkrst: initialize",           "...");
        s.info("clkrst: CPU clock",            "...");
        s.info("pcv: initialize",              "...");
        s.info("pcv: CPU clock",               "...");
    }
    {
        report::Section& s = report_.add("System & Account");
        s.info("account: initialize",        "...");
        s.info("account: user count",        "...");
        s.info("account: list users",        "...");
        s.info("setsys: initialize",         "...");
        s.info("setsys: colour set",         "...");
        s.info("set: initialize",            "...");
        s.info("set: region code",           "...");
        s.info("ns: initialize",             "...");
        s.info("ns: SD total space",         "...");
        s.info("ns: SD free space",          "...");
        s.info("mii: initialize",            "...");
        s.info("mii: database count",        "...");
        s.info("pctl: initialize",           "...");
        s.info("pctl: restriction enabled",  "...");
    }
}

void ServicesMode::run() {
    debug::log("[services] run() begin");
    // --- Power & thermal -------------------------------------------------
    {
        debug::log("[services] Power & Thermal: section begin");
        report::Section& s = report_.add("Power & Thermal");

        debug::log("[services]   psmInitialize...");
        Result rc = psmInitialize();
        debug::log("[services]   psmInitialize -> 0x%08X", rc);
        s.result("psm: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            u32 pct = 0;
            Result r1 = psmGetBatteryChargePercentage(&pct);
            if (R_SUCCEEDED(r1)) s.expect("psm: battery charge", pct, 0, 100, "%");
            else                 s.error("psm: battery charge", r1);

            PsmChargerType ct = PsmChargerType_Unconnected;
            Result r2 = psmGetChargerType(&ct);
            if (R_SUCCEEDED(r2))
                s.check("psm: charger type", ct <= PsmChargerType_NotSupported,
                        "%s", chargerName(ct));
            else
                s.error("psm: charger type", r2);

            debug::log("[services]   psmIsBatteryChargingEnabled...");
            bool charging = false;
            Result r3 = psmIsBatteryChargingEnabled(&charging);
            debug::log("[services]   psmIsBatteryChargingEnabled -> rc=0x%08X v=%d",
                       r3, (int)charging);
            if (R_SUCCEEDED(r3)) s.check("psm: charging enabled", true,
                                         "%s", charging ? "yes" : "no");
            else                 s.error("psm: charging enabled", r3);
            debug::log("[services]   psmExit");
            psmExit();
        }

        debug::log("[services]   tsInitialize...");
        rc = tsInitialize();
        debug::log("[services]   tsInitialize -> 0x%08X", rc);
        s.result("ts: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            probeTemp(s, "ts: PCB temperature",
                      TsDeviceCode_LocationInternal, TsLocation_Internal, 90.0);
            probeTemp(s, "ts: SoC temperature",
                      TsDeviceCode_LocationExternal, TsLocation_External, 95.0);
            debug::log("[services]   tsExit");
            tsExit();
        }

        debug::log("[services]   fanInitialize...");
        rc = fanInitialize();
        debug::log("[services]   fanInitialize -> 0x%08X", rc);
        s.result("fan: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            debug::log("[services]   fanOpenController(0x3D000001)...");
            FanController fan;
            Result orc = fanOpenController(&fan, 0x3D000001);  // cooler device
            debug::log("[services]   fanOpenController -> 0x%08X", orc);
            if (R_SUCCEEDED(orc)) {
                debug::log("[services]   fanControllerGetRotationSpeedLevel...");
                float level = -1.0f;
                Result grc = fanControllerGetRotationSpeedLevel(&fan, &level);
                debug::log("[services]   fanControllerGetRotationSpeedLevel -> rc=0x%08X level=%.3f",
                           grc, level);
                if (R_SUCCEEDED(grc))
                    s.expect("fan: rotation speed level", level, 0.0, 1.0, "");
                else
                    s.error("fan: rotation speed level", grc);
                fanControllerClose(&fan);
            } else {
                s.error("fan: open controller", orc);
            }
            debug::log("[services]   fanExit");
            fanExit();
        }

        debug::log("[services]   apmInitialize...");
        rc = apmInitialize();
        debug::log("[services]   apmInitialize -> 0x%08X", rc);
        s.result("apm: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            ApmPerformanceMode pm = ApmPerformanceMode_Invalid;
            Result r1 = apmGetPerformanceMode(&pm);
            if (R_SUCCEEDED(r1))
                s.check("apm: performance mode", pm != ApmPerformanceMode_Invalid,
                        "%s", pm == ApmPerformanceMode_Boost ? "Boost"
                            : pm == ApmPerformanceMode_Normal ? "Normal" : "INVALID");
            else
                s.error("apm: performance mode", r1);

            u32 cfg = 0;
            Result r2 = apmGetPerformanceConfiguration(ApmPerformanceMode_Normal, &cfg);
            if (R_SUCCEEDED(r2)) s.check("apm: normal-mode config", cfg != 0,
                                         "0x%08X", cfg);
            else                 s.error("apm: normal-mode config", r2);
            debug::log("[services]   apmExit");
            apmExit();
        }
        debug::log("[services] Power & Thermal: section end");
    }

    // --- Display & audio -------------------------------------------------
    {
        debug::log("[services] Display & Audio: section begin");
        report::Section& s = report_.add("Display & Audio");

        debug::log("[services]   lblInitialize...");
        Result rc = lblInitialize();
        debug::log("[services]   lblInitialize -> 0x%08X", rc);
        s.result("lbl: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            float b = -1.0f;
            Result r1 = lblGetCurrentBrightnessSetting(&b);
            if (R_SUCCEEDED(r1)) s.expect("lbl: brightness setting",
                                          b * 100.0, 0.0, 100.0, "%");
            else                 s.error("lbl: brightness setting", r1);

            float ba = -1.0f;
            Result r2 = lblGetBrightnessSettingAppliedToBacklight(&ba);
            if (R_SUCCEEDED(r2)) s.expect("lbl: applied brightness",
                                          ba * 100.0, 0.0, 100.0, "%");
            else                 s.error("lbl: applied brightness", r2);

            LblBacklightSwitchStatus st = (LblBacklightSwitchStatus)0;
            Result r3 = lblGetBacklightSwitchStatus(&st);
            if (R_SUCCEEDED(r3)) s.check("lbl: backlight switch", true,
                                         "status %d", (int)st);
            else                 s.error("lbl: backlight switch", r3);
            debug::log("[services]   lblExit");
            lblExit();
        }

        debug::log("[services]   audoutInitialize...");
        rc = audoutInitialize();
        debug::log("[services]   audoutInitialize -> 0x%08X", rc);
        s.result("audout: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            s.exact("audout: sample rate", audoutGetSampleRate(), 48000.0, "Hz");
            s.atLeast("audout: channel count", audoutGetChannelCount(), 2, "");

            char names[8 * 0x100] = {};
            u32 count = 0;
            Result r1 = audoutListAudioOuts(names, 8, &count);
            if (R_SUCCEEDED(r1))
                s.check("audout: list outputs", count > 0,
                        "%lu output(s): %s", (unsigned long)count,
                        count ? names : "none");
            else
                s.error("audout: list outputs", r1);

            AudioOutState state = (AudioOutState)0;
            Result r2 = audoutGetAudioOutState(&state);
            if (R_SUCCEEDED(r2)) s.check("audout: output state", true,
                                         "state %d", (int)state);
            else                 s.error("audout: output state", r2);
            debug::log("[services]   audoutExit");
            audoutExit();
        }
        debug::log("[services] Display & Audio: section end");
    }

    // --- Network ---------------------------------------------------------
    {
        debug::log("[services] Network: section begin");
        report::Section& s = report_.add("Network");
        debug::log("[services]   nifmInitialize(NifmServiceType_User)...");
        Result rc = nifmInitialize(NifmServiceType_User);
        debug::log("[services]   nifmInitialize -> 0x%08X", rc);
        s.result("nifm: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            u32 ip = 0;
            Result r1 = nifmGetCurrentIpAddress(&ip);
            if (R_SUCCEEDED(r1))
                s.check("nifm: current IP", true, "%u.%u.%u.%u",
                        ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF,
                        (ip >> 24) & 0xFF);
            else
                s.error("nifm: current IP", r1);

            bool wifi = false;
            Result r2 = nifmIsWirelessCommunicationEnabled(&wifi);
            if (R_SUCCEEDED(r2)) s.check("nifm: wireless enabled", true,
                                         "%s", wifi ? "yes" : "no");
            else                 s.error("nifm: wireless enabled", r2);

            bool eth = false;
            Result r3 = nifmIsEthernetCommunicationEnabled(&eth);
            if (R_SUCCEEDED(r3)) s.check("nifm: ethernet enabled", true,
                                         "%s", eth ? "yes" : "no");
            else                 s.error("nifm: ethernet enabled", r3);

            NifmInternetConnectionType ct = (NifmInternetConnectionType)0;
            NifmInternetConnectionStatus cs = (NifmInternetConnectionStatus)0;
            u32 strength = 0;
            Result r4 = nifmGetInternetConnectionStatus(&ct, &strength, &cs);
            if (R_SUCCEEDED(r4)) s.check("nifm: connection status", true,
                                         "type %d, status %d, signal %lu",
                                         (int)ct, (int)cs, (unsigned long)strength);
            else                 s.error("nifm: connection status", r4);
            debug::log("[services]   nifmExit");
            nifmExit();
        }
        debug::log("[services] Network: section end");
    }

    // --- Crypto & random -------------------------------------------------
    {
        debug::log("[services] Crypto & Random: section begin");
        report::Section& s = report_.add("Crypto & Random");
        debug::log("[services]   csrngInitialize...");
        Result rc = csrngInitialize();
        debug::log("[services]   csrngInitialize -> 0x%08X", rc);
        s.result("csrng: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            u8 a[16] = {}, b[16] = {};
            Result r1 = csrngGetRandomBytes(a, sizeof(a));
            Result r2 = csrngGetRandomBytes(b, sizeof(b));
            if (R_SUCCEEDED(r1) && R_SUCCEEDED(r2)) {
                // Two draws from a real CSPRNG must not be identical.
                s.check("csrng: draws differ", memcmp(a, b, 16) != 0,
                        "%02X%02X%02X%02X.. vs %02X%02X%02X%02X..",
                        a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
            } else {
                s.error("csrng: getRandomBytes", R_FAILED(r1) ? r1 : r2);
            }
            debug::log("[services]   csrngExit");
            csrngExit();
        }

        debug::log("[services]   splInitialize...");
        rc = splInitialize();
        debug::log("[services]   splInitialize -> 0x%08X", rc);
        s.result("spl: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            u8 r[16] = {};
            Result r1 = splGetRandomBytes(r, sizeof(r));
            if (R_SUCCEEDED(r1)) {
                u64 v = 0; memcpy(&v, r, 8);
                s.check("spl: getRandomBytes", v != 0,
                        "%02X%02X%02X%02X%02X%02X%02X%02X",
                        r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
            } else {
                s.error("spl: getRandomBytes", r1);
            }
            bool dev = false;
            Result r2 = splIsDevelopment(&dev);
            if (R_SUCCEEDED(r2)) s.check("spl: isDevelopment", true,
                                         "%s", dev ? "development unit" : "retail");
            else                 s.error("spl: isDevelopment", r2);
            debug::log("[services]   splExit");
            splExit();
        }
        debug::log("[services] Crypto & Random: section end");
    }

    // --- Hardware buses --------------------------------------------------
    {
        debug::log("[services] Hardware Buses: section begin");
        report::Section& s = report_.add("Hardware Buses");

        debug::log("[services]   i2cInitialize...");
        Result rc = i2cInitialize();
        debug::log("[services]   i2cInitialize -> 0x%08X", rc);
        s.result("i2c: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            I2cSession sess;
            Result orc = i2cOpenSession(&sess, I2cDevice_Tmp451);  // temp sensor
            if (R_SUCCEEDED(orc)) {
                s.check("i2c: open Tmp451 session", true, "session opened");
                i2csessionClose(&sess);
            } else {
                s.error("i2c: open Tmp451 session", orc);
            }
            debug::log("[services]   i2cExit");
            i2cExit();
        }

        debug::log("[services]   gpioInitialize...");
        rc = gpioInitialize();
        debug::log("[services]   gpioInitialize -> 0x%08X", rc);
        s.result("gpio: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            GpioPadSession pad;
            Result orc = gpioOpenSession(&pad, GpioPadName_SdCd);  // SD card-detect
            if (R_SUCCEEDED(orc)) {
                GpioDirection dir = (GpioDirection)0;
                Result grc = gpioPadGetDirection(&pad, &dir);
                if (R_SUCCEEDED(grc))
                    s.check("gpio: SD-detect pad direction", true,
                            "direction %d", (int)dir);
                else
                    s.error("gpio: SD-detect pad direction", grc);
                gpioPadClose(&pad);
            } else {
                s.error("gpio: open SD-detect pad", orc);
            }
            debug::log("[services]   gpioExit");
            gpioExit();
        }

        debug::log("[services]   clkrstInitialize...");
        rc = clkrstInitialize();
        debug::log("[services]   clkrstInitialize -> 0x%08X", rc);
        s.result("clkrst: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            ClkrstSession sess;
            Result orc = clkrstOpenSession(&sess, PcvModuleId_CpuBus, 3);
            if (R_SUCCEEDED(orc)) {
                u32 hz = 0;
                Result grc = clkrstGetClockRate(&sess, &hz);
                if (R_SUCCEEDED(grc))
                    s.expect("clkrst: CPU clock", hz / 1.0e6, 70.0, 2500.0, "MHz");
                else
                    s.error("clkrst: CPU clock", grc);
                clkrstCloseSession(&sess);
            } else {
                s.error("clkrst: open CpuBus session", orc);
            }
            debug::log("[services]   clkrstExit");
            clkrstExit();
        }

        debug::log("[services]   pcvInitialize...");
        rc = pcvInitialize();
        debug::log("[services]   pcvInitialize -> 0x%08X", rc);
        s.result("pcv: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            u32 hz = 0;
            // pcvGetClockRate is removed on [8.0.0+]; a failure here is the
            // expected outcome on a modern firmware.
            Result grc = pcvGetClockRate(PcvModule_CpuBus, &hz);
            if (R_SUCCEEDED(grc))
                s.expect("pcv: CPU clock", hz / 1.0e6, 70.0, 2500.0, "MHz");
            else
                s.error("pcv: CPU clock (removed on 8.0.0+)", grc);
            debug::log("[services]   pcvExit");
            pcvExit();
        }
        debug::log("[services] Hardware Buses: section end");
    }

    // --- System & account -----------------------------------------------
    {
        debug::log("[services] System & Account: section begin");
        report::Section& s = report_.add("System & Account");

        debug::log("[services]   accountInitialize(Application)...");
        Result rc = accountInitialize(AccountServiceType_Application);
        debug::log("[services]   accountInitialize -> 0x%08X", rc);
        s.result("account: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            s32 users = -1;
            Result r1 = accountGetUserCount(&users);
            if (R_SUCCEEDED(r1)) s.expect("account: user count", users, 0, 8, "");
            else                 s.error("account: user count", r1);

            AccountUid uids[8] = {};
            s32 listed = 0;
            Result r2 = accountListAllUsers(uids, 8, &listed);
            if (R_SUCCEEDED(r2)) s.check("account: list users", true,
                                         "%d profile(s) enumerated", listed);
            else                 s.error("account: list users", r2);
            debug::log("[services]   accountExit");
            accountExit();
        }

        debug::log("[services]   setsysInitialize...");
        rc = setsysInitialize();
        debug::log("[services]   setsysInitialize -> 0x%08X", rc);
        s.result("setsys: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            ColorSetId color = ColorSetId_Light;
            Result r1 = setsysGetColorSetId(&color);
            if (R_SUCCEEDED(r1)) s.check("setsys: colour set", true,
                                         "%s", color == ColorSetId_Dark ? "Dark"
                                                                         : "Light");
            else                 s.error("setsys: colour set", r1);
            debug::log("[services]   setsysExit");
            setsysExit();
        }

        debug::log("[services]   setInitialize...");
        rc = setInitialize();
        debug::log("[services]   setInitialize -> 0x%08X", rc);
        s.result("set: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            SetRegion region = SetRegion_JPN;
            Result r1 = setGetRegionCode(&region);
            if (R_SUCCEEDED(r1)) s.check("set: region code", region <= SetRegion_CHN,
                                         "region %d", (int)region);
            else                 s.error("set: region code", r1);
            debug::log("[services]   setExit");
            setExit();
        }

        debug::log("[services]   nsInitialize...");
        rc = nsInitialize();
        debug::log("[services]   nsInitialize -> 0x%08X", rc);
        s.result("ns: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            s64 total = 0, freeSp = 0;
            Result r1 = nsGetTotalSpaceSize(NcmStorageId_SdCard, &total);
            if (R_SUCCEEDED(r1)) s.atLeast("ns: SD total space",
                                           total / (double)(1u << 30), 0.1, "GiB");
            else                 s.error("ns: SD total space", r1);

            Result r2 = nsGetFreeSpaceSize(NcmStorageId_SdCard, &freeSp);
            if (R_SUCCEEDED(r2)) s.atLeast("ns: SD free space",
                                           freeSp / (double)(1u << 20), 0.0, "MiB");
            else                 s.error("ns: SD free space", r2);
            debug::log("[services]   nsExit");
            nsExit();
        }

        debug::log("[services]   miiInitialize(User)...");
        rc = miiInitialize(MiiServiceType_User);
        debug::log("[services]   miiInitialize -> 0x%08X", rc);
        s.result("mii: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            MiiDatabase db;
            Result orc = miiOpenDatabase(&db, MiiSpecialKeyCode_Normal);
            if (R_SUCCEEDED(orc)) {
                s32 count = -1;
                Result grc = miiDatabaseGetCount(&db, &count, MiiSourceFlag_Database);
                if (R_SUCCEEDED(grc)) s.atLeast("mii: database count", count, 0, "");
                else                  s.error("mii: database count", grc);
                miiDatabaseClose(&db);
            } else {
                s.error("mii: open database", orc);
            }
            debug::log("[services]   miiExit");
            miiExit();
        }

        debug::log("[services]   pctlInitialize...");
        rc = pctlInitialize();
        debug::log("[services]   pctlInitialize -> 0x%08X", rc);
        s.result("pctl: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            bool restricted = false;
            Result r1 = pctlIsRestrictionEnabled(&restricted);
            if (R_SUCCEEDED(r1)) s.check("pctl: restriction enabled", true,
                                         "%s", restricted ? "yes" : "no");
            else                 s.error("pctl: restriction enabled", r1);
            debug::log("[services]   pctlExit");
            pctlExit();
        }
        debug::log("[services] System & Account: section end");
    }

    {
        debug::log("[services] Notes: section begin");
        report::Section& s = report_.add("Notes");
        s.info("What is tested", "each service is initialised, then real");
        s.info("", "functions are called on it and their results validated");
        s.info("Reading a failure", "a failed call is a valid observation - it");
        s.info("", "shows the call is out of reach for applet-context homebrew");
        s.info("Fingerprint", "the set of calls that succeed differs across");
        s.info("", "real hardware, CFW and emulators");
        debug::log("[services] Notes: section end");
    }
    debug::log("[services] run() end");
}
