#include "overlay.h"
#include <cstdio>
#include <cwchar>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

static const wchar_t* kClass = L"FpsMonOverlayWnd";

static LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_NCHITTEST) return HTTRANSPARENT;   // 双保险：鼠标永不命中
    return DefWindowProcW(h, m, wp, lp);
}

bool Overlay::Create(HINSTANCE hInst)
{
    UINT dpi = 96;
    auto getDpi = (UINT(WINAPI*)())GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem");
    if (getDpi) dpi = getDpi();
    scale_ = float(dpi) / 96.0f;

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = hInst;
    wc.hCursor = nullptr;
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CXSMICON), 0);
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    // 关键样式：LAYERED 支持逐像素透明；TRANSPARENT 让鼠标穿透；TOOLWINDOW 不进 Alt+Tab；
    // NOACTIVATE 永不抢焦点；TOPMOST 浮在游戏之上
    DWORD ex = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
               WS_EX_NOACTIVATE | WS_EX_TOPMOST;

    // 横条形态：五项指标横向排开，贴屏幕顶端，尽量不占视野
    winW_ = (int)(438 * scale_);
    winH_ = (int)(26 * scale_);

    hwnd_ = CreateWindowExW(ex, kClass, L"FPS", WS_POPUP,
                            0, 0, winW_, winH_, nullptr, nullptr, hInst, nullptr);
    if (!hwnd_) return false;

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(&dwrite_));
    if (!d2d_ || !dwrite_) return false;

    if (!CreateDeviceObjects()) return false;

    EnsureSurface(winW_, winH_);
    Reposition();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    return true;
}

void Overlay::EnsureSurface(int w, int h)
{
    if (w <= 0 || h <= 0) return;
    if (surW_ == w && surH_ == h && bmp_) return;
    if (bmp_) {
        if (memDC_ && oldBmp_) SelectObject(memDC_, oldBmp_);
        DeleteObject(bmp_);
        bmp_ = nullptr; oldBmp_ = nullptr; bits_ = nullptr;
    }
    if (!memDC_) memDC_ = CreateCompatibleDC(nullptr);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;                 // 自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    bmp_ = CreateDIBSection(memDC_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp_) return;
    bits_ = bits;
    oldBmp_ = (HBITMAP)SelectObject(memDC_, bmp_);
    surW_ = w; surH_ = h;
}

void Overlay::Reposition()
{
    if (!hwnd_) return;
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    // 居中贴边：顶部时紧贴工作区上沿，底部时紧贴下沿，左右居中
    const int x = wa.left + ((wa.right - wa.left) - winW_) / 2;
    const int y = (pos_ == 0) ? wa.top : (wa.bottom - winH_);

    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, winW_, winH_,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void Overlay::SetPosition(int p)
{
    pos_ = (p == 1) ? 1 : 0;
    Reposition();
}

void Overlay::SetVisible(bool v)
{
    visible_ = v;
    ShowWindow(hwnd_, v ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (v) Reposition();
}

void Overlay::ToggleVisible() { SetVisible(!visible_); }

// 渲染目标与画笔/字体的创建。设备丢失（EndDraw 失败）后整体重建。
bool Overlay::CreateDeviceObjects()
{
    if (!d2d_ || !dwrite_) return false;

    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    if (rt_) { rt_->Release(); rt_ = nullptr; }
    if (FAILED(d2d_->CreateDCRenderTarget(&rtp, &rt_))) return false;

    rt_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.62f), &bgBrush_);
    rt_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1.0f), &numBrush_);
    rt_->CreateSolidColorBrush(D2D1::ColorF(0.72f, 0.74f, 0.78f, 1.0f), &lblBrush_);

    dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                              14.5f * scale_, L"", &fmtNum_);
    dwrite_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                              11.5f * scale_, L"", &fmtLbl_);
    if (fmtNum_) {
        fmtNum_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        fmtNum_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fmtNum_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);   // 横条只有一行，禁止折行
    }
    if (fmtLbl_) {
        fmtLbl_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        fmtLbl_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fmtLbl_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    return rt_ && bgBrush_ && numBrush_ && lblBrush_ && fmtNum_ && fmtLbl_;
}

void Overlay::Render(const OverlayValues& v)
{
    if (!hwnd_ || !visible_) return;
    if (!rt_ && !CreateDeviceObjects()) return;   // 设备丢失后在这里恢复
    EnsureSurface(winW_, winH_);
    if (!bmp_ || !memDC_) return;

    RECT rc{ 0, 0, winW_, winH_ };
    rt_->BindDC(memDC_, &rc);
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->Clear(D2D1::ColorF(0, 0, 0, 0.0f));

    // 贴顶时上沿切平（不留缺口），只把下沿两个角做圆，避免像贴了块黑胶布
    if (pos_ == 0) {
        const float r = 7.0f * scale_;
        const float w = (float)winW_, h = (float)winH_;
        ID2D1PathGeometry* geo = nullptr;
        if (SUCCEEDED(d2d_->CreatePathGeometry(&geo)) && geo) {
            ID2D1GeometrySink* sink = nullptr;
            if (SUCCEEDED(geo->Open(&sink)) && sink) {
                sink->BeginFigure(D2D1::Point2F(0.0f, 0.0f), D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLine(D2D1::Point2F(w, 0.0f));
                sink->AddLine(D2D1::Point2F(w, h - r));
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(w - r, h), D2D1::SizeF(r, r), 0.0f,
                                              D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->AddLine(D2D1::Point2F(r, h));
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(0.0f, h - r), D2D1::SizeF(r, r), 0.0f,
                                              D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                sink->Release();
                rt_->FillGeometry(geo, bgBrush_);
            } else {
                rt_->FillRectangle(D2D1::RectF(0, 0, w, h), bgBrush_);
            }
            geo->Release();
        } else {
            rt_->FillRectangle(D2D1::RectF(0, 0, (float)winW_, (float)winH_), bgBrush_);
        }
    } else {
        const float r = 8.0f * scale_;
        D2D1_ROUNDED_RECT rr{ D2D1::RectF(0, 0, (float)winW_, (float)winH_), r, r };
        rt_->FillRoundedRectangle(rr, bgBrush_);
    }

    const wchar_t* labels[5] = { L"FPS", L"1% low", L"CPU", L"GPU", L"RAM" };
    wchar_t nums[5][24] = {};

    if (v.hasFrame) {
        swprintf_s(nums[0], L"%.0f", v.fps);
        swprintf_s(nums[1], L"%.0f", v.low1);
    } else {
        wcscpy_s(nums[0], L"--");
        wcscpy_s(nums[1], L"--");
    }
    swprintf_s(nums[2], L"%.0f%%", v.cpu);
    swprintf_s(nums[3], v.gpuValid ? L"%.0f%%" : L"--", v.gpu);
    swprintf_s(nums[4], L"%.0f%%", v.mem);

    // 横向五组：每组「数值右对齐 + 标签左对齐」，组宽固定，数字位数变化时不会左右乱跳
    const float padX = 8.0f * scale_;
    const float groupW = 78.0f * scale_;
    const float stride = 86.0f * scale_;
    const float numW = 34.0f * scale_;

    for (int i = 0; i < 5; ++i) {
        const float x = padX + stride * i;
        D2D1_RECT_F rn = D2D1::RectF(x, 0.0f, x + numW, (float)winH_);
        D2D1_RECT_F rl = D2D1::RectF(x + numW + 4.0f * scale_, 1.0f * scale_,
                                     x + groupW, (float)winH_);
        // 所有数值共用同一字号，所有标签共用另一字号，两组各成一体
        rt_->DrawTextW(nums[i], (UINT32)wcslen(nums[i]), fmtNum_, rn, numBrush_,
                       D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
        rt_->DrawTextW(labels[i], (UINT32)wcslen(labels[i]), fmtLbl_, rl, lblBrush_,
                       D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
    }

    if (FAILED(rt_->EndDraw())) {
        // 设备丢失（驱动重置/远程会话切换）：销毁渲染目标，下个 tick 重建重画
        ReleaseDeviceObjects();
        CreateDeviceObjects();
        return;
    }

    POINT ptDst{};
    SIZE  sz{ winW_, winH_ };
    POINT ptSrc{ 0, 0 };
    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    ptDst.x = wr.left;
    ptDst.y = wr.top;

    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd_, nullptr, &ptDst, &sz, memDC_, &ptSrc, 0, &bf, ULW_ALPHA);
}

void Overlay::ReleaseDeviceObjects()
{
    if (fmtNum_) { fmtNum_->Release(); fmtNum_ = nullptr; }
    if (fmtLbl_) { fmtLbl_->Release(); fmtLbl_ = nullptr; }
    if (bgBrush_) { bgBrush_->Release(); bgBrush_ = nullptr; }
    if (numBrush_) { numBrush_->Release(); numBrush_ = nullptr; }
    if (lblBrush_) { lblBrush_->Release(); lblBrush_ = nullptr; }
    if (rt_) { rt_->Release(); rt_ = nullptr; }
}

void Overlay::Destroy()
{
    ReleaseDeviceObjects();
    if (dwrite_) { dwrite_->Release(); dwrite_ = nullptr; }
    if (d2d_) { d2d_->Release(); d2d_ = nullptr; }
    if (bmp_) {
        if (memDC_ && oldBmp_) SelectObject(memDC_, oldBmp_);
        DeleteObject(bmp_);
        bmp_ = nullptr;
    }
    if (memDC_) { DeleteDC(memDC_); memDC_ = nullptr; }
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
}
