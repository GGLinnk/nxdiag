#include "report.hpp"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <sys/stat.h>
#include <json-c/json.h>

// Provided by the build (-DAPP_VERSION, from the Makefile's APP_VERSION);
// fall back so the file still compiles outside the project build.
#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

namespace report {

// All reports share one lock. A report is filled by a single background
// worker and read by the render thread, so contention is minimal; the lock
// only has to keep a vector from being resized mid-read.
static Mutex g_mtx;

// --- helpers ----------------------------------------------------------------

static std::string vformat(const char* fmt, va_list ap) {
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    return std::string(buf);
}

// Upsert an entry under the lock so a concurrent render sees the change
// atomically. Matching by `key` lets a probe overwrite the placeholder a
// seeded skeleton left in the same section. Entries with an empty key
// (continuation lines under a previous entry) always append.
static void pushEntry(std::vector<Entry>& v, const Entry& e) {
    mutexLock(&g_mtx);
    if (!e.key.empty()) {
        for (Entry& ex : v) {
            if (ex.key == e.key) {
                ex = e;
                mutexUnlock(&g_mtx);
                return;
            }
        }
    }
    v.push_back(e);
    mutexUnlock(&g_mtx);
}

// Compact human rendering of a number: drop the fraction for whole values.
static std::string fmtNum(double n, const char* unit) {
    char buf[80];
    if (n == (double)(long long)n && n < 1e15 && n > -1e15)
        snprintf(buf, sizeof(buf), "%lld%s%s", (long long)n,
                 (unit && *unit) ? " " : "", unit ? unit : "");
    else
        snprintf(buf, sizeof(buf), "%.3f%s%s", n,
                 (unit && *unit) ? " " : "", unit ? unit : "");
    return std::string(buf);
}

// --- Section entry builders -------------------------------------------------

void Section::info(const char* key, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::string v = vformat(fmt, ap);
    va_end(ap);
    pushEntry(entries, Entry{key, v, Status::Info, false, 0.0, ""});
}

void Section::text(const char* key, Status st, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::string v = vformat(fmt, ap);
    va_end(ap);
    pushEntry(entries, Entry{key, v, st, false, 0.0, ""});
}

void Section::num(const char* key, double n, const char* unit, Status st) {
    pushEntry(entries, Entry{key, fmtNum(n, unit), st, true, n, unit});
}

void Section::result(const char* key, Result rc) {
    Status st = R_SUCCEEDED(rc) ? Status::Good : Status::Bad;
    pushEntry(entries, Entry{key, describeResult(rc), st, false, 0.0, ""});
}

std::string describeResult(Result rc) {
    char buf[64];
    if (R_SUCCEEDED(rc))
        snprintf(buf, sizeof(buf), "0x%08X  ok", rc);
    else
        snprintf(buf, sizeof(buf), "0x%08X  module %u desc %u",
                 rc, R_MODULE(rc), R_DESCRIPTION(rc));
    return std::string(buf);
}

// --- assertion-style entries ------------------------------------------------

void Section::error(const char* key, Result rc) {
    std::string v = "FAILED  " + describeResult(rc);
    pushEntry(entries, Entry{key, v, Status::Bad, false, 0.0, ""});
}

void Section::missing(const char* key, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::string v = "UNAVAILABLE  " + vformat(fmt, ap);
    va_end(ap);
    pushEntry(entries, Entry{key, v, Status::Bad, false, 0.0, ""});
}

void Section::expect(const char* key, double n, double lo, double hi,
                     const char* unit) {
    bool ok = (n >= lo && n <= hi);
    char tail[96];
    snprintf(tail, sizeof(tail), "   (expect %s..%s)",
             fmtNum(lo, "").c_str(), fmtNum(hi, unit).c_str());
    Entry e{key, fmtNum(n, unit) + tail, ok ? Status::Good : Status::Bad,
            true, n, unit};
    pushEntry(entries, e);
}

void Section::atLeast(const char* key, double n, double floor, const char* unit) {
    bool ok = (n >= floor);
    char tail[96];
    snprintf(tail, sizeof(tail), "   (expect >= %s)", fmtNum(floor, unit).c_str());
    Entry e{key, fmtNum(n, unit) + tail, ok ? Status::Good : Status::Bad,
            true, n, unit};
    pushEntry(entries, e);
}

void Section::exact(const char* key, double n, double expected, const char* unit) {
    bool ok = (n == expected);
    char tail[96];
    snprintf(tail, sizeof(tail), "   (expect %s)", fmtNum(expected, unit).c_str());
    Entry e{key, fmtNum(n, unit) + tail, ok ? Status::Good : Status::Bad,
            true, n, unit};
    pushEntry(entries, e);
}

void Section::check(const char* key, bool pass, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::string v = vformat(fmt, ap);
    va_end(ap);
    pushEntry(entries, Entry{key, v, pass ? Status::Good : Status::Bad,
                            false, 0.0, ""});
}

// --- Report -----------------------------------------------------------------

Section& Report::add(const char* title) {
    mutexLock(&g_mtx);
    // Find-or-add by title: a seeded skeleton creates the section first, then
    // the real probe re-adds it and gets the same one back, overwriting its
    // placeholder entries through pushEntry's upsert.
    for (Section& sx : sections_) {
        if (sx.title == title) {
            mutexUnlock(&g_mtx);
            return sx;
        }
    }
    sections_.push_back(Section{});
    sections_.back().title = title;
    Section& ref = sections_.back();
    mutexUnlock(&g_mtx);
    return ref;
}

void Report::lock()   const { mutexLock(&g_mtx); }
void Report::unlock() const { mutexUnlock(&g_mtx); }

static const char* statusName(Status s) {
    switch (s) {
        case Status::Good: return "good";
        case Status::Warn: return "warn";
        case Status::Bad:  return "bad";
        default:           return "info";
    }
}

// Build the JSON array of a report's sections (each section an object with a
// title and an array of entry objects).
static json_object* sectionsArray(const std::vector<Section>& sections) {
    json_object* secs = json_object_new_array();
    for (const Section& s : sections) {
        json_object* so = json_object_new_object();
        json_object_object_add(so, "title", json_object_new_string(s.title.c_str()));

        json_object* ents = json_object_new_array();
        for (const Entry& e : s.entries) {
            json_object* eo = json_object_new_object();
            json_object_object_add(eo, "key",   json_object_new_string(e.key.c_str()));
            json_object_object_add(eo, "value", json_object_new_string(e.value.c_str()));
            if (e.numeric) {
                json_object_object_add(eo, "number", json_object_new_double(e.number));
                if (!e.unit.empty())
                    json_object_object_add(eo, "unit",
                                           json_object_new_string(e.unit.c_str()));
            }
            json_object_object_add(eo, "status",
                                   json_object_new_string(statusName(e.status)));
            json_object_array_add(ents, eo);
        }
        json_object_object_add(so, "entries", ents);
        json_object_array_add(secs, so);
    }
    return secs;
}

// Serialise a categorized report with json-c (switch-libjson-c). json-c handles
// string escaping and number formatting, so the export is well-formed by
// construction. Each category is one module's sections under a `module` label.
std::string toJson(const std::vector<Category>& categories) {
    json_object* root = json_object_new_object();
    json_object_object_add(root, "tool",    json_object_new_string("nxdiag"));
    json_object_object_add(root, "version", json_object_new_string(APP_VERSION));

    json_object* cats = json_object_new_array();
    for (const Category& c : categories) {
        json_object* co = json_object_new_object();
        json_object_object_add(co, "module",
                               json_object_new_string(c.module.c_str()));
        json_object_object_add(co, "sections",
                               sectionsArray(c.report.sections()));
        json_object_array_add(cats, co);
    }
    json_object_object_add(root, "categories", cats);

    const char* str = json_object_to_json_string_ext(
        root, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED);
    std::string out(str ? str : "{}");
    out += '\n';
    json_object_put(root);   // frees the whole tree
    return out;
}

Result writeJson(const char* path, const std::vector<Category>& categories) {
    // Best-effort: ensure the containing directory exists.
    std::string dir(path);
    size_t slash = dir.find_last_of('/');
    if (slash != std::string::npos) {
        dir.resize(slash);
        mkdir(dir.c_str(), 0777);
    }

    FILE* f = fopen(path, "wb");
    if (!f)
        return MAKERESULT(Module_Libnx, LibnxError_IoError);

    std::string json = toJson(categories);
    size_t written = fwrite(json.data(), 1, json.size(), f);
    int    flush    = fflush(f);
    fclose(f);

    if (written != json.size() || flush != 0)
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    return 0;
}

} // namespace report
