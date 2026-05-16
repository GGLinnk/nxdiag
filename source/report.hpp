#pragma once
#include <switch.h>
#include <string>
#include <vector>

// A diagnostic report: an ordered list of sections, each an ordered list of
// key/value entries. Every probe module fills one report; the export groups
// the modules' reports by category and serialises the lot to JSON on the SD
// card so an on-device run can be diffed against an emulator run.
namespace report {

// Colour-coded verdict for an entry. Info is neutral (a raw reading);
// Good/Warn/Bad flag a probe result against an expectation.
enum class Status : u8 { Info, Good, Warn, Bad };

struct Entry {
    std::string key;
    std::string value;        // human-readable rendering, always present
    Status      status = Status::Info;
    bool        numeric = false;  // when true JSON emits `number` (+ `unit`)
    double      number  = 0.0;
    std::string unit;
};

struct Section {
    std::string title;
    std::vector<Entry> entries;

    // printf-style string entry (neutral Info status).
    void info(const char* key, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
    // printf-style string entry with an explicit verdict.
    void text(const char* key, Status st, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
    // Numeric entry: kept as a real number in JSON so it can be diffed.
    void num(const char* key, double n, const char* unit, Status st = Status::Info);
    // A libnx Result: prints "0x….  module/description", verdict Good/Bad.
    void result(const char* key, Result rc);

    // --- assertion-style entries ----------------------------------------
    // Each of these always emits exactly one entry carrying a verdict, so a
    // failed or out-of-range probe is visible rather than silently absent.

    // A query that failed: an explicit Bad entry carrying the Result code.
    void error(const char* key, Result rc);
    // Unavailable for a non-Result reason: an explicit Bad entry.
    void missing(const char* key, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
    // Numeric reading checked against an inclusive [lo,hi] range.
    void expect(const char* key, double n, double lo, double hi, const char* unit);
    // Numeric reading that must be greater than or equal to `floor`.
    void atLeast(const char* key, double n, double floor, const char* unit);
    // Numeric reading that must equal `expected` exactly.
    void exact(const char* key, double n, double expected, const char* unit);
    // Boolean assertion: pass -> Good, fail -> Bad; fmt describes the state.
    void check(const char* key, bool pass, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
};

class Report {
public:
    Section& add(const char* title);     // append and return a fresh section
    void clear() { sections_.clear(); }
    bool empty() const { return sections_.empty(); }
    const std::vector<Section>& sections() const { return sections_; }

    // A probe worker fills the report while the UI renders it. The Section
    // builders take an internal lock for each mutation; the renderer brackets
    // its read with lock()/unlock() so it always sees a consistent snapshot.
    void lock()   const;
    void unlock() const;

private:
    std::vector<Section> sections_;
};

// One module's contribution to the categorized export: a name and its report.
struct Category {
    std::string module;   // module name, e.g. "CPU"
    Report      report;   // that module's sections
};

// Serialise a categorized report (sections grouped by module) to a JSON
// document, and to a file. writeJson creates parent directories; its Result
// is 0 on success, or a libnx fs/errno-wrapped failure code.
std::string toJson(const std::vector<Category>& categories);
Result      writeJson(const char* path, const std::vector<Category>& categories);

// "0x01A8 (module 2, desc 3)" style rendering of a libnx Result.
std::string describeResult(Result rc);

} // namespace report
