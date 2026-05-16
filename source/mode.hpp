#pragma once

// Screen indices into the Runner's view list. A plain enum, so the values
// convert directly to the int indices the nxd framework navigates by.
enum ModeId {
    Menu = 0,
    SysInfo,
    Cpu,
    Memory,
    Gpu,
    Storage,
    Kernel,
    Services,
    COUNT
};
