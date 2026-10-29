// minitest.cpp - 隔离测试：只跑 FrameSource，不创建任何窗口
#include <windows.h>
#include <cstdio>
#include <vector>
#include <string>
#include "frame_source.h"

int wmain()
{
    std::wstring err;
    if (!FrameSource::Instance().Start(err)) {
        wprintf(L"Start failed: %s\n", err.c_str());
        return 1;
    }
    wprintf(L"started. qpf=%lld\n", (long long)FrameSource::Instance().QpcFreq());
    wprintf(L"startRc=%ld restartRc=%ld enableRc=%ld openErr=%ld session=%llu trace=%llu\n",
            FrameSource::Instance().dbgStartRc,
            FrameSource::Instance().dbgRestartRc,
            FrameSource::Instance().dbgEnableRc,
            FrameSource::Instance().dbgOpenErr,
            FrameSource::Instance().dbgSession,
            FrameSource::Instance().dbgTrace);

    std::vector<FrameEvent> ev;
    for (int i = 0; i < 12; ++i) {
        Sleep(500);
        ev.clear();
        size_t n = FrameSource::Instance().Drain(ev);
        wprintf(L"tick %2d  recv=%-4zu raw=%-6lld pt=%ld\n",
                i, n, FrameSource::Instance().RawEvents(),
                FrameSource::Instance().ProcTraceRc());
    }
    FrameSource::Instance().Stop();
    return 0;
}
