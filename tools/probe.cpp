// probe.cpp - 枚举 Microsoft-Windows-DXGI provider 的事件，找出 Present 相关事件 ID。
// 一次性诊断工具，不参与最终交付。
#include <windows.h>
#include <tdh.h>
#include <cstdio>
#include <vector>
#include <string>
#include <cwchar>

#pragma comment(lib, "tdh.lib")
#pragma comment(lib, "advapi32.lib")

static std::wstring EventNameOf(const GUID& provider, const EVENT_DESCRIPTOR& desc)
{
    ULONG size = 0;
    ULONG r = TdhGetManifestEventInformation(const_cast<LPGUID>(&provider),
                                             const_cast<PEVENT_DESCRIPTOR>(&desc),
                                             nullptr, &size);
    if (r != ERROR_INSUFFICIENT_BUFFER || size == 0) return L"";
    std::vector<BYTE> buf(size);
    auto* info = reinterpret_cast<TRACE_EVENT_INFO*>(buf.data());
    r = TdhGetManifestEventInformation(const_cast<LPGUID>(&provider),
                                       const_cast<PEVENT_DESCRIPTOR>(&desc),
                                       info, &size);
    if (r != ERROR_SUCCESS) return L"";
    if (info->EventNameOffset == 0) return L"";
    return std::wstring(reinterpret_cast<const wchar_t*>(buf.data() + info->EventNameOffset));
}

int wmain()
{
    ULONG size = 0;
    TdhEnumerateProviders(nullptr, &size);
    if (size == 0) { wprintf(L"TdhEnumerateProviders sized 0\n"); return 1; }
    std::vector<BYTE> buf(size);
    auto* providers = reinterpret_cast<PROVIDER_ENUMERATION_INFO*>(buf.data());
    ULONG r = TdhEnumerateProviders(providers, &size);
    if (r != ERROR_SUCCESS) { wprintf(L"TdhEnumerateProviders failed: %lu\n", r); return 1; }
    wprintf(L"providers total: %lu\n", providers->NumberOfProviders);

    GUID dxgi{};
    bool found = false;
    for (ULONG i = 0; i < providers->NumberOfProviders; ++i) {
        auto& p = providers->TraceProviderInfoArray[i];
        auto* name = reinterpret_cast<const wchar_t*>(buf.data() + p.ProviderNameOffset);
        if (name && _wcsicmp(name, L"Microsoft-Windows-DXGI") == 0) {
            dxgi = p.ProviderGuid;
            found = true;
            break;
        }
    }
    if (!found) { wprintf(L"DXGI provider NOT found\n"); return 1; }
    wprintf(L"DXGI GUID: {%08lX-%04hX-%04hX-%02X%02X-%02X%02X%02X%02X%02X%02X}\n",
            dxgi.Data1, dxgi.Data2, dxgi.Data3,
            dxgi.Data4[0], dxgi.Data4[1], dxgi.Data4[2], dxgi.Data4[3],
            dxgi.Data4[4], dxgi.Data4[5], dxgi.Data4[6], dxgi.Data4[7]);

    ULONG evSize = 0;
    r = TdhEnumerateManifestProviderEvents(&dxgi, nullptr, &evSize);
    if (evSize == 0) { wprintf(L"enum manifest events sized 0 (rc=%lu)\n", r); return 1; }
    std::vector<BYTE> evBuf(evSize);
    auto* events = reinterpret_cast<PROVIDER_EVENT_INFO*>(evBuf.data());
    r = TdhEnumerateManifestProviderEvents(&dxgi, events, &evSize);
    if (r != ERROR_SUCCESS) { wprintf(L"enum manifest events failed: %lu\n", r); return 1; }

    wprintf(L"events total: %lu\n\n", events->NumberOfEvents);
    wprintf(L"%-6s %-7s %-5s %-19s %s\n", L"id", L"opcode", L"task", L"keyword", L"name");
    for (ULONG i = 0; i < events->NumberOfEvents; ++i) {
        auto& d = events->EventDescriptorsArray[i];
        std::wstring n = EventNameOf(dxgi, d);
        wprintf(L"%-6u %-7u %-5u 0x%016llX  %s\n",
                d.Id, d.Opcode, (unsigned)d.Task,
                (unsigned long long)d.Keyword, n.c_str());
    }
    return 0;
}
