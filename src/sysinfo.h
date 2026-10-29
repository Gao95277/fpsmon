// sysinfo.h - 系统占用率采样（全部从系统计数器读取，不接触游戏进程）
#pragma once
#include <windows.h>
#include <pdh.h>

struct SysInfo
{
    double cpuPct = 0.0;
    double gpuPct = 0.0;
    double memPct = 0.0;
    bool   gpuValid = false;
};

class SysSampler
{
public:
    bool Init();
    void Shutdown();
    void Sample(SysInfo& out);

private:
    ULONGLONG prevIdle_ = 0, prevKernel_ = 0, prevUser_ = 0;
    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER gpuCounter_ = nullptr;
    bool gpuReady_ = false;
};
