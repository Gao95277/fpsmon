// etwtest.cpp - 一次性验证工具：
// 订阅 Microsoft-Windows-DXGI 全部 ETW 事件，同时在自身进程跑一个约 60fps 的 D3D11 呈现循环，
// 统计每个 EventID 的触发次数。频率与帧数吻合者即为 Present 事件。
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdio>
#include <map>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "winmm.lib")

static const GUID DXGI_PROVIDER =
    { 0xCA11C036, 0x0102, 0x4A2D, { 0xA6, 0xAD, 0xF0, 0x3C, 0xFE, 0xD5, 0xD3, 0xC9 } };

static CRITICAL_SECTION g_cs;
static std::map<USHORT, long long> g_counts;
static std::map<USHORT, long long> g_myCounts;   // 只统计本进程
static std::atomic<bool> g_run{ true };
static const DWORD g_myPid = GetCurrentProcessId();

static void WINAPI OnEvent(PEVENT_RECORD ev)
{
    if (!IsEqualGUID(ev->EventHeader.ProviderId, DXGI_PROVIDER)) return;
    USHORT id = ev->EventHeader.EventDescriptor.Id;
    bool mine = (ev->EventHeader.ProcessId == g_myPid);
    EnterCriticalSection(&g_cs);
    g_counts[id]++;
    if (mine) g_myCounts[id]++;
    LeaveCriticalSection(&g_cs);
}

static ULONG WINAPI OnBuffer(PEVENT_TRACE_LOGFILEW) { return g_run.load() ? TRUE : FALSE; }

struct D3dLoop
{
    HWND hwnd = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* sc = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    int frames = 0;

    bool Init()
    {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"EtwPresentTestWnd";
        RegisterClassExW(&wc);
        hwnd = CreateWindowExW(WS_EX_TOPMOST, L"EtwPresentTestWnd", L"ETW present test",
                               WS_POPUP | WS_VISIBLE, 60, 60, 240, 160,
                               nullptr, nullptr, wc.hInstance, nullptr);
        if (!hwnd) return false;

        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = 240;
        sd.BufferDesc.Height = 160;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        D3D_FEATURE_LEVEL fl{};
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
        if (FAILED(hr)) {
            wprintf(L"D3D11CreateDeviceAndSwapChain failed hr=0x%08lX\n", (unsigned long)hr);
            return false;
        }
        ID3D11Texture2D* back = nullptr;
        sc->GetBuffer(0, IID_PPV_ARGS(&back));
        if (back) { dev->CreateRenderTargetView(back, nullptr, &rtv); back->Release(); }
        return true;
    }

    void Run(int ms)
    {
        timeBeginPeriod(1);
        ULONGLONG t0 = GetTickCount64();
        float c = 0.0f;
        while (GetTickCount64() - t0 < (ULONGLONG)ms) {
            c += 0.02f;
            float col[4] = { (float)(0.5 + 0.5 * sin(c)), 0.2f, 0.4f, 1.0f };
            if (rtv) ctx->ClearRenderTargetView(rtv, col);
            sc->Present(0, 0);
            frames++;
            Sleep(16);
        }
        timeEndPeriod(1);
    }

    void Shutdown()
    {
        if (rtv) rtv->Release();
        if (sc) sc->Release();
        if (ctx) ctx->Release();
        if (dev) dev->Release();
        if (hwnd) DestroyWindow(hwnd);
    }
};

int wmain()
{
    InitializeCriticalSection(&g_cs);

    // 1. 启动 ETW 实时会话（固定会话名 + 先清理，避免反复运行累积残留占满订阅名额）
    const wchar_t* SESSION = L"FpsMonProbeFixed";
    const size_t nameBytes = (wcslen(SESSION) + 1) * sizeof(wchar_t);
    const size_t propsSize = sizeof(EVENT_TRACE_PROPERTIES) + nameBytes;
    EVENT_TRACE_PROPERTIES* props = (EVENT_TRACE_PROPERTIES*)calloc(1, propsSize);
    props->Wnode.BufferSize = (ULONG)propsSize;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    props->BufferSize = 64;
    props->MinimumBuffers = 4;
    props->MaximumBuffers = 16;
    props->FlushTimer = 1;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    memcpy((BYTE*)props + props->LoggerNameOffset, SESSION, nameBytes);

    ControlTraceW(0, SESSION, props, EVENT_TRACE_CONTROL_STOP);   // 清理同名残留
    props->Wnode.BufferSize = (ULONG)propsSize;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    props->BufferSize = 64;
    props->MinimumBuffers = 4;
    props->MaximumBuffers = 16;
    props->FlushTimer = 1;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    memcpy((BYTE*)props + props->LoggerNameOffset, SESSION, nameBytes);

    TRACEHANDLE session = 0;
    ULONG rc = StartTraceW(&session, SESSION, props);
    if (rc != ERROR_SUCCESS) {
        wprintf(L"StartTrace failed: %lu (%s)\n", rc,
                rc == ERROR_ACCESS_DENIED ? L"需要管理员权限" : L"");
        return 1;
    }
    wprintf(L"session started\n");

    rc = EnableTraceEx2(session, &DXGI_PROVIDER, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                        TRACE_LEVEL_VERBOSE, 0, 0, 0, nullptr);
    if (rc != ERROR_SUCCESS) wprintf(L"EnableTraceEx2 rc=%lu\n", rc);
    else wprintf(L"provider enabled\n");

    // 2. ProcessTrace 线程
    EVENT_TRACE_LOGFILEW lf{};
    lf.LoggerName = (LPWSTR)SESSION;
    lf.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    lf.EventRecordCallback = OnEvent;
    lf.BufferCallback = OnBuffer;
    TRACEHANDLE hTrace = OpenTraceW(&lf);
    if (hTrace == INVALID_PROCESSTRACE_HANDLE) {
        wprintf(L"OpenTrace failed: %lu\n", GetLastError());
        return 1;
    }
    std::thread th([hTrace]() mutable { ProcessTrace(&hTrace, 1, nullptr, nullptr); });

    // 3. 自身跑 5 秒 60fps 呈现
    D3dLoop loop;
    if (!loop.Init()) { wprintf(L"d3d init failed\n"); return 1; }
    wprintf(L"rendering 60s ...\n");
    SetForegroundWindow(loop.hwnd);
    loop.Run(60000);
    loop.Shutdown();
    wprintf(L"own frames presented: %d\n\n", loop.frames);

    // 4. 停止
    Sleep(700);
    g_run = false;
    ControlTraceW(session, SESSION, props, EVENT_TRACE_CONTROL_STOP);
    th.join();
    CloseTrace(hTrace);
    free(props);

    // 5. 输出
    std::vector<std::pair<long long, USHORT>> v;
    for (auto& kv : g_counts) v.push_back({ kv.second, kv.first });
    std::sort(v.rbegin(), v.rend());
    wprintf(L"%-8s %-10s %-10s %s\n", L"eventid", L"all", L"this_proc", L"per_own_frame");
    for (size_t i = 0; i < v.size() && i < 15; ++i) {
        USHORT id = v[i].second;
        long long all = v[i].first;
        auto it = g_myCounts.find(id);
        long long mine = (it == g_myCounts.end()) ? 0 : it->second;
        double ratio = loop.frames > 0 ? (double)mine / loop.frames : 0.0;
        wprintf(L"%-8u %-10lld %-10lld %.3f\n", id, all, mine, ratio);
    }
    return 0;
}
