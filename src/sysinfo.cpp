#include "sysinfo.h"
#include <pdhmsg.h>
#include <vector>
#include <cwchar>

#pragma comment(lib, "pdh.lib")

// 实例名形如 pid_1234_luid_0x00000000_0x000143F2_phys_0_eng_0_engtype_3d
// 引擎类型字符串大小写不固定，必须不区分大小写比较
static bool ContainsNoCase(const wchar_t* s, const wchar_t* sub)
{
    const size_t n = wcslen(sub);
    if (n == 0) return true;
    for (; *s; ++s) {
        if (_wcsnicmp(s, sub, n) == 0) return true;
    }
    return false;
}

static bool Is3DEngine(const wchar_t* name)
{
    return ContainsNoCase(name, L"engtype_3d") ||
           ContainsNoCase(name, L"engtype_high priority 3d");
}

bool SysSampler::Init()
{
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        prevIdle_   = (ULONGLONG(idle.dwHighDateTime)   << 32) | idle.dwLowDateTime;
        prevKernel_ = (ULONGLONG(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
        prevUser_   = (ULONGLONG(user.dwHighDateTime)   << 32) | user.dwLowDateTime;
    }

    if (PdhOpenQueryW(nullptr, 0, &query_) == ERROR_SUCCESS) {
        // 任务管理器同源的 GPU 引擎计数器；按 3D 引擎统计
        if (PdhAddEnglishCounterW(query_, L"\\GPU Engine(*)\\Utilization Percentage", 0,
                                  &gpuCounter_) == ERROR_SUCCESS) {
            PdhCollectQueryData(query_);   // 首次采集作为基线
            gpuReady_ = true;
        }
    }
    return true;
}

void SysSampler::Shutdown()
{
    if (query_) { PdhCloseQuery(query_); query_ = nullptr; gpuCounter_ = nullptr; gpuReady_ = false; }
}

void SysSampler::Sample(SysInfo& out)
{
    // --- CPU：GetSystemTimes 差值 ---
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        ULONGLONG i = (ULONGLONG(idle.dwHighDateTime)   << 32) | idle.dwLowDateTime;
        ULONGLONG k = (ULONGLONG(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
        ULONGLONG u = (ULONGLONG(user.dwHighDateTime)   << 32) | user.dwLowDateTime;
        ULONGLONG di = i - prevIdle_;
        ULONGLONG dk = k - prevKernel_;
        ULONGLONG du = u - prevUser_;
        ULONGLONG total = dk + du;          // kernel 已包含 idle
        if (total > 0 && prevKernel_ != 0) {
            out.cpuPct = 100.0 * double(total - di) / double(total);
            if (out.cpuPct < 0.0) out.cpuPct = 0.0;
            if (out.cpuPct > 100.0) out.cpuPct = 100.0;
        }
        prevIdle_ = i; prevKernel_ = k; prevUser_ = u;
    }

    // --- 内存：物理内存使用百分比 ---
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) out.memPct = double(ms.dwMemoryLoad);

    // --- GPU：所有 3D 引擎里取最忙的那个 ---
    out.gpuValid = false;
    out.gpuPct = 0.0;
    if (gpuReady_ && PdhCollectQueryData(query_) == ERROR_SUCCESS) {
        DWORD bufSize = 0, itemCount = 0;
        PDH_STATUS st = PdhGetFormattedCounterArrayW(gpuCounter_, PDH_FMT_DOUBLE,
                                                     &bufSize, &itemCount, nullptr);
        if (st == PDH_MORE_DATA && bufSize > 0) {
            std::vector<BYTE> buf(bufSize);
            auto* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
            st = PdhGetFormattedCounterArrayW(gpuCounter_, PDH_FMT_DOUBLE,
                                              &bufSize, &itemCount, items);
            if (st == ERROR_SUCCESS) {
                double best = 0.0;
                for (DWORD i = 0; i < itemCount; ++i) {
                    if (!items[i].szName) continue;
                    if (!Is3DEngine(items[i].szName)) continue;
                    if (items[i].FmtValue.CStatus != ERROR_SUCCESS) continue;
                    if (items[i].FmtValue.doubleValue > best) best = items[i].FmtValue.doubleValue;
                }
                out.gpuPct = best;
                out.gpuValid = true;
            }
        }
    }
}
