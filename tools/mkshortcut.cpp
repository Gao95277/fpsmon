// mkshortcut.cpp - 创建 .lnk 快捷方式的命令行工具
// 某些环境下无法使用 PowerShell 的 WScript.Shell（COM 被安全策略禁用），
// 这个工具直接用 IShellLink 完成同样的事。
//
// 用法:
//   mkshortcut <目标exe> <快捷方式路径.lnk> [工作目录] [描述]
//
// 注意: 含中文的路径请确认调用方编码正确，或在代码里改用宽字符串字面量。
#include <windows.h>
#include <shlobj.h>
#include <cstdio>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) {
        wprintf(L"usage: mkshortcut <target.exe> <link.lnk> [workdir] [description]\n");
        return 1;
    }
    const wchar_t* target = argv[1];
    const wchar_t* linkpath = argv[2];

    if (GetFileAttributesW(target) == INVALID_FILE_ATTRIBUTES) {
        wprintf(L"target not found: %ls\n", target);
        return 2;
    }

    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) { wprintf(L"CoInitialize failed 0x%08lX\n", (unsigned long)hr); return 3; }

    IShellLinkW* link = nullptr;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IShellLinkW, (void**)&link);
    if (FAILED(hr) || !link) { wprintf(L"CoCreateInstance 0x%08lX\n", (unsigned long)hr); return 4; }

    link->SetPath(target);
    link->SetIconLocation(target, 0);
    if (argc >= 4) link->SetWorkingDirectory(argv[3]);
    if (argc >= 5) link->SetDescription(argv[4]);

    IPersistFile* pf = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, (void**)&pf);
    if (SUCCEEDED(hr) && pf) {
        hr = pf->Save(linkpath, TRUE);
        pf->Release();
    } else {
        hr = E_FAIL;
    }
    link->Release();
    CoUninitialize();

    if (hr == S_OK) { wprintf(L"created: %ls\n", linkpath); return 0; }
    wprintf(L"Save failed 0x%08lX\n", (unsigned long)hr);
    return 5;
}
