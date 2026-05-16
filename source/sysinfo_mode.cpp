#include "sysinfo_mode.hpp"
#include <switch.h>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {

void languageString(u64 code, char* out, size_t n) {
    char raw[9] = {};
    memcpy(raw, &code, 8);
    snprintf(out, n, "%s", raw[0] ? raw : "?");
}

const char* regionName(SetRegion r) {
    switch (r) {
        case SetRegion_JPN: return "Japan";
        case SetRegion_USA: return "Americas";
        case SetRegion_EUR: return "Europe";
        case SetRegion_AUS: return "Australia / NZ";
        case SetRegion_HTK: return "Hong Kong / Taiwan / Korea";
        case SetRegion_CHN: return "China";
        default:            return "unknown";
    }
}

const char* hardwareName(u64 t) {
    switch (t) {
        case 0: return "Icosa (Erista / launch Switch)";
        case 1: return "Copper (Erista dev/sim)";
        case 2: return "Hoag (Switch Lite)";
        case 3: return "Iowa (Mariko / Switch v2)";
        case 4: return "Calcio (Mariko dev/sim)";
        case 5: return "Aula (Switch OLED)";
        default: return "UNKNOWN - outside the documented 0..5 range";
    }
}

// One spl config item, always emitted: value on success, error otherwise.
void splItem(report::Section& s, const char* key, SplConfigItem item) {
    u64 v = 0;
    Result rc = splGetConfig(item, &v);
    if (R_SUCCEEDED(rc)) s.info(key, "%llu  (0x%llx)",
                                (unsigned long long)v, (unsigned long long)v);
    else                 s.error(key, rc);
}

} // namespace

void SysInfoMode::run() {
    // --- Firmware --------------------------------------------------------
    {
        report::Section& s = report_.add("Firmware");
        Result rc = setsysInitialize();
        s.result("setsys: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            SetSysFirmwareVersion fw{};
            Result frc = setsysGetFirmwareVersion(&fw);
            if (R_SUCCEEDED(frc)) {
                s.check("Display version present", fw.display_version[0] != 0,
                        "%s", fw.display_version[0] ? fw.display_version : "EMPTY");
                s.expect("Major version", fw.major, 1, 30, "");
                s.info("Version triplet", "%u.%u.%u", fw.major, fw.minor, fw.micro);
                s.info("Revision", "%u.%u", fw.revision_major, fw.revision_minor);
                s.check("Platform string", fw.platform[0] != 0,
                        "%s", fw.platform[0] ? fw.platform : "EMPTY");
                char hash[17] = {};
                memcpy(hash, fw.version_hash, 16);
                s.check("Version hash present", hash[0] != 0,
                        "%s", hash[0] ? hash : "EMPTY");
            } else {
                s.error("Firmware version", frc);
            }

            SetSysFirmwareVersionDigest digest{};
            Result drc = setsysGetFirmwareVersionDigest(&digest);
            if (R_SUCCEEDED(drc))
                s.check("Firmware digest present", digest.digest[0] != 0,
                        "%.32s", digest.digest);
            else
                s.error("Firmware digest", drc);

            ColorSetId color = ColorSetId_Light;
            Result crc = setsysGetColorSetId(&color);
            if (R_SUCCEEDED(crc))
                s.check("Theme colour set", color == ColorSetId_Light ||
                                            color == ColorSetId_Dark,
                        "%s", color == ColorSetId_Dark ? "Dark" : "Light");
            else
                s.error("Theme colour set", crc);

            SetSysSerialNumber serial{};
            Result src = setsysGetSerialNumber(&serial);
            if (R_SUCCEEDED(src))
                // A blank serial is normal on CFW (Atmosphere can hide it),
                // so flag it as a warning rather than a hard failure.
                s.text("Serial number",
                       serial.number[0] ? report::Status::Good : report::Status::Warn,
                       "%s", serial.number[0] ? serial.number
                                              : "blank (hidden by CFW)");
            else
                s.error("Serial number", src);

            setsysExit();
        }
    }

    // --- Hardware --------------------------------------------------------
    {
        report::Section& s = report_.add("Hardware");
        Result rc = splInitialize();
        s.result("spl: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            u64 hw = ~0ull;
            Result hrc = splGetConfig(SplConfigItem_HardwareType, &hw);
            if (R_SUCCEEDED(hrc)) {
                s.check("Hardware type", hw <= 5, "%llu - %s",
                        (unsigned long long)hw, hardwareName(hw));
            } else {
                s.error("Hardware type", hrc);
            }
            splItem(s, "New hardware type",   SplConfigItem_NewHardwareType);
            splItem(s, "Is retail",           SplConfigItem_IsRetail);
            splItem(s, "Is debug mode",       SplConfigItem_IsDebugMode);
            splItem(s, "Is recovery boot",    SplConfigItem_IsRecoveryBoot);
            splItem(s, "Is kiosk",            SplConfigItem_IsKiosk);
            splItem(s, "Boot reason",         SplConfigItem_BootReason);
            splItem(s, "DRAM id",             SplConfigItem_DramId);
            splItem(s, "Memory arrangement",  SplConfigItem_MemoryArrange);
            splItem(s, "Kernel memory cfg",   SplConfigItem_KernelMemoryConfiguration);

            u64 devId = 0;
            Result drc = splGetConfig(SplConfigItem_DeviceId, &devId);
            if (R_SUCCEEDED(drc))
                s.check("Device id", devId != 0, "0x%016llx",
                        (unsigned long long)devId);
            else
                s.error("Device id", drc);
            splExit();
        }
    }

    // --- CFW / loader ----------------------------------------------------
    {
        report::Section& s = report_.add("CFW / Loader");
        Result rc = splInitialize();
        if (R_SUCCEEDED(rc)) {
            u64 v = 0;
            Result erc = splGetConfig((SplConfigItem)65000, &v);   // ExosphereApiVersion
            if (R_SUCCEEDED(erc)) {
                u32 maj = (v >> 56) & 0xFF, min = (v >> 48) & 0xFF, mic = (v >> 40) & 0xFF;
                s.text("Custom firmware", report::Status::Info,
                       "Atmosphere %u.%u.%u detected", maj, min, mic);
                u64 hw = 0;
                if (R_SUCCEEDED(splGetConfig((SplConfigItem)65001, &hw)))
                    s.info("Exosphere target firmware", "0x%llx",
                           (unsigned long long)hw);
            } else {
                s.text("Custom firmware", report::Status::Info,
                       "not detected (stock firmware or emulator)");
            }
            splExit();
        } else {
            s.error("spl (for CFW probe)", rc);
        }
        s.check("Loaded as NRO", !envIsNso(), "%s",
                envIsNso() ? "NSO - installed title" : "NRO via homebrew loader");
        s.info("Has argv", "%s", envHasArgv() ? "yes" : "no");
        s.info("Heap override", "%s", envHasHeapOverride() ? "yes" : "no");
        s.info("Next-load capable", "%s", envHasNextLoad() ? "yes" : "no");
    }

    // --- Region & locale -------------------------------------------------
    {
        report::Section& s = report_.add("Region & Locale");
        Result rc = setInitialize();
        s.result("set: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            SetRegion region;
            Result rrc = setGetRegionCode(&region);
            if (R_SUCCEEDED(rrc))
                s.check("Region code", region >= SetRegion_JPN && region <= SetRegion_CHN,
                        "%d - %s", (int)region, regionName(region));
            else
                s.error("Region code", rrc);

            u64 lc = 0;
            char lang[16];
            Result lrc = setGetSystemLanguage(&lc);
            if (R_SUCCEEDED(lrc)) {
                languageString(lc, lang, sizeof(lang));
                s.check("System language", lang[0] != '?', "%s", lang);
            } else {
                s.error("System language", lrc);
            }
            Result crc = setGetLanguageCode(&lc);
            if (R_SUCCEEDED(crc)) {
                languageString(lc, lang, sizeof(lang));
                s.check("Language code", lang[0] != '?', "%s", lang);
            } else {
                s.error("Language code", crc);
            }
            setExit();
        }
    }

    // --- Power & performance --------------------------------------------
    {
        report::Section& s = report_.add("Power & Performance");
        AppletOperationMode op = appletGetOperationMode();
        s.check("Operation mode", op == AppletOperationMode_Handheld ||
                                  op == AppletOperationMode_Console,
                "%s", op == AppletOperationMode_Console ? "Console (docked)"
                                                        : "Handheld");

        ApmPerformanceMode pm = appletGetPerformanceMode();
        s.check("Applet performance mode", pm != ApmPerformanceMode_Invalid,
                "%s", pm == ApmPerformanceMode_Boost ? "Boost"
                    : pm == ApmPerformanceMode_Normal ? "Normal" : "INVALID");

        Result rc = apmInitialize();
        s.result("apm: initialize", rc);
        if (R_SUCCEEDED(rc)) {
            ApmPerformanceMode apm = ApmPerformanceMode_Invalid;
            Result arc = apmGetPerformanceMode(&apm);
            if (R_SUCCEEDED(arc))
                s.check("APM performance mode", apm != ApmPerformanceMode_Invalid,
                        "%s", apm == ApmPerformanceMode_Boost ? "Boost"
                            : apm == ApmPerformanceMode_Normal ? "Normal" : "INVALID");
            else
                s.error("APM performance mode", arc);
            apmExit();
        }
    }

    // --- Counters & time -------------------------------------------------
    {
        report::Section& s = report_.add("Counters & Time");
        u64 freq = armGetSystemTickFreq();
        s.exact("System tick frequency", (double)freq, 19200000.0, "Hz");

        // Two back-to-back reads execute faster than one ~52 ns tick, so spin
        // until the counter actually moves before asserting it advanced.
        u64 t0 = armGetSystemTick(), t1 = t0;
        for (int i = 0; i < 2000000 && t1 == t0; i++) t1 = armGetSystemTick();
        s.check("System tick advancing", t1 > t0, "monotonic counter is running");
        s.num("Process uptime", (double)armTicksToNs(armGetSystemTick()) / 1e9, "s");

        time_t now = time(nullptr);
        if (now > 0) {
            struct tm tmv;
            gmtime_r(&now, &tmv);
            char buf[40];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tmv);
            // A plausible wall clock is after 2017 (Switch launch year).
            s.check("Wall clock", tmv.tm_year + 1900 >= 2017, "%s", buf);
        } else {
            s.missing("Wall clock", "time() returned no value");
        }
    }
}
