#include "frame_source.h"
#include <evntcons.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "advapi32.lib")

static const GUID DXGI_PROVIDER =
    { 0xCA11C036, 0x0102, 0x4A2D, { 0xA6, 0xAD, 0xF0, 0x3C, 0xFE, 0xD5, 0xD3, 0xC9 } };

// 探测程序实测：DXGI provider 中 ID=42 的事件在呈现循环里与本进程帧数 1:1 对应
static const USHORT PRESENT_START_ID = 42;

FrameSource& FrameSource::Instance()
{
    static FrameSource inst;
    return inst;
}

VOID WINAPI FrameSource::EventThunk(PEVENT_RECORD ev) { Instance().HandleEvent(ev); }

ULONG WINAPI FrameSource::BufferThunk(PEVENT_TRACE_LOGFILEW)
{
    return Instance().running_ ? TRUE : FALSE;
}

void FrameSource::HandleEvent(PEVENT_RECORD ev)
{
    ++rawEvents_;
    if (!IsEqualGUID(ev->EventHeader.ProviderId, DXGI_PROVIDER)) return;
    if (ev->EventHeader.EventDescriptor.Id != PRESENT_START_ID) return;

    FrameEvent fe;
    fe.pid = ev->EventHeader.ProcessId;
    fe.qpc = ev->EventHeader.TimeStamp.QuadPart;

    std::lock_guard<std::mutex> lk(mtx_);
    if (queue_.size() >= 16384) {         // 上限保护，正常读取不会触发
        queue_.pop_front();
        ++dropped_;
    }
    queue_.push_back(fe);
}

size_t FrameSource::Drain(std::vector<FrameEvent>& out)
{
    std::lock_guard<std::mutex> lk(mtx_);
    size_t n = queue_.size();
    out.clear();
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) out.push_back(queue_[i]);
    queue_.clear();
    return n;
}

bool FrameSource::Start(std::wstring& err)
{
    if (running_) return true;

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    qpcFreq_ = f.QuadPart;

    // 会话名必须固定：带进程号的会话名在异常退出后会永久残留，
    // 每个残留会话都占一个 DXGI 订阅名额，累积几次就再也订阅不上。
    sessionName_ = L"FpsMonRealtime";

    const size_t nameBytes = (sessionName_.size() + 1) * sizeof(wchar_t);
    const size_t propsSize = sizeof(EVENT_TRACE_PROPERTIES) + nameBytes;
    propsMem_ = calloc(1, propsSize);
    if (!propsMem_) { err = L"内存分配失败"; return false; }

    auto* props = (EVENT_TRACE_PROPERTIES*)propsMem_;
    auto fillProps = [&]() {
        memset(propsMem_, 0, propsSize);
        props->Wnode.BufferSize = (ULONG)propsSize;
        props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
        props->BufferSize = 64;          // KB：缓冲必须够大，8KB 实测会丢一半事件
        props->MinimumBuffers = 4;
        props->MaximumBuffers = 16;
        props->FlushTimer = 1;           // 1 秒强制刷一次，避免低帧率时事件滞留
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        memcpy((BYTE*)props + props->LoggerNameOffset, sessionName_.c_str(), nameBytes);
    };
    fillProps();

    // 先无条件清理同名残留会话（不存在会返回失败，忽略即可）
    ControlTraceW(0, sessionName_.c_str(), props, EVENT_TRACE_CONTROL_STOP);
    fillProps();

    ULONG rc = StartTraceW(&session_, sessionName_.c_str(), props);
    dbgStartRc = (long)rc;
    dbgSession = (unsigned long long)session_;
    if (rc == ERROR_ALREADY_EXISTS) {
        // 上次异常退出残留了同名会话，先停掉再重建
        ControlTraceW(0, sessionName_.c_str(), props, EVENT_TRACE_CONTROL_STOP);
        fillProps();
        rc = StartTraceW(&session_, sessionName_.c_str(), props);
        dbgRestartRc = (long)rc;
        dbgSession = (unsigned long long)session_;
    }
    if (rc != ERROR_SUCCESS) {
        wchar_t buf[256];
        swprintf_s(buf, L"StartTrace 失败 (错误码 %lu)%s", rc,
                   rc == ERROR_ACCESS_DENIED ? L"，需要管理员权限" : L"");
        err = buf;
        free(propsMem_); propsMem_ = nullptr;
        return false;
    }

    // 订阅 DXGI 事件源。不做事件 ID 过滤——该 provider 为自描述型，过滤器不生效反而有副作用，
    // 由回调按 EventID 过滤即可（事件总量本身很小）。
    filtered_ = false;
    rc = EnableTraceEx2(session_, &DXGI_PROVIDER, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                        TRACE_LEVEL_VERBOSE, 0, 0, 0, nullptr);
    dbgEnableRc = (long)rc;
    if (rc != ERROR_SUCCESS) {
        wchar_t buf[256];
        swprintf_s(buf, L"启用 DXGI 事件源失败 (错误码 %lu)", rc);
        err = buf;
        ControlTraceW(session_, sessionName_.c_str(), props, EVENT_TRACE_CONTROL_STOP);
        session_ = 0;
        free(propsMem_); propsMem_ = nullptr;
        return false;
    }

    running_ = true;

    // logfile 必须是成员：ProcessTrace 在独立线程运行，不能依赖栈上对象
    memset(&logfile_, 0, sizeof(logfile_));
    logfile_.LoggerName = (LPWSTR)sessionName_.c_str();
    logfile_.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile_.EventRecordCallback = EventThunk;
    logfile_.BufferCallback = BufferThunk;
    trace_ = OpenTraceW(&logfile_);
    dbgTrace = (unsigned long long)trace_;
    dbgOpenErr = (long)GetLastError();
    if (trace_ == INVALID_PROCESSTRACE_HANDLE) {
        wchar_t buf[256];
        swprintf_s(buf, L"OpenTrace 失败 (错误码 %lu)", GetLastError());
        err = buf;
        running_ = false;
        ControlTraceW(session_, sessionName_.c_str(), props, EVENT_TRACE_CONTROL_STOP);
        session_ = 0;
        free(propsMem_); propsMem_ = nullptr;
        return false;
    }

    thread_ = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
        auto* self = (FrameSource*)p;
        TRACEHANDLE h = self->trace_;
        self->procTraceRc_ = (long)ProcessTrace(&h, 1, nullptr, nullptr);
        return 0;
    }, this, 0, nullptr);

    if (!thread_) { err = L"创建采集线程失败"; Stop(); return false; }
    return true;
}

void FrameSource::Stop()
{
    if (session_ == 0 && !thread_) return;
    running_ = false;
    if (session_ && propsMem_) {
        ControlTraceW(session_, sessionName_.c_str(), (EVENT_TRACE_PROPERTIES*)propsMem_,
                      EVENT_TRACE_CONTROL_STOP);
        session_ = 0;
    }
    if (thread_) { WaitForSingleObject(thread_, 3000); CloseHandle(thread_); thread_ = nullptr; }
    if (trace_ != INVALID_PROCESSTRACE_HANDLE && trace_ != 0) { CloseTrace(trace_); trace_ = 0; }
    if (propsMem_) { free(propsMem_); propsMem_ = nullptr; }
    if (filterMem_) { free(filterMem_); filterMem_ = nullptr; }
}
