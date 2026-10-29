// overlay.h - 悬浮显示层（屏幕顶端的横条）
// 置顶、透明、鼠标完全穿透：所有鼠标事件直接落到下层窗口，物理上碰不到。
#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

struct OverlayValues
{
    bool   hasFrame = false;    // 有帧数据才显示 FPS / 1% low
    double fps = 0.0;
    double low1 = 0.0;
    double cpu = 0.0;
    double gpu = 0.0;
    double mem = 0.0;
    bool   gpuValid = false;
};

class Overlay
{
public:
    bool Create(HINSTANCE hInst);
    void Destroy();
    void Render(const OverlayValues& v);

    HWND Hwnd() const { return hwnd_; }
    int  Position() const { return pos_; }
    void SetPosition(int p);          // 0=顶部居中 1=底部居中
    void ToggleVisible();
    bool Visible() const { return visible_; }
    void SetVisible(bool v);

private:
    void EnsureSurface(int w, int h);
    void ReleaseDeviceObjects();
    void Reposition();

    HWND hwnd_ = nullptr;
    int  pos_ = 0;                    // 0=顶部居中 1=底部居中
    bool visible_ = true;
    float scale_ = 1.0f;

    ID2D1Factory* d2d_ = nullptr;
    IDWriteFactory* dwrite_ = nullptr;
    ID2D1DCRenderTarget* rt_ = nullptr;
    ID2D1SolidColorBrush* bgBrush_ = nullptr;
    ID2D1SolidColorBrush* numBrush_ = nullptr;
    ID2D1SolidColorBrush* lblBrush_ = nullptr;
    IDWriteTextFormat* fmtNum_ = nullptr;    // 所有数值统一用这个字号
    IDWriteTextFormat* fmtLbl_ = nullptr;    // 所有标签统一用这个字号

    HDC     memDC_ = nullptr;
    HBITMAP bmp_ = nullptr;
    HBITMAP oldBmp_ = nullptr;
    void*   bits_ = nullptr;
    int     surW_ = 0, surH_ = 0;
    int     winW_ = 0, winH_ = 0;
};
