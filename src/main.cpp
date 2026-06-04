#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cwchar>

#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")
#pragma comment(lib, "dwmapi")
#pragma comment(lib, "shell32")

namespace {

constexpr int kWindowWidth = 300;
constexpr int kWindowHeight = 180;
constexpr int kCompactWidth = 230;
constexpr int kCompactHeight = 76;
constexpr int kHistorySize = 60;
constexpr UINT_PTR kSampleTimer = 1;
constexpr UINT_PTR kFrameTimer = 2;
constexpr UINT_PTR kDockHideTimer = 3;
constexpr float kPi = 3.14159265358979323846f;
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
constexpr int kHotkeyToggleClickThrough = 100;
constexpr UINT kTrayIconId = 1;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr int kDockSnapDistance = 12;
constexpr int kDockHandleSize = 8;
constexpr UINT kDockHideDelayMs = 900;

enum class DockEdge {
    None,
    Left,
    Right,
    Top
};

enum MenuId : UINT {
    kMenuAlwaysOnTop = 1000,
    kMenuClickThrough,
    kMenuCompactMode,
    kMenuEdgeDock,
    kMenuOpacity60,
    kMenuOpacity80,
    kMenuOpacity100,
    kMenuRefresh250,
    kMenuRefresh500,
    kMenuRefresh1000,
    kMenuShowHide,
    kMenuExit
};

template <typename T>
void SafeRelease(T** value) {
    if (*value) {
        (*value)->Release();
        *value = nullptr;
    }
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float Lerp(float from, float to, float amount) {
    return from + (to - from) * amount;
}

FILETIME SubtractFileTime(FILETIME a, FILETIME b) {
    ULARGE_INTEGER ua{};
    ULARGE_INTEGER ub{};
    ua.LowPart = a.dwLowDateTime;
    ua.HighPart = a.dwHighDateTime;
    ub.LowPart = b.dwLowDateTime;
    ub.HighPart = b.dwHighDateTime;

    ULARGE_INTEGER result{};
    result.QuadPart = ua.QuadPart - ub.QuadPart;

    FILETIME ft{};
    ft.dwLowDateTime = result.LowPart;
    ft.dwHighDateTime = result.HighPart;
    return ft;
}

unsigned long long FileTimeToUInt64(FILETIME value) {
    ULARGE_INTEGER converted{};
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

struct History {
    std::array<float, kHistorySize> values{};
    int head = 0;
    bool filled = false;

    void Push(float value) {
        values[head] = Clamp01(value);
        head = (head + 1) % kHistorySize;
        if (head == 0) {
            filled = true;
        }
    }

    float At(int visualIndex) const {
        const int count = filled ? kHistorySize : head;
        if (count == 0) {
            return 0.0f;
        }

        const int clamped = std::clamp(visualIndex, 0, count - 1);
        const int first = filled ? head : 0;
        return values[(first + clamped) % kHistorySize];
    }

    int Count() const {
        return filled ? kHistorySize : head;
    }
};

class PulseDialApp {
public:
    explicit PulseDialApp(HWND hwnd) : hwnd_(hwnd) {}

    ~PulseDialApp() {
        RemoveTrayIcon();
        DiscardDeviceResources();
        SafeRelease(&formatTiny_);
        SafeRelease(&formatSmallMedium_);
        SafeRelease(&formatSmallBold_);
        SafeRelease(&formatValue_);
        SafeRelease(&writeFactory_);
        SafeRelease(&factory_);
    }

    HRESULT Initialize() {
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_);
        if (FAILED(hr)) {
            return hr;
        }

        hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&writeFactory_));
        if (FAILED(hr)) {
            return hr;
        }

        hr = CreateTextFormats();
        if (FAILED(hr)) {
            return hr;
        }

        previousCpuValid_ = ReadCpuTimes(previousIdle_, previousKernel_, previousUser_);
        Sample();
        ApplyWindowOptions();
        AddTrayIcon();
        return S_OK;
    }

    void Resize() {
        if (!renderTarget_) {
            return;
        }

        RECT rc{};
        GetClientRect(hwnd_, &rc);
        const D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
        renderTarget_->Resize(size);
    }

    void Sample() {
        const float cpu = ReadCpuLoad();
        const float memory = ReadMemoryLoad();
        cpuTarget_ = cpu;
        memTarget_ = memory;
        cpuHistory_.Push(cpu);
        memHistory_.Push(memory);
    }

    void TickFrame() {
        cpuVisible_ = Lerp(cpuVisible_, cpuTarget_, 0.18f);
        memVisible_ = Lerp(memVisible_, memTarget_, 0.12f);
        StepDockAnimation();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    HRESULT Render() {
        HRESULT hr = CreateDeviceResources();
        if (FAILED(hr)) {
            return hr;
        }

        renderTarget_->BeginDraw();
        renderTarget_->Clear(D2D1::ColorF(0.0f, 0.0f));

        DrawPanel();
        DrawHeader();
        if (compactMode_) {
            DrawCompact();
        } else {
            DrawGauge();
            DrawMemory();
            DrawHistory();
        }

        hr = renderTarget_->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            DiscardDeviceResources();
            hr = S_OK;
        }
        return hr;
    }

    void ShowContextMenu(POINT point) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (alwaysOnTop_ ? MF_CHECKED : 0), kMenuAlwaysOnTop, L"Always on top");
        AppendMenuW(
            menu,
            MF_STRING | (clickThrough_ ? MF_CHECKED : 0),
            kMenuClickThrough,
            clickThrough_ ? L"Disable click-through" : L"Enable click-through");
        AppendMenuW(menu, MF_STRING | (compactMode_ ? MF_CHECKED : 0), kMenuCompactMode, L"Compact mode");
        AppendMenuW(menu, MF_STRING | (edgeDockEnabled_ ? MF_CHECKED : 0), kMenuEdgeDock, L"Edge dock");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        HMENU opacityMenu = CreatePopupMenu();
        AppendMenuW(opacityMenu, MF_STRING | (opacity_ == 153 ? MF_CHECKED : 0), kMenuOpacity60, L"60%");
        AppendMenuW(opacityMenu, MF_STRING | (opacity_ == 204 ? MF_CHECKED : 0), kMenuOpacity80, L"80%");
        AppendMenuW(opacityMenu, MF_STRING | (opacity_ == 255 ? MF_CHECKED : 0), kMenuOpacity100, L"100%");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(opacityMenu), L"Opacity");

        HMENU refreshMenu = CreatePopupMenu();
        AppendMenuW(refreshMenu, MF_STRING | (sampleIntervalMs_ == 250 ? MF_CHECKED : 0), kMenuRefresh250, L"250 ms");
        AppendMenuW(refreshMenu, MF_STRING | (sampleIntervalMs_ == 500 ? MF_CHECKED : 0), kMenuRefresh500, L"500 ms");
        AppendMenuW(refreshMenu, MF_STRING | (sampleIntervalMs_ == 1000 ? MF_CHECKED : 0), kMenuRefresh1000, L"1000 ms");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(refreshMenu), L"Refresh rate");

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuShowHide, IsWindowVisible(hwnd_) ? L"Hide" : L"Show");
        AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");

        SetForegroundWindow(hwnd_);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
    }

    void HandleCommand(UINT command) {
        switch (command) {
        case kMenuAlwaysOnTop:
            alwaysOnTop_ = !alwaysOnTop_;
            ApplyWindowOptions();
            break;
        case kMenuClickThrough:
            SetClickThrough(!clickThrough_);
            break;
        case kMenuCompactMode:
            compactMode_ = !compactMode_;
            ApplyWindowOptions();
            if (edgeDockEnabled_ && dockEdge_ != DockEdge::None) {
                DockToEdge(dockEdge_, false);
            }
            break;
        case kMenuEdgeDock:
            SetEdgeDockEnabled(!edgeDockEnabled_);
            break;
        case kMenuOpacity60:
            opacity_ = 153;
            ApplyWindowOptions();
            break;
        case kMenuOpacity80:
            opacity_ = 204;
            ApplyWindowOptions();
            break;
        case kMenuOpacity100:
            opacity_ = 255;
            ApplyWindowOptions();
            break;
        case kMenuRefresh250:
            SetSampleInterval(250);
            break;
        case kMenuRefresh500:
            SetSampleInterval(500);
            break;
        case kMenuRefresh1000:
            SetSampleInterval(1000);
            break;
        case kMenuShowHide:
            ShowWindow(hwnd_, IsWindowVisible(hwnd_) ? SW_HIDE : SW_SHOWNOACTIVATE);
            break;
        case kMenuExit:
            PostMessage(hwnd_, WM_CLOSE, 0, 0);
            break;
        default:
            break;
        }
    }

    void ToggleClickThrough() {
        SetClickThrough(!clickThrough_);
    }

    bool IsClickThrough() const {
        return clickThrough_;
    }

    void HandleMouseMove(bool nonClient = false) {
        if (edgeDockEnabled_ && dockHidden_) {
            ShowDockExpanded();
        }
        TrackMouseLeave(nonClient);
    }

    void HandleMouseLeave() {
        trackingMouse_ = false;
        if (edgeDockEnabled_ && dockEdge_ != DockEdge::None && !dockHidden_) {
            SetTimer(hwnd_, kDockHideTimer, kDockHideDelayMs, nullptr);
        }
    }

    void HandleExitSizeMove() {
        if (!edgeDockEnabled_) {
            return;
        }
        TryDockNearEdge();
    }

    void HandleDockHideTimer() {
        HideDock();
    }

    void HandleTrayMessage(LPARAM event) {
        if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
            POINT point{};
            GetCursorPos(&point);
            ShowContextMenu(point);
        } else if (event == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd_, IsWindowVisible(hwnd_) ? SW_HIDE : SW_SHOWNOACTIVATE);
        }
    }

private:
    HRESULT CreateDeviceResources() {
        if (renderTarget_) {
            return S_OK;
        }

        RECT rc{};
        GetClientRect(hwnd_, &rc);
        const D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

        const auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));

        const auto hwndProps = D2D1::HwndRenderTargetProperties(hwnd_, size);
        HRESULT hr = factory_->CreateHwndRenderTarget(props, hwndProps, &renderTarget_);
        if (FAILED(hr)) {
            return hr;
        }

        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0x101214, 0.92f), &brushPanel_);
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0x343A40, 0.92f), &brushBorder_);
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0xE7EAEC, 0.96f), &brushText_);
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0x7F8991, 0.95f), &brushMuted_);
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0x32E0C4, 0.95f), &brushCpu_);
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0xD7A84A, 0.95f), &brushMem_);
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0xF05A50, 0.95f), &brushDanger_);
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0x22282D, 0.9f), &brushTrack_);

        return S_OK;
    }

    void DiscardDeviceResources() {
        SafeRelease(&brushPanel_);
        SafeRelease(&brushBorder_);
        SafeRelease(&brushText_);
        SafeRelease(&brushMuted_);
        SafeRelease(&brushCpu_);
        SafeRelease(&brushMem_);
        SafeRelease(&brushDanger_);
        SafeRelease(&brushTrack_);
        SafeRelease(&renderTarget_);
    }

    void DrawPanel() {
        const auto size = renderTarget_->GetSize();
        const auto panel = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, size.width - 0.5f, size.height - 0.5f), 6.0f, 6.0f);
        renderTarget_->FillRoundedRectangle(panel, brushPanel_);
        renderTarget_->DrawRoundedRectangle(panel, brushBorder_, 1.0f);
        renderTarget_->DrawLine(D2D1::Point2F(16.0f, 31.0f), D2D1::Point2F(size.width - 16.0f, 31.0f), brushTrack_, 1.0f);
    }

    void DrawHeader() {
        DrawTextAt(L"PULSE", 16.0f, 9.0f, 80.0f, 18.0f, brushText_, formatSmallBold_);

        const auto size = renderTarget_->GetSize();
        wchar_t timeText[16]{};
        SYSTEMTIME time{};
        GetLocalTime(&time);
        std::swprintf(timeText, 16, L"%02d:%02d", time.wHour, time.wMinute);
        DrawTextAt(timeText, size.width - 64.0f, 9.0f, 44.0f, 18.0f, brushMuted_, formatSmallMedium_);

        const float load = std::max(cpuVisible_, memVisible_);
        ID2D1SolidColorBrush* statusBrush = load > 0.85f ? brushDanger_ : load > 0.60f ? brushMem_ : brushCpu_;
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(size.width - 19.0f, 17.0f), 3.0f, 3.0f), statusBrush);
    }

    void DrawGauge() {
        DrawTextAt(L"CPU", 139.0f, 37.0f, 40.0f, 18.0f, brushMuted_, formatSmallBold_);

        const D2D1_POINT_2F center = D2D1::Point2F(150.0f, 106.0f);
        const float radius = 62.0f;
        DrawArc(center, radius, 205.0f, 335.0f, brushTrack_, 5.0f);

        ID2D1SolidColorBrush* valueBrush = cpuVisible_ > 0.85f ? brushDanger_ : brushCpu_;
        DrawArc(center, radius, 205.0f, 205.0f + 130.0f * Clamp01(cpuVisible_), valueBrush, 5.0f);
        DrawArc(center, radius - 9.0f, 205.0f, 335.0f, brushBorder_, 1.0f);

        for (int i = 0; i <= 10; ++i) {
            const float t = static_cast<float>(i) / 10.0f;
            const float angle = DegreesToRadians(205.0f + 130.0f * t);
            const float outer = radius + 2.0f;
            const float inner = radius - (i % 5 == 0 ? 9.0f : 6.0f);
            const auto p1 = D2D1::Point2F(center.x + std::cos(angle) * outer, center.y + std::sin(angle) * outer);
            const auto p2 = D2D1::Point2F(center.x + std::cos(angle) * inner, center.y + std::sin(angle) * inner);
            renderTarget_->DrawLine(p1, p2, i >= 8 ? brushDanger_ : brushMuted_, 1.0f);
        }

        const float needleAngle = DegreesToRadians(205.0f + 130.0f * Clamp01(cpuVisible_));
        const auto needleEnd = D2D1::Point2F(center.x + std::cos(needleAngle) * 46.0f, center.y + std::sin(needleAngle) * 46.0f);
        renderTarget_->DrawLine(center, needleEnd, valueBrush, 2.0f);
        renderTarget_->FillEllipse(D2D1::Ellipse(center, 4.0f, 4.0f), brushText_);
        renderTarget_->FillEllipse(D2D1::Ellipse(center, 2.0f, 2.0f), valueBrush);

        wchar_t value[16]{};
        std::swprintf(value, 16, L"%d%%", static_cast<int>(std::round(cpuVisible_ * 100.0f)));
        DrawTextAt(value, 129.0f, 84.0f, 55.0f, 26.0f, valueBrush, formatValue_);
        DrawTextAt(L"0", 77.0f, 104.0f, 20.0f, 16.0f, brushMuted_, formatTiny_);
        DrawTextAt(L"100", 205.0f, 104.0f, 32.0f, 16.0f, brushMuted_, formatTiny_);
    }

    void DrawMemory() {
        wchar_t label[32]{};
        std::swprintf(label, 32, L"MEM  %d%%", static_cast<int>(std::round(memVisible_ * 100.0f)));
        ID2D1SolidColorBrush* valueBrush = memVisible_ > 0.85f ? brushDanger_ : brushMem_;
        DrawTextAt(label, 22.0f, 119.0f, 70.0f, 18.0f, valueBrush, formatSmallBold_);

        constexpr int segmentCount = 10;
        constexpr float x = 93.0f;
        constexpr float y = 126.0f;
        constexpr float segmentWidth = 13.0f;
        constexpr float segmentGap = 3.0f;
        const int lit = static_cast<int>(std::round(memVisible_ * segmentCount));
        for (int i = 0; i < segmentCount; ++i) {
            const auto rect = D2D1::RoundedRect(
                D2D1::RectF(x + i * (segmentWidth + segmentGap), y, x + i * (segmentWidth + segmentGap) + segmentWidth, y + 8.0f),
                2.0f,
                2.0f);
            renderTarget_->FillRoundedRectangle(rect, i < lit ? valueBrush : brushTrack_);
        }
    }

    void DrawHistory() {
        DrawTextAt(L"CPU", 22.0f, 143.0f, 28.0f, 14.0f, brushMuted_, formatTiny_);
        DrawTextAt(L"MEM", 22.0f, 160.0f, 28.0f, 14.0f, brushMuted_, formatTiny_);
        DrawSparkline(cpuHistory_, D2D1::RectF(52.0f, 143.0f, 278.0f, 156.0f), brushCpu_);
        DrawSparkline(memHistory_, D2D1::RectF(52.0f, 160.0f, 278.0f, 173.0f), brushMem_);
    }

    void DrawCompact() {
        ID2D1SolidColorBrush* cpuBrush = cpuVisible_ > 0.85f ? brushDanger_ : brushCpu_;
        ID2D1SolidColorBrush* memBrush = memVisible_ > 0.85f ? brushDanger_ : brushMem_;

        wchar_t cpu[24]{};
        wchar_t mem[24]{};
        std::swprintf(cpu, 24, L"CPU %d%%", static_cast<int>(std::round(cpuVisible_ * 100.0f)));
        std::swprintf(mem, 24, L"MEM %d%%", static_cast<int>(std::round(memVisible_ * 100.0f)));
        DrawTextAt(cpu, 18.0f, 39.0f, 70.0f, 18.0f, cpuBrush, formatSmallBold_);
        DrawTextAt(mem, 116.0f, 39.0f, 70.0f, 18.0f, memBrush, formatSmallBold_);

        DrawSparkline(cpuHistory_, D2D1::RectF(18.0f, 58.0f, 102.0f, 70.0f), brushCpu_);
        DrawSparkline(memHistory_, D2D1::RectF(116.0f, 58.0f, 210.0f, 70.0f), brushMem_);
    }

    void DrawSparkline(const History& history, D2D1_RECT_F rect, ID2D1SolidColorBrush* brush) {
        const int count = history.Count();
        if (count < 2) {
            return;
        }

        ID2D1PathGeometry* geometry = nullptr;
        ID2D1GeometrySink* sink = nullptr;
        if (FAILED(factory_->CreatePathGeometry(&geometry))) {
            return;
        }
        if (FAILED(geometry->Open(&sink))) {
            SafeRelease(&geometry);
            return;
        }

        const float width = rect.right - rect.left;
        const float height = rect.bottom - rect.top;
        for (int i = 0; i < count; ++i) {
            const float x = rect.left + width * (static_cast<float>(i) / static_cast<float>(count - 1));
            const float y = rect.bottom - height * history.At(i);
            if (i == 0) {
                sink->BeginFigure(D2D1::Point2F(x, y), D2D1_FIGURE_BEGIN_HOLLOW);
            } else {
                sink->AddLine(D2D1::Point2F(x, y));
            }
        }

        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        renderTarget_->DrawGeometry(geometry, brush, 1.4f);

        SafeRelease(&sink);
        SafeRelease(&geometry);
    }

    void DrawArc(D2D1_POINT_2F center, float radius, float startDegrees, float endDegrees, ID2D1SolidColorBrush* brush, float strokeWidth) {
        if (std::fabs(endDegrees - startDegrees) < 0.01f) {
            return;
        }

        ID2D1PathGeometry* geometry = nullptr;
        ID2D1GeometrySink* sink = nullptr;
        if (FAILED(factory_->CreatePathGeometry(&geometry))) {
            return;
        }
        if (FAILED(geometry->Open(&sink))) {
            SafeRelease(&geometry);
            return;
        }

        const float start = DegreesToRadians(startDegrees);
        const float end = DegreesToRadians(endDegrees);
        const auto p1 = D2D1::Point2F(center.x + std::cos(start) * radius, center.y + std::sin(start) * radius);
        const auto p2 = D2D1::Point2F(center.x + std::cos(end) * radius, center.y + std::sin(end) * radius);
        const float sweep = std::fabs(endDegrees - startDegrees);

        sink->BeginFigure(p1, D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment(
            p2,
            D2D1::SizeF(radius, radius),
            0.0f,
            D2D1_SWEEP_DIRECTION_CLOCKWISE,
            sweep > 180.0f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();

        renderTarget_->DrawGeometry(geometry, brush, strokeWidth);
        SafeRelease(&sink);
        SafeRelease(&geometry);
    }

    float DegreesToRadians(float degrees) const {
        return degrees * kPi / 180.0f;
    }

    void DrawTextAt(
        const wchar_t* text,
        float x,
        float y,
        float width,
        float height,
        ID2D1SolidColorBrush* brush,
        IDWriteTextFormat* format) {
        renderTarget_->DrawTextW(
            text,
            static_cast<UINT32>(std::wcslen(text)),
            format,
            D2D1::RectF(x, y, x + width, y + height),
            brush,
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    HRESULT CreateTextFormats() {
        HRESULT hr = CreateTextFormat(9.0f, DWRITE_FONT_WEIGHT_NORMAL, &formatTiny_);
        if (FAILED(hr)) return hr;
        hr = CreateTextFormat(10.0f, DWRITE_FONT_WEIGHT_MEDIUM, &formatSmallMedium_);
        if (FAILED(hr)) return hr;
        hr = CreateTextFormat(10.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &formatSmallBold_);
        if (FAILED(hr)) return hr;
        return CreateTextFormat(20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &formatValue_);
    }

    HRESULT CreateTextFormat(float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** format) {
        HRESULT hr = writeFactory_->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            weight,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            size,
            L"",
            format);
        if (SUCCEEDED(hr)) {
            (*format)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            (*format)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        return hr;
    }

    void SetSampleInterval(UINT intervalMs) {
        sampleIntervalMs_ = intervalMs;
        SetTimer(hwnd_, kSampleTimer, sampleIntervalMs_, nullptr);
    }

    void SetClickThrough(bool enabled) {
        const bool changed = clickThrough_ != enabled;
        clickThrough_ = enabled;
        ApplyWindowOptions();
        if (changed && clickThrough_) {
            ShowClickThroughNotice();
        }
    }

    void SetEdgeDockEnabled(bool enabled) {
        edgeDockEnabled_ = enabled;
        KillTimer(hwnd_, kDockHideTimer);
        if (!edgeDockEnabled_) {
            if (dockEdge_ != DockEdge::None) {
                dockHidden_ = false;
                dockExpandedRect_ = CalculateExpandedDockRect(dockEdge_);
                AnimateTo(dockExpandedRect_);
            }
            dockHidden_ = false;
            dockEdge_ = DockEdge::None;
            return;
        }
        TryDockNearEdge();
    }

    void ApplyWindowOptions() {
        LONG_PTR exStyle = GetWindowLongPtr(hwnd_, GWL_EXSTYLE);
        exStyle |= WS_EX_LAYERED | WS_EX_TOOLWINDOW;
        if (clickThrough_) {
            exStyle |= WS_EX_TRANSPARENT;
        } else {
            exStyle &= ~WS_EX_TRANSPARENT;
        }
        SetWindowLongPtr(hwnd_, GWL_EXSTYLE, exStyle);

        SetLayeredWindowAttributes(hwnd_, 0, opacity_, LWA_ALPHA);

        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        const int width = compactMode_ ? kCompactWidth : kWindowWidth;
        const int height = compactMode_ ? kCompactHeight : kWindowHeight;
        SetWindowPos(
            hwnd_,
            alwaysOnTop_ ? HWND_TOPMOST : HWND_NOTOPMOST,
            rect.left,
            rect.top,
            width,
            height,
            SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    void TrackMouseLeave(bool nonClient) {
        if (trackingMouse_) {
            return;
        }

        TRACKMOUSEEVENT event{};
        event.cbSize = sizeof(event);
        event.dwFlags = TME_LEAVE | (nonClient ? TME_NONCLIENT : 0);
        event.hwndTrack = hwnd_;
        if (TrackMouseEvent(&event)) {
            trackingMouse_ = true;
        }
    }

    void TryDockNearEdge() {
        RECT rect{};
        GetWindowRect(hwnd_, &rect);

        const RECT workArea = GetCurrentWorkArea();
        const int distanceLeft = std::abs(rect.left - workArea.left);
        const int distanceRight = std::abs(workArea.right - rect.right);
        const int distanceTop = std::abs(rect.top - workArea.top);

        DockEdge edge = DockEdge::None;
        int bestDistance = kDockSnapDistance + 1;
        if (distanceLeft <= kDockSnapDistance && distanceLeft < bestDistance) {
            edge = DockEdge::Left;
            bestDistance = distanceLeft;
        }
        if (distanceRight <= kDockSnapDistance && distanceRight < bestDistance) {
            edge = DockEdge::Right;
            bestDistance = distanceRight;
        }
        if (distanceTop <= kDockSnapDistance && distanceTop < bestDistance) {
            edge = DockEdge::Top;
        }

        if (edge == DockEdge::None) {
            dockEdge_ = DockEdge::None;
            dockHidden_ = false;
            dockAnimating_ = false;
            return;
        }

        DockToEdge(edge, true);
    }

    void DockToEdge(DockEdge edge, bool hideAfterDelay) {
        dockEdge_ = edge;
        dockHidden_ = false;
        dockExpandedRect_ = CalculateExpandedDockRect(edge);
        AnimateTo(dockExpandedRect_);
        if (hideAfterDelay) {
            SetTimer(hwnd_, kDockHideTimer, kDockHideDelayMs, nullptr);
        }
    }

    void ShowDockExpanded() {
        KillTimer(hwnd_, kDockHideTimer);
        dockHidden_ = false;
        dockExpandedRect_ = CalculateExpandedDockRect(dockEdge_);
        AnimateTo(dockExpandedRect_);
    }

    void HideDock() {
        if (!edgeDockEnabled_ || dockEdge_ == DockEdge::None) {
            return;
        }
        dockHidden_ = true;
        AnimateTo(CalculateHiddenDockRect(dockEdge_));
    }

    void StepDockAnimation() {
        if (!dockAnimating_) {
            return;
        }

        RECT current{};
        GetWindowRect(hwnd_, &current);
        RECT next{
            StepToward(current.left, dockTargetRect_.left),
            StepToward(current.top, dockTargetRect_.top),
            StepToward(current.right, dockTargetRect_.right),
            StepToward(current.bottom, dockTargetRect_.bottom),
        };

        const bool done =
            std::abs(next.left - dockTargetRect_.left) <= 1 &&
            std::abs(next.top - dockTargetRect_.top) <= 1 &&
            std::abs(next.right - dockTargetRect_.right) <= 1 &&
            std::abs(next.bottom - dockTargetRect_.bottom) <= 1;

        if (done) {
            next = dockTargetRect_;
            dockAnimating_ = false;
        }

        SetWindowPos(
            hwnd_,
            nullptr,
            next.left,
            next.top,
            next.right - next.left,
            next.bottom - next.top,
            SWP_NOACTIVATE | SWP_NOZORDER);
    }

    int StepToward(int current, int target) {
        return current + static_cast<int>(std::round((target - current) * 0.35f));
    }

    void AnimateTo(RECT target) {
        dockTargetRect_ = target;
        dockAnimating_ = true;
    }

    RECT CalculateExpandedDockRect(DockEdge edge) {
        const RECT workArea = GetCurrentWorkArea();
        const SIZE size = CurrentWindowSize();
        RECT current{};
        GetWindowRect(hwnd_, &current);

        int x = current.left;
        int y = current.top;
        const int left = static_cast<int>(workArea.left);
        const int top = static_cast<int>(workArea.top);
        const int right = static_cast<int>(workArea.right);
        const int bottom = static_cast<int>(workArea.bottom);
        const int width = static_cast<int>(size.cx);
        const int height = static_cast<int>(size.cy);
        if (edge == DockEdge::Left) {
            x = left;
            y = std::clamp(y, top, bottom - height);
        } else if (edge == DockEdge::Right) {
            x = right - width;
            y = std::clamp(y, top, bottom - height);
        } else if (edge == DockEdge::Top) {
            x = std::clamp(x, left, right - width);
            y = top;
        }

        return RECT{x, y, x + width, y + height};
    }

    RECT CalculateHiddenDockRect(DockEdge edge) {
        RECT rect = dockExpandedRect_;
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        const RECT workArea = GetCurrentWorkArea();

        if (edge == DockEdge::Left) {
            rect.left = workArea.left - width + kDockHandleSize;
            rect.right = rect.left + width;
        } else if (edge == DockEdge::Right) {
            rect.left = workArea.right - kDockHandleSize;
            rect.right = rect.left + width;
        } else if (edge == DockEdge::Top) {
            rect.top = workArea.top - height + kDockHandleSize;
            rect.bottom = rect.top + height;
        }
        return rect;
    }

    RECT GetCurrentWorkArea() {
        HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info)) {
            return info.rcWork;
        }

        RECT fallback{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &fallback, 0);
        return fallback;
    }

    SIZE CurrentWindowSize() const {
        return SIZE{
            compactMode_ ? kCompactWidth : kWindowWidth,
            compactMode_ ? kCompactHeight : kWindowHeight,
        };
    }

    void AddTrayIcon() {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = hwnd_;
        data.uID = kTrayIconId;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        data.uCallbackMessage = kTrayMessage;
        data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        lstrcpynW(data.szTip, L"PulseDial", ARRAYSIZE(data.szTip));
        Shell_NotifyIconW(NIM_ADD, &data);
    }

    void RemoveTrayIcon() {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = hwnd_;
        data.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &data);
    }

    void ShowClickThroughNotice() {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = hwnd_;
        data.uID = kTrayIconId;
        data.uFlags = NIF_INFO;
        lstrcpynW(data.szInfoTitle, L"PulseDial click-through enabled", ARRAYSIZE(data.szInfoTitle));
        lstrcpynW(data.szInfo, L"Press Ctrl+Alt+P or use the tray menu to disable it.", ARRAYSIZE(data.szInfo));
        data.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    bool ReadCpuTimes(FILETIME& idle, FILETIME& kernel, FILETIME& user) {
        return GetSystemTimes(&idle, &kernel, &user) == TRUE;
    }

    float ReadCpuLoad() {
        FILETIME idle{};
        FILETIME kernel{};
        FILETIME user{};
        if (!ReadCpuTimes(idle, kernel, user)) {
            return cpuTarget_;
        }

        if (!previousCpuValid_) {
            previousIdle_ = idle;
            previousKernel_ = kernel;
            previousUser_ = user;
            previousCpuValid_ = true;
            return cpuTarget_;
        }

        const auto idleDelta = FileTimeToUInt64(SubtractFileTime(idle, previousIdle_));
        const auto kernelDelta = FileTimeToUInt64(SubtractFileTime(kernel, previousKernel_));
        const auto userDelta = FileTimeToUInt64(SubtractFileTime(user, previousUser_));
        const auto total = kernelDelta + userDelta;

        previousIdle_ = idle;
        previousKernel_ = kernel;
        previousUser_ = user;

        if (total == 0) {
            return cpuTarget_;
        }

        return Clamp01(static_cast<float>(total - idleDelta) / static_cast<float>(total));
    }

    float ReadMemoryLoad() {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (!GlobalMemoryStatusEx(&status)) {
            return memTarget_;
        }
        return Clamp01(static_cast<float>(status.dwMemoryLoad) / 100.0f);
    }

    HWND hwnd_ = nullptr;
    ID2D1Factory* factory_ = nullptr;
    IDWriteFactory* writeFactory_ = nullptr;
    IDWriteTextFormat* formatTiny_ = nullptr;
    IDWriteTextFormat* formatSmallMedium_ = nullptr;
    IDWriteTextFormat* formatSmallBold_ = nullptr;
    IDWriteTextFormat* formatValue_ = nullptr;
    ID2D1HwndRenderTarget* renderTarget_ = nullptr;
    ID2D1SolidColorBrush* brushPanel_ = nullptr;
    ID2D1SolidColorBrush* brushBorder_ = nullptr;
    ID2D1SolidColorBrush* brushText_ = nullptr;
    ID2D1SolidColorBrush* brushMuted_ = nullptr;
    ID2D1SolidColorBrush* brushCpu_ = nullptr;
    ID2D1SolidColorBrush* brushMem_ = nullptr;
    ID2D1SolidColorBrush* brushDanger_ = nullptr;
    ID2D1SolidColorBrush* brushTrack_ = nullptr;

    History cpuHistory_;
    History memHistory_;
    float cpuTarget_ = 0.0f;
    float memTarget_ = 0.0f;
    float cpuVisible_ = 0.0f;
    float memVisible_ = 0.0f;
    UINT sampleIntervalMs_ = 1000;
    BYTE opacity_ = 204;
    bool alwaysOnTop_ = true;
    bool clickThrough_ = false;
    bool compactMode_ = false;
    bool edgeDockEnabled_ = false;
    bool dockHidden_ = false;
    bool dockAnimating_ = false;
    bool trackingMouse_ = false;
    DockEdge dockEdge_ = DockEdge::None;
    RECT dockExpandedRect_{};
    RECT dockTargetRect_{};
    bool previousCpuValid_ = false;
    FILETIME previousIdle_{};
    FILETIME previousKernel_{};
    FILETIME previousUser_{};
};

PulseDialApp* GetApp(HWND hwnd) {
    return reinterpret_cast<PulseDialApp*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        auto* app = new PulseDialApp(hwnd);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        if (FAILED(app->Initialize())) {
            delete app;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
            return -1;
        }

        SetTimer(hwnd, kSampleTimer, 1000, nullptr);
        SetTimer(hwnd, kFrameTimer, 33, nullptr);
        RegisterHotKey(hwnd, kHotkeyToggleClickThrough, MOD_CONTROL | MOD_ALT, 'P');
        return 0;
    }
    case WM_DESTROY: {
        UnregisterHotKey(hwnd, kHotkeyToggleClickThrough);
        KillTimer(hwnd, kSampleTimer);
        KillTimer(hwnd, kFrameTimer);
        KillTimer(hwnd, kDockHideTimer);
        delete GetApp(hwnd);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        PostQuitMessage(0);
        return 0;
    }
    case WM_SIZE:
        if (auto* app = GetApp(hwnd)) {
            app->Resize();
        }
        return 0;
    case WM_TIMER:
        if (auto* app = GetApp(hwnd)) {
            if (wParam == kSampleTimer) {
                app->Sample();
            } else if (wParam == kFrameTimer) {
                app->TickFrame();
            } else if (wParam == kDockHideTimer) {
                KillTimer(hwnd, kDockHideTimer);
                app->HandleDockHideTimer();
            }
        }
        return 0;
    case WM_COMMAND:
        if (auto* app = GetApp(hwnd)) {
            app->HandleCommand(LOWORD(wParam));
        }
        return 0;
    case WM_CONTEXTMENU: {
        if (auto* app = GetApp(hwnd)) {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (point.x == -1 && point.y == -1) {
                RECT rect{};
                GetWindowRect(hwnd, &rect);
                point = POINT{rect.left + 24, rect.top + 24};
            }
            app->ShowContextMenu(point);
        }
        return 0;
    }
    case WM_NCRBUTTONUP: {
        if (auto* app = GetApp(hwnd)) {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            app->ShowContextMenu(point);
        }
        return 0;
    }
    case WM_HOTKEY:
        if (wParam == kHotkeyToggleClickThrough) {
            if (auto* app = GetApp(hwnd)) {
                app->ToggleClickThrough();
            }
        }
        return 0;
    case WM_MOUSEMOVE:
        if (auto* app = GetApp(hwnd)) {
            app->HandleMouseMove();
        }
        return 0;
    case WM_NCMOUSEMOVE:
        if (auto* app = GetApp(hwnd)) {
            app->HandleMouseMove(true);
        }
        return DefWindowProc(hwnd, message, wParam, lParam);
    case WM_MOUSELEAVE:
    case WM_NCMOUSELEAVE:
        if (auto* app = GetApp(hwnd)) {
            app->HandleMouseLeave();
        }
        return 0;
    case WM_EXITSIZEMOVE:
        if (auto* app = GetApp(hwnd)) {
            app->HandleExitSizeMove();
        }
        return 0;
    case kTrayMessage:
        if (auto* app = GetApp(hwnd)) {
            app->HandleTrayMessage(lParam);
        }
        return 0;
    case WM_PAINT:
    case WM_DISPLAYCHANGE: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        if (auto* app = GetApp(hwnd)) {
            app->Render();
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST: {
        if (auto* app = GetApp(hwnd); app && app->IsClickThrough()) {
            return HTTRANSPARENT;
        }
        const LRESULT hit = DefWindowProc(hwnd, message, wParam, lParam);
        return hit == HTCLIENT ? HTCAPTION : hit;
    }
    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    const wchar_t* className = L"PulseDialWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED;
    const DWORD style = WS_POPUP;
    HWND hwnd = CreateWindowExW(
        exStyle,
        className,
        L"PulseDial",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!hwnd) {
        CoUninitialize();
        return 0;
    }

    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &dark, sizeof(dark));

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return 0;
}
