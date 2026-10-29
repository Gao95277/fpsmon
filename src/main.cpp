// main.cpp - 入口：托盘常驻 + 定时刷新 + 各模块协调
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <vector>
#include <map>
#include <string>
#include <algorithm>

#include "overlay.h"
#include "frame_source.h"
#include "stats.h"
#include "sysinfo.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

#define WM_TRAYMSG      (WM_APP + 1)
#define IDM_TOGGLE      1001
#define IDM_CORNER_BASE 1010
#define IDM_AUTOSTART   1020
#define IDM_EXIT        1099
#define TIMER_TICK_ID   1
#define TICK_MS         200

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunName = L"FpsMon";

static bool IsAutoStart()
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    wchar_t buf[1024];
    DWORD sz = sizeof(buf);
    bool on = (RegQueryValueExW(k, kRunName, nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS);
    RegCloseKey(k);
    return on;
}

static void SetAutoStart(bool on)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring quoted = L"\"" + std::wstring(path) + L"\"";
        RegSetValueExW(k, kRunName, 0, REG_SZ, (const BYTE*)quoted.c_str(),
                       (DWORD)((quoted.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, kRunName);
    }
    RegCloseKey(k);
}

static Overlay      g_overlay;
static FrameStats   g_stats;
static SysSampler   g_sys;
static SysInfo      g_sysInfo;
static std::vector<FrameEvent> g_events;
static DWORD        g_lastFgPid = 0;
static int          g_tick = 0;
static bool         g_debug = false;
static bool         g_followAny = false;   // 调试用：不跟随前台，改为跟随帧数最多的进程
static NOTIFYICONDATAW g_nid{};

// 每条日志独立开关文件，保证运行中随时可读
static void LogLine(const char* s)
{
    FILE* f = nullptr;
    if (fopen_s(&f, "fpsmon.log", "a") == 0 && f) { fputs(s, f); fclose(f); }
}

static DWORD ForegroundPid()
{
    HWND fg = GetForegroundWindow();
    if (!fg) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid;
}

static void AddTrayIcon(HWND hwnd)
{
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYMSG;
    g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"FPS 监控");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void RemoveTrayIcon()
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

static void ShowTrayMenu(HWND hwnd)
{
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_TOGGLE, g_overlay.Visible() ? L"隐藏显示层" : L"显示显示层");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU sub = CreatePopupMenu();
    const wchar_t* names[2] = { L"屏幕顶端", L"屏幕底端" };
    for (int i = 0; i < 2; ++i) {
        UINT flags = MF_STRING | (g_overlay.Position() == i ? MF_CHECKED : MF_UNCHECKED);
        AppendMenuW(sub, flags, IDM_CORNER_BASE + i, names[i]);
    }
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)sub, L"位置");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (IsAutoStart() ? MF_CHECKED : MF_UNCHECKED),
                IDM_AUTOSTART, L"开机自动启动");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (cmd == IDM_TOGGLE) {
        g_overlay.ToggleVisible();
    } else if (cmd >= IDM_CORNER_BASE && cmd < IDM_CORNER_BASE + 2) {
        g_overlay.SetPosition(cmd - IDM_CORNER_BASE);
    } else if (cmd == IDM_AUTOSTART) {
        SetAutoStart(!IsAutoStart());
    } else if (cmd == IDM_EXIT) {
        PostQuitMessage(0);
    }
}

static void OnTick()
{
    g_events.clear();
    FrameSource::Instance().Drain(g_events);

    DWORD fg = g_followAny ? 0 : ForegroundPid();
    if (g_followAny) {
        std::map<DWORD, int> dist;
        for (auto& e : g_events) dist[e.pid]++;
        int best = 0;
        for (auto& kv : dist) if (kv.second > best) { best = kv.second; fg = kv.first; }
        // 事件是按批到达的，本 tick 没有新事件时沿用上一个目标，避免误判成"切换了窗口"
        if (fg == 0) fg = g_lastFgPid;
    }
    if (fg != g_lastFgPid) {          // 切换窗口即清空统计，避免把上一个程序的数据带过来
        g_stats.Reset();
        g_lastFgPid = fg;
    }
    for (auto& e : g_events) {
        if (e.pid == fg) g_stats.AddFrame(e.pid, e.qpc);
    }

    if (g_tick % 5 == 0) g_sys.Sample(g_sysInfo);
    ++g_tick;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    StatsSnapshot s = g_stats.Compute(now.QuadPart);

    OverlayValues v;
    v.hasFrame = s.valid;
    v.fps = s.fps;
    v.low1 = s.low1;
    v.cpu = g_sysInfo.cpuPct;
    v.gpu = g_sysInfo.gpuPct;
    v.mem = g_sysInfo.memPct;
    v.gpuValid = g_sysInfo.gpuValid;
    g_overlay.Render(v);

    if (g_debug) {
        char buf[512];
        if (s.valid) {
            snprintf(buf, sizeof(buf),
                     "pid=%lu recv=%zu held=%zu  fps=%.1f  low1=%.1f  frameMs=%.2f  win=%.1fs  cpu=%.0f  gpu=%.0f  mem=%.0f\n",
                     (unsigned long)fg, g_events.size(), s.sampleCount,
                     s.fps, s.low1, s.frameMs, s.windowSec, v.cpu, v.gpu, v.mem);
        } else {
            snprintf(buf, sizeof(buf), "pid=%lu recv=%zu  --  cpu=%.0f  gpu=%.0f  mem=%.0f  pt=%ld  raw=%lld\n",
                     (unsigned long)fg, g_events.size(), v.cpu, v.gpu, v.mem,
                     FrameSource::Instance().ProcTraceRc(),
                     FrameSource::Instance().RawEvents());
        }
        LogLine(buf);

        if (g_followAny) {
            std::map<DWORD, int> dist;
            for (auto& e : g_events) dist[e.pid]++;
            std::vector<std::pair<int, DWORD>> top;
            for (auto& kv : dist) top.push_back({ kv.second, kv.first });
            std::sort(top.rbegin(), top.rend());
            char b2[512] = { 0 };
            int off = snprintf(b2, sizeof(b2), "    dist:");
            for (size_t i = 0; i < top.size() && i < 4; ++i)
                off += snprintf(b2 + off, sizeof(b2) - off, " %lu:%d", (unsigned long)top[i].second, top[i].first);
            snprintf(b2 + off, sizeof(b2) - off, "\n");
            LogLine(b2);
        }
    }
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR cmdLine, int)
{
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"FpsMon_SingleInstance_v1");
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    if (cmdLine && wcsstr(cmdLine, L"--debug")) {
        g_debug = true;
        FILE* f = nullptr;
        if (fopen_s(&f, "fpsmon.log", "w") == 0 && f) fclose(f);   // 清空旧日志
    }
    if (cmdLine && wcsstr(cmdLine, L"--follow-any")) g_followAny = true;

    if (!g_overlay.Create(hInst)) return 1;
    HWND hwnd = g_overlay.Hwnd();

    std::wstring err;
    bool started = FrameSource::Instance().Start(err);
    g_stats.SetQpcFreq(FrameSource::Instance().QpcFreq());
    g_stats.Reset();
    g_sys.Init();

    if (g_debug) {
        char buf[512];
        snprintf(buf, sizeof(buf), "ETW: %ls (filtered=%d, qpf=%lld, procTrace=%ld)\n",
                 started ? L"running" : err.c_str(),
                 (int)FrameSource::Instance().Filtered(),
                 (long long)FrameSource::Instance().QpcFreq(),
                 FrameSource::Instance().ProcTraceRc());
        LogLine(buf);
    } else if (!started) {
        std::wstring msg = L"无法从系统事件流读取帧数据，FPS 会一直显示 --。\n\n原因：";
        msg += err;
        msg += L"\n\n如果这是权限问题，请退出后右键「以管理员身份运行」。\nCPU / GPU / 内存占用不受影响，仍会正常显示。";
        MessageBoxW(nullptr, msg.c_str(), L"FPS 监控", MB_ICONWARNING | MB_OK);
    }

    AddTrayIcon(hwnd);
    SetTimer(hwnd, TIMER_TICK_ID, TICK_MS, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        // 显示层与托盘的消息在主循环里直接处理，不进入窗口过程
        if (msg.hwnd == hwnd && msg.message == WM_TIMER && msg.wParam == TIMER_TICK_ID) {
            OnTick();
            continue;
        }
        if (msg.hwnd == hwnd && msg.message == WM_TRAYMSG) {
            if (msg.lParam == WM_RBUTTONUP || msg.lParam == WM_CONTEXTMENU) ShowTrayMenu(hwnd);
            else if (msg.lParam == WM_LBUTTONDBLCLK) g_overlay.ToggleVisible();
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(hwnd, TIMER_TICK_ID);
    RemoveTrayIcon();
    FrameSource::Instance().Stop();
    g_sys.Shutdown();
    g_overlay.Destroy();
    if (mtx) CloseHandle(mtx);
    return 0;
}
