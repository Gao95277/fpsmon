// img2png.cpp - BMP 转 PNG（GDI+），用于生成仓库里的预览图
#include <windows.h>
#include <gdiplus.h>
#include <cstdio>

#pragma comment(lib, "gdiplus.lib")

static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
{
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    auto* info = (Gdiplus::ImageCodecInfo*)malloc(size);
    if (!info) return -1;
    Gdiplus::GetImageEncoders(num, size, info);
    int found = -1;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, format) == 0) { *pClsid = info[i].Clsid; found = (int)i; break; }
    }
    free(info);
    return found;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) { wprintf(L"usage: img2png <in.bmp> <out.png>\n"); return 1; }

    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &gsi, nullptr) != Gdiplus::Ok) return 2;

    int rc = 3;
    {
        Gdiplus::Bitmap bmp(argv[1]);
        if (bmp.GetLastStatus() == Gdiplus::Ok) {
            CLSID clsid;
            if (GetEncoderClsid(L"image/png", &clsid) >= 0) {
                rc = (bmp.Save(argv[2], &clsid, nullptr) == Gdiplus::Ok) ? 0 : 4;
            }
        }
    }
    Gdiplus::GdiplusShutdown(token);
    wprintf(rc == 0 ? L"ok\n" : L"failed %d\n", rc);
    return rc;
}
