// shot.cpp - 截取屏幕指定区域并保存为 24 位 BMP（诊断用，非交付物）
#include <windows.h>
#include <cstdio>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

static bool SaveBmp(const wchar_t* path, int w, int h, void* bits)
{
    const int rowSize = ((w * 24 + 31) / 32) * 4;
    const int imgSize = rowSize * h;

    BITMAPFILEHEADER fh{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + imgSize;

    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = h;
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = imgSize;

    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") != 0 || !f) return false;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    fwrite(bits, imgSize, 1, f);
    fclose(f);
    return true;
}

int wmain(int argc, wchar_t** argv)
{
    int x = 0, y = 0, w = 0, h = 0;

    if (argc >= 5) {
        x = _wtoi(argv[1]); y = _wtoi(argv[2]);
        w = _wtoi(argv[3]); h = _wtoi(argv[4]);
    } else {
        x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }
    if (w <= 0 || h <= 0) { wprintf(L"bad size\n"); return 1; }

    HDC screen = CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr);
    if (!screen) { wprintf(L"CreateDC failed\n"); return 1; }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h;           // 正数 = bottom-up，与 BMP 存储顺序一致
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) { wprintf(L"CreateDIBSection failed\n"); return 1; }
    HGDIOBJ old = SelectObject(mem, bmp);

    BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY);

    const wchar_t* out = (argc >= 6) ? argv[5] : L"shot.bmp";
    bool ok = SaveBmp(out, w, h, bits);
    wprintf(L"%s  %dx%d at (%d,%d)\n", ok ? L"saved" : L"FAILED", w, h, x, y);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    DeleteDC(screen);
    return ok ? 0 : 1;
}
