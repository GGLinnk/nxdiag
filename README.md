# NX Diag

A Nintendo Switch homebrew **device diagnostics suite**, built with
devkitPro / libnx. It probes the CPU, memory, GPU, kernel and system services,
benchmarks the hardware and exports a structured JSON report.

It renders its UI in software via the libnx Framebuffer API - no RomFS or
external assets required - and drives the GPU through deko3d.

NX Diag is a companion to **Screen Tester 101**: that tool checks the panel,
touchscreen and controllers; this one checks everything behind the glass.

## Demo

https://github.com/user-attachments/assets/203fd43c-abff-4d2a-a470-1408add212b7
> A full walk-through of every diagnostic module - opening each one, watching
> it probe, scrolling the report and running the run-all JSON export - rendered
> off-device by the host renderer (see `tools/host`).

## Purpose

- **Probe the device featureset** - exercise libnx, the Nintendo system
  services and the kernel SVC surface, and record exactly what answers.
- **Benchmark the hardware** - integer / floating-point / NEON throughput,
  multi-core scaling, memory bandwidth and latency, GPU bring-up.
- **Compare against an emulator** - every run can export `report.json`. Run
  NX Diag on real hardware and on an emulator (yuzu, Ryujinx, …) and diff the
  two reports: differing values, missing services or wrong Result codes are
  emulator-fidelity bugs.

## Modules

| Module | What it probes |
|---|---|
| **System Info** | Firmware version, hardware model / SoC codename, region & locale, performance mode, CFW (Atmosphère) detection, system tick frequency |
| **CPU** | Core mask & topology, clkrst clocks, integer / double / NEON benchmarks, per-core throughput, multi-core scaling factor, counter resolution |
| **Memory** | `svcGetInfo` region sizes, address-space layout, `svcQueryMemory` virtual-memory map walk, copy/read/write bandwidth, cache-latency curve, heap allocation limit |
| **GPU** | deko3d device bring-up timing, GPU memory allocation, CPU→GPU-memory write bandwidth, GPU timestamp clock, GPU / EMC clock rates |
| **Storage** | SD-card capacity, a 4 MiB file write/read/**verify** round-trip, measured I/O throughput, directory create/remove, path resolution |
| **Kernel / SVC** | Full `svcGetInfo` InfoType sweep, process / thread identity, system tick behaviour, libnx environment (NRO/NSO, hbloader), entropy sources |
| **Services** | Reachability probe of `psm`, `ts`, `fan`, `apm`, `lbl`, `audout`, `nifm`, `csrng`, `spl`, `i2c`, `gpio`, `clkrst`, `pcv`, `account`, `setsys`, `set`, `ns`, `mii`, `pctl` - init Result code plus a validated sample reading per service |

## How probes report

Every probe behaves like an **assertion**, not just a reading. It always emits
an entry, and that entry always carries a verdict:

- **green** - the value was read *and* it meets its expectation (in range,
  exact match, consistent with another reading);
- **red** - the query failed (`FAILED 0x… module/desc`), the value is missing
  (`UNAVAILABLE …`) or it falls outside the expected range / fails a check;
- **neutral** - a raw reading with no fixed expectation (an address, an id).

So a missing or wrong value is never silently absent - it shows up red. The
report reads like a unit-test run executed from inside the device.

## Controls

| Input | Action |
|---|---|
| D-pad + `A` | (Menu) select / open a module |
| `A` | (Module) re-run the probes / benchmarks |
| `↑` / `↓` | Scroll the report (accelerates while held) |
| `L` / `R` | Scroll the report one page |
| Left stick | Proportional continuous scroll |
| Touch drag | Grab and drag the report |
| `ZL` / `ZR` | Cycle between modules |
| `B` | Return to the menu |
| `+` | Exit to hbmenu |

Probes and benchmarks run on a **background thread**: the UI stays at full
frame rate and remains scrollable while a module is measuring.

## JSON export

From the menu, **Run all tests & export JSON** probes every module and writes
the combined report to:

```
sdmc:/nxdiag/report.json
```

The report is serialised with **json-c** (the `switch-libjson-c` portlib). It
is a flat list of sections and entries; numeric entries keep a real `number`
(and `unit`) field, and every entry carries its `status`, so two runs can be
diffed mechanically:

```json
{
  "tool": "nxdiag",
  "version": "1.0.0",
  "sections": [
    { "title": "Single-Core Throughput", "entries": [
      { "key": "Integer", "value": "3210 Mops/s", "number": 3210.4,
        "unit": "Mops/s", "status": "good" }
    ]}
  ]
}
```

## Building

Requires devkitPro with the `switch-dev` group (`devkitA64` + `libnx`) and the
`switch-libjson-c` portlib. deko3d ships with libnx. The shared **nxdisplaylib**
library is a git submodule at `libs/nxdisplaylib`.

```sh
# clone with the nxdisplaylib submodule (or: git submodule update --init):
git clone --recursive <repo-url>

# one-time, if json-c is not already installed:
sudo dkp-pacman -S switch-libjson-c

DEVKITPRO=/opt/devkitpro make
```

This produces `nxdiag.nro`. `make clean` removes build artifacts.
(`DEVKITPRO` is only needed if it is not already exported in your environment.)

## Running

Copy `nxdiag.nro` to the `/switch/` folder of your SD card and launch it from
the homebrew menu, or load the `.nro` in a Switch emulator.

> Each module probes on a background worker thread; an animated "running"
> overlay is shown while it measures, and the menu's run-all does the same so
> the UI never stalls.

## Project layout

gfx + font come from the shared **nxdisplaylib** library - common ground with
Screen Tester 101 - pulled in as a git submodule.

```
libs/nxdisplaylib/           shared display library (gfx + font, submodule)
source/
  main.cpp                entrypoint + framebuffer-failure fallback
  app.{hpp,cpp}           main loop, module dispatch, chrome
  mode.hpp                abstract Mode interface + Input struct
  probe_mode.{hpp,cpp}    base for probe modules: run scheduling + redraw
  report.{hpp,cpp}        report tree + assertion helpers + json-c export
  ui.{hpp,cpp}            scrollable report view widget
  bench.hpp               system-counter timing helpers
  menu_mode.{hpp,cpp}     landing menu + incremental run-all / export
  sysinfo_mode.{hpp,cpp}  firmware / hardware / region probes
  cpu_mode.{hpp,cpp}      CPU topology, clocks and benchmarks
  mem_mode.{hpp,cpp}      memory regions, VM map, bandwidth, latency
  gpu_mode.{hpp,cpp}      deko3d bring-up, GPU memory, clocks
  storage_mode.{hpp,cpp}  SD-card capacity + file I/O round-trip test
  kernel_mode.{hpp,cpp}   svcGetInfo sweep, process/thread, entropy
  services_mode.{hpp,cpp} system-service reachability probe
tools/
  make_icon.py            regenerates the homebrew icon
```
