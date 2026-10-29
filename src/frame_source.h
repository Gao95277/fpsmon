// frame_source.h - ETW 帧事件采集
// 订阅 Microsoft-Windows-DXGI 的 Present 起始事件（EventID 42），只取进程 ID 与高精度时间戳。
// 不注入、不读游戏内存，从系统事件流旁路获取。
#pragma once
#include <windows.h>
#include <evntrace.h>
#include <cstdint>
#include <vector>
#include <deque>
#include <mutex>
#include <string>

struct FrameEvent
{
    uint32_t pid;
    int64_t  qpc;   // 100ns 计数（QPC）
};

class FrameSource
{
public:
    static FrameSource& Instance();

    bool Start(std::wstring& err);
    void Stop();
    bool Running() const { return running_; }

    // 取走累积的帧事件（线程安全）
    size_t Drain(std::vector<FrameEvent>& out);

    int64_t QpcFreq() const { return qpcFreq_; }
    long long DroppedCount() const { return dropped_; }
    bool Filtered() const { return filtered_; }
    long ProcTraceRc() const { return procTraceRc_; }
    long long RawEvents() const { return rawEvents_; }   // 回调被调用的总次数（诊断用）

    long dbgStartRc = 0, dbgEnableRc = 0, dbgRestartRc = 0, dbgOpenErr = 0;
    unsigned long long dbgSession = 0, dbgTrace = 0;

private:
    FrameSource() = default;
    static VOID WINAPI EventThunk(PEVENT_RECORD ev);
    static ULONG WINAPI BufferThunk(PEVENT_TRACE_LOGFILEW lf);
    void HandleEvent(PEVENT_RECORD ev);

    TRACEHANDLE session_ = 0;
    TRACEHANDLE trace_ = 0;
    volatile bool running_ = false;
    bool filtered_ = false;
    std::mutex mtx_;
    std::deque<FrameEvent> queue_;
    int64_t qpcFreq_ = 10000000;
    long long dropped_ = 0;
    void* propsMem_ = nullptr;
    void* filterMem_ = nullptr;
    HANDLE thread_ = nullptr;
    std::wstring sessionName_;
    EVENT_TRACE_LOGFILEW logfile_{};
    volatile long procTraceRc_ = -1;
    volatile long long rawEvents_ = 0;
};
