#include "services_mode.hpp"
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
    TsSession sess;
    Result orc = tsOpenSession(&sess, deviceCode);
    if (R_SUCCEEDED(orc)) {
        float tc = 0.0f;
        Result grc = tsSessionGetTemperature(&sess, &tc);
        tsSessionClose(&sess);
        if (R_SUCCEEDED(grc)) s.expect(key, tc, 0.0, hi, "C");
        else                  s.error(key, grc);
        return;
    }
    s32 milli = 0;
    Result lrc = tsGetTemperatureMilliC(legacy, &milli);
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

void ServicesMode::run() {
    // --- Power & thermal -------------------------------------------------
    {
        report::Section& s = report_.add("Power & Thermal");

        Result rc = psmInitialize();
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

            bool charging = false;
            Result r3 = psmIsBatteryChargingEnabled(&charging);
            if (R_SUCCEEDED(r3)) s.check("psm: charging enabled", true,
                                         "%s", charging ? "yes" : "no");
            else                 s.error("psm: charging enabled", r3);
            psmExit();
        }

        rc = tsInitialize();
        s.result("ts: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            probeTemp(s, "ts: PCB temperature",
                      TsDeviceCode_LocationInternal, TsLocation_Internal, 90.0);
            probeTemp(s, "ts: SoC temperature",
                      TsDeviceCode_LocationExternal, TsLocation_External, 95.0);
            tsExit();
        }

        rc = fanInitialize();
        s.result("fan: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            FanController fan;
            Result orc = fanOpenController(&fan, 0x3D000001);  // cooler device
            if (R_SUCCEEDED(orc)) {
                float level = -1.0f;
                Result grc = fanControllerGetRotationSpeedLevel(&fan, &level);
                if (R_SUCCEEDED(grc))
                    s.expect("fan: rotation speed level", level, 0.0, 1.0, "");
                else
                    s.error("fan: rotation speed level", grc);
                fanControllerClose(&fan);
            } else {
                s.error("fan: open controller", orc);
            }
            fanExit();
        }

        rc = apmInitialize();
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
            apmExit();
        }
    }

    // --- Display & audio -------------------------------------------------
    {
        report::Section& s = report_.add("Display & Audio");

        Result rc = lblInitialize();
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
            lblExit();
        }

        rc = audoutInitialize();
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
            audoutExit();
        }
    }

    // --- Network ---------------------------------------------------------
    {
        report::Section& s = report_.add("Network");
        Result rc = nifmInitialize(NifmServiceType_User);
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
            nifmExit();
        }
    }

    // --- Crypto & random -------------------------------------------------
    {
        report::Section& s = report_.add("Crypto & Random");
        Result rc = csrngInitialize();
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
            csrngExit();
        }

        rc = splInitialize();
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
            splExit();
        }
    }

    // --- Hardware buses --------------------------------------------------
    {
        report::Section& s = report_.add("Hardware Buses");

        Result rc = i2cInitialize();
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
            i2cExit();
        }

        rc = gpioInitialize();
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
            gpioExit();
        }

        rc = clkrstInitialize();
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
            clkrstExit();
        }

        rc = pcvInitialize();
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
            pcvExit();
        }
    }

    // --- System & account -----------------------------------------------
    {
        report::Section& s = report_.add("System & Account");

        Result rc = accountInitialize(AccountServiceType_Application);
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
            accountExit();
        }

        rc = setsysInitialize();
        s.result("setsys: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            ColorSetId color = ColorSetId_Light;
            Result r1 = setsysGetColorSetId(&color);
            if (R_SUCCEEDED(r1)) s.check("setsys: colour set", true,
                                         "%s", color == ColorSetId_Dark ? "Dark"
                                                                         : "Light");
            else                 s.error("setsys: colour set", r1);
            setsysExit();
        }

        rc = setInitialize();
        s.result("set: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            SetRegion region = SetRegion_JPN;
            Result r1 = setGetRegionCode(&region);
            if (R_SUCCEEDED(r1)) s.check("set: region code", region <= SetRegion_CHN,
                                         "region %d", (int)region);
            else                 s.error("set: region code", r1);
            setExit();
        }

        rc = nsInitialize();
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
            nsExit();
        }

        rc = miiInitialize(MiiServiceType_User);
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
            miiExit();
        }

        rc = pctlInitialize();
        s.result("pctl: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            bool restricted = false;
            Result r1 = pctlIsRestrictionEnabled(&restricted);
            if (R_SUCCEEDED(r1)) s.check("pctl: restriction enabled", true,
                                         "%s", restricted ? "yes" : "no");
            else                 s.error("pctl: restriction enabled", r1);
            pctlExit();
        }
    }

    {
        report::Section& s = report_.add("Notes");
        s.info("What is tested", "each service is initialised, then real");
        s.info("", "functions are called on it and their results validated");
        s.info("Reading a failure", "a failed call is a valid observation - it");
        s.info("", "shows the call is out of reach for applet-context homebrew");
        s.info("Fingerprint", "the set of calls that succeed differs across");
        s.info("", "real hardware, CFW and emulators");
    }
}
