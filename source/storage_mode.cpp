#include "storage_mode.hpp"
#include "bench.hpp"
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

void StorageMode::run() {
    mkdir(kDir, 0777);   // ensure the work directory exists

    // --- SD card capacity ------------------------------------------------
    {
        report::Section& s = report_.add("SD Card");
        struct statvfs vfs{};
        if (statvfs("sdmc:/", &vfs) == 0) {
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
    }

    // --- File write / read / verify round-trip --------------------------
    {
        report::Section& s = report_.add("File I/O Round-Trip");
        const size_t SZ = 4u << 20;        // 4 MiB test payload
        u8* wbuf = (u8*)malloc(SZ);
        u8* rbuf = (u8*)malloc(SZ);
        if (!wbuf || !rbuf) {
            s.missing("I/O buffers", "could not allocate 2x4 MiB");
        } else {
            for (size_t i = 0; i < SZ; i++) wbuf[i] = patternByte(i);

            // write
            bool wrote = false;
            double wsec = 0.0;
            FILE* f = fopen(kTestFile, "wb");
            if (!f) {
                s.missing("Open for write", "fopen(%s) failed", kTestFile);
            } else {
                u64 t0 = bench::tick();
                size_t n = fwrite(wbuf, 1, SZ, f);
                int fl = fflush(f);
                wsec = bench::since(t0);
                fclose(f);
                wrote = (n == SZ && fl == 0);
                s.check("Write 4 MiB", wrote, "%zu of %zu bytes written", n, SZ);
                if (wrote)
                    s.atLeast("Write throughput", SZ / wsec / 1e6, 0.5, "MB/s");
            }

            // size on disk
            if (wrote) {
                struct stat st{};
                if (stat(kTestFile, &st) == 0)
                    s.exact("File size on disk", (double)st.st_size,
                            (double)SZ, "bytes");
                else
                    s.missing("File size on disk", "stat() failed");
            }

            // read back
            bool readOk = false;
            if (wrote) {
                f = fopen(kTestFile, "rb");
                if (!f) {
                    s.missing("Open for read", "fopen(%s) failed", kTestFile);
                } else {
                    u64 t0 = bench::tick();
                    size_t n = fread(rbuf, 1, SZ, f);
                    double rsec = bench::since(t0);
                    fclose(f);
                    readOk = (n == SZ);
                    s.check("Read 4 MiB", readOk, "%zu of %zu bytes read", n, SZ);
                    if (readOk)
                        s.atLeast("Read throughput", SZ / rsec / 1e6, 1.0, "MB/s");
                }
            }

            // integrity
            if (readOk) {
                bool identical = memcmp(wbuf, rbuf, SZ) == 0;
                s.check("Data integrity", identical,
                        "%s", identical ? "read-back is byte-identical"
                                        : "MISMATCH - data corrupted on round-trip");
            }

            // cleanup
            int rm = remove(kTestFile);
            struct stat st{};
            s.check("Test file removed", rm == 0 && stat(kTestFile, &st) != 0,
                    "%s", rm == 0 ? "deleted" : "remove() failed");
        }
        free(wbuf);
        free(rbuf);
    }

    // --- Directory create / remove --------------------------------------
    {
        report::Section& s = report_.add("Directory Operations");
        rmdir(kTestDir);   // clear any stale leftover

        int mk = mkdir(kTestDir, 0777);
        struct stat st{};
        bool created = (mk == 0) && (stat(kTestDir, &st) == 0) && S_ISDIR(st.st_mode);
        s.check("Create directory", created,
                "%s", created ? "mkdir + stat confirm a directory"
                              : "mkdir failed");

        if (created) {
            int rd = rmdir(kTestDir);
            bool gone = (rd == 0) && (stat(kTestDir, &st) != 0);
            s.check("Remove directory", gone,
                    "%s", gone ? "rmdir succeeded, entry gone"
                               : "rmdir failed");
        }
    }

    // --- Path resolution -------------------------------------------------
    {
        report::Section& s = report_.add("Path Resolution");
        struct stat st{};
        s.check("sdmc: device mounted", stat("sdmc:/", &st) == 0,
                "%s", "SD root resolves via the libnx devoptab");
        // The current working directory is where the .nro was launched from.
        char cwd[FS_MAX_PATH];
        if (getcwd(cwd, sizeof(cwd)))
            s.check("Working directory", cwd[0] != 0, "%s", cwd);
        else
            s.missing("Working directory", "getcwd() failed");
    }
}
