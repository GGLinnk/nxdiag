#include "storage_mode.hpp"
#include "bench.hpp"
#include "debug.hpp"
#include <switch.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace {

const char* kDir      = "sdmc:/nxdiag";
const char* kTestFile = "sdmc:/nxdiag/selftest.bin";
const char* kTestDir  = "sdmc:/nxdiag/selftest_dir";

// Deterministic pattern so the read-back can be byte-verified.
u8 patternByte(size_t i) {
    return (u8)(((i * 2654435761u) ^ (i >> 3)) >> 16);
}

} // namespace

void StorageMode::seedSkeleton() {
    {
        report::Section& s = report_.add("SD Card");
        s.info("Total capacity",       "...");
        s.info("Free space available", "...");
        s.info("Free space sane",      "...");
    }
    {
        report::Section& s = report_.add("File I/O Round-Trip");
        s.info("Write 4 MiB",       "...");
        s.info("Write throughput",  "...");
        s.info("File size on disk", "...");
        s.info("Read 4 MiB",        "...");
        s.info("Read throughput",   "...");
        s.info("Data integrity",    "...");
        s.info("Test file removed", "...");
    }
    {
        report::Section& s = report_.add("Directory Operations");
        s.info("Create directory", "...");
        s.info("Remove directory", "...");
    }
    {
        report::Section& s = report_.add("Path Resolution");
        s.info("sdmc: device mounted", "...");
        s.info("Working directory",    "...");
    }
}

void StorageMode::run() {
    debug::log("[storage] run() begin");
    debug::log("[storage] mkdir(%s)...", kDir);
    int mkrc = mkdir(kDir, 0777);   // ensure the work directory exists
    debug::log("[storage] mkdir -> %d (errno may not apply if exists)", mkrc);

    // --- SD card capacity ------------------------------------------------
    {
        debug::log("[storage] SD Card: section begin");
        report::Section& s = report_.add("SD Card");
        debug::log("[storage]   statvfs(sdmc:/)...");
        struct statvfs vfs{};
        int rc = statvfs("sdmc:/", &vfs);
        debug::log("[storage]   statvfs -> %d (f_blocks=%llu f_bavail=%llu f_frsize=%llu)",
                   rc, (unsigned long long)vfs.f_blocks,
                   (unsigned long long)vfs.f_bavail,
                   (unsigned long long)vfs.f_frsize);
        if (rc == 0) {
            double blk   = (double)vfs.f_frsize;
            double total = blk * vfs.f_blocks;
            double avail = blk * vfs.f_bavail;
            s.atLeast("Total capacity", total / (1u << 30), 0.1, "GiB");
            s.check("Free space available", avail > 0,
                    "%.2f GiB free of %.2f GiB",
                    avail / (1u << 30), total / (1u << 30));
            s.check("Free space sane", avail <= total,
                    "%.1f%% used", total > 0 ? (total - avail) * 100.0 / total : 0.0);
        } else {
            s.missing("SD capacity", "statvfs(sdmc:/) failed - no SD card?");
        }
        debug::log("[storage] SD Card: section end");
    }

    // --- File write / read / verify round-trip --------------------------
    {
        debug::log("[storage] File I/O Round-Trip: section begin");
        report::Section& s = report_.add("File I/O Round-Trip");
        const size_t SZ = 4u << 20;        // 4 MiB test payload
        debug::log("[storage]   malloc 2x %zu bytes for I/O buffers...", SZ);
        u8* wbuf = (u8*)malloc(SZ);
        u8* rbuf = (u8*)malloc(SZ);
        debug::log("[storage]   wbuf=%p rbuf=%p", (void*)wbuf, (void*)rbuf);
        if (!wbuf || !rbuf) {
            s.missing("I/O buffers", "could not allocate 2x4 MiB");
        } else {
            for (size_t i = 0; i < SZ; i++) wbuf[i] = patternByte(i);

            // write
            debug::log("[storage]   fopen(%s, wb)...", kTestFile);
            bool wrote = false;
            double wsec = 0.0;
            FILE* f = fopen(kTestFile, "wb");
            debug::log("[storage]   fopen wb -> %p", (void*)f);
            if (!f) {
                s.missing("Open for write", "fopen(%s) failed", kTestFile);
            } else {
                debug::log("[storage]   fwrite %zu bytes...", SZ);
                u64 t0 = bench::tick();
                size_t n = fwrite(wbuf, 1, SZ, f);
                int fl = fflush(f);
                wsec = bench::since(t0);
                fclose(f);
                wrote = (n == SZ && fl == 0);
                debug::log("[storage]   fwrite -> %zu / %zu, fflush=%d, %.3fs",
                           n, SZ, fl, wsec);
                s.check("Write 4 MiB", wrote, "%zu of %zu bytes written", n, SZ);
                if (wrote)
                    s.atLeast("Write throughput", SZ / wsec / 1e6, 0.5, "MB/s");
            }

            // size on disk
            if (wrote) {
                debug::log("[storage]   stat(%s) for size check...", kTestFile);
                struct stat st{};
                int sr = stat(kTestFile, &st);
                debug::log("[storage]   stat -> %d size=%lld",
                           sr, (long long)st.st_size);
                if (sr == 0)
                    s.exact("File size on disk", (double)st.st_size,
                            (double)SZ, "bytes");
                else
                    s.missing("File size on disk", "stat() failed");
            }

            // read back
            bool readOk = false;
            if (wrote) {
                debug::log("[storage]   fopen(%s, rb)...", kTestFile);
                f = fopen(kTestFile, "rb");
                debug::log("[storage]   fopen rb -> %p", (void*)f);
                if (!f) {
                    s.missing("Open for read", "fopen(%s) failed", kTestFile);
                } else {
                    debug::log("[storage]   fread %zu bytes...", SZ);
                    u64 t0 = bench::tick();
                    size_t n = fread(rbuf, 1, SZ, f);
                    double rsec = bench::since(t0);
                    fclose(f);
                    readOk = (n == SZ);
                    debug::log("[storage]   fread -> %zu / %zu, %.3fs", n, SZ, rsec);
                    s.check("Read 4 MiB", readOk, "%zu of %zu bytes read", n, SZ);
                    if (readOk)
                        s.atLeast("Read throughput", SZ / rsec / 1e6, 1.0, "MB/s");
                }
            }

            // integrity
            if (readOk) {
                debug::log("[storage]   memcmp wbuf vs rbuf...");
                bool identical = memcmp(wbuf, rbuf, SZ) == 0;
                debug::log("[storage]   memcmp -> %s",
                           identical ? "identical" : "MISMATCH");
                s.check("Data integrity", identical,
                        "%s", identical ? "read-back is byte-identical"
                                        : "MISMATCH - data corrupted on round-trip");
            }

            // cleanup
            debug::log("[storage]   remove(%s)...", kTestFile);
            int rm = remove(kTestFile);
            struct stat st{};
            int sr = stat(kTestFile, &st);
            debug::log("[storage]   remove -> %d, stat-after -> %d", rm, sr);
            s.check("Test file removed", rm == 0 && sr != 0,
                    "%s", rm == 0 ? "deleted" : "remove() failed");
        }
        debug::log("[storage]   free wbuf/rbuf");
        free(wbuf);
        free(rbuf);
        debug::log("[storage] File I/O Round-Trip: section end");
    }

    // --- Directory create / remove --------------------------------------
    {
        debug::log("[storage] Directory Operations: section begin");
        report::Section& s = report_.add("Directory Operations");
        debug::log("[storage]   rmdir(%s) (cleanup stale)...", kTestDir);
        rmdir(kTestDir);   // clear any stale leftover

        debug::log("[storage]   mkdir(%s)...", kTestDir);
        int mk = mkdir(kTestDir, 0777);
        struct stat st{};
        int sr = stat(kTestDir, &st);
        debug::log("[storage]   mkdir -> %d, stat -> %d, isdir=%d",
                   mk, sr, (sr == 0 && S_ISDIR(st.st_mode)));
        bool created = (mk == 0) && (sr == 0) && S_ISDIR(st.st_mode);
        s.check("Create directory", created,
                "%s", created ? "mkdir + stat confirm a directory"
                              : "mkdir failed");

        if (created) {
            debug::log("[storage]   rmdir(%s)...", kTestDir);
            int rd = rmdir(kTestDir);
            int sr2 = stat(kTestDir, &st);
            debug::log("[storage]   rmdir -> %d, stat-after -> %d", rd, sr2);
            bool gone = (rd == 0) && (sr2 != 0);
            s.check("Remove directory", gone,
                    "%s", gone ? "rmdir succeeded, entry gone"
                               : "rmdir failed");
        }
        debug::log("[storage] Directory Operations: section end");
    }

    // --- Path resolution -------------------------------------------------
    {
        debug::log("[storage] Path Resolution: section begin");
        report::Section& s = report_.add("Path Resolution");
        debug::log("[storage]   stat(sdmc:/)...");
        struct stat st{};
        int sr = stat("sdmc:/", &st);
        debug::log("[storage]   stat(sdmc:/) -> %d", sr);
        s.check("sdmc: device mounted", sr == 0,
                "%s", "SD root resolves via the libnx devoptab");
        // The current working directory is where the .nro was launched from.
        debug::log("[storage]   getcwd...");
        char cwd[FS_MAX_PATH];
        const char* gc = getcwd(cwd, sizeof(cwd));
        debug::log("[storage]   getcwd -> %s", gc ? cwd : "(null)");
        if (gc)
            s.check("Working directory", cwd[0] != 0, "%s", cwd);
        else
            s.missing("Working directory", "getcwd() failed");
        debug::log("[storage] Path Resolution: section end");
    }
    debug::log("[storage] run() end");
}
