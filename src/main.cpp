#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <cstdint>
#include "win32_input.h"

namespace {

constexpr wchar_t kClassName[] = L"WClicker.Main";
constexpr UINT_PTR kTimer = 1;
constexpr int kToggle = 101;
constexpr int kStop = 102;
constexpr int kExit = 103;

class Application {
public:
    Application() : engine_(input_) {
        HDC screen = GetDC(nullptr);
        if (screen) {
            dpi_ = GetDeviceCaps(screen, LOGPIXELSX);
            ReleaseDC(nullptr, screen);
        }
        title_font_ = Font(27, FW_BOLD);
        status_font_ = Font(21, FW_SEMIBOLD);
        body_font_ = Font(14, FW_NORMAL);
        small_font_ = Font(12, FW_NORMAL);
    }

    ~Application() {
        engine_.Stop();
        DeleteObject(title_font_);
        DeleteObject(status_font_);
        DeleteObject(body_font_);
        DeleteObject(small_font_);
    }

    int Scale(int value) const { return MulDiv(value, dpi_, 96); }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            app = static_cast<Application*>(create->lpCreateParams);
            app->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        return app ? app->Handle(message, wparam, lparam)
                   : DefWindowProcW(window, message, wparam, lparam);
    }

private:
    HFONT Font(int height, int weight) const {
        return CreateFontW(-Scale(height), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    }

    HWND Button(const wchar_t* text, int id, int x, int width) {
        HWND button = CreateWindowExW(0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            Scale(x), Scale(220), Scale(width), Scale(38), window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(body_font_), TRUE);
        return button;
    }

    bool Initialize() {
        toggle_button_ = Button(L"Запустить · F6", kToggle, 24, 208);
        HWND stop = Button(L"Стоп · F8", kStop, 242, 118);
        HWND exit = Button(L"Выход · F10", kExit, 370, 126);
        if (!toggle_button_ || !stop || !exit) return false;

        if (!RegisterHotKey(window_, kToggle, MOD_NOREPEAT, VK_F6) ||
            !RegisterHotKey(window_, kStop, MOD_NOREPEAT, VK_F8) ||
            !RegisterHotKey(window_, kExit, MOD_NOREPEAT, VK_F10)) {
            MessageBoxW(window_, L"Не удалось зарегистрировать F6, F8 или F10.\n"
                L"Закройте программу, которая использует эти сочетания, и запустите WClicker снова.",
                L"WClicker — горячие клавиши заняты", MB_OK | MB_ICONERROR);
            return false;
        }
        if (!SetTimer(window_, kTimer, 20, nullptr)) return false;
        Update();
        return true;
    }

    void Toggle() {
        if (closing_) return;
        if (engine_.state() != wclicker::State::Running) {
            // Minimize before injecting the first click so our own buttons
            // cannot receive the automated click and toggle the state again.
            ShowWindow(window_, SW_MINIMIZE);
        }
        engine_.Toggle(GetTickCount64());
        Update();
    }

    void Close() {
        closing_ = true;
        if (engine_.Stop()) {
            DestroyWindow(window_);
        } else {
            Update();
        }
    }

    void Update() {
        const auto state = engine_.state();
        const auto error = engine_.error();
        if (error != wclicker::Error::None && error != displayed_error_) {
            ShowWindow(window_, SW_RESTORE);
            FLASHWINFO flash{sizeof(FLASHWINFO), window_, FLASHW_TRAY, 3, 0};
            FlashWindowEx(&flash);
        }
        displayed_error_ = error;
        displayed_state_ = state;
        const wchar_t* title = L"WClicker — остановлен";
        const wchar_t* button = L"Запустить · F6";
        if (state == wclicker::State::Running) {
            title = L"WClicker — работает";
            button = L"Пауза · F6";
        } else if (state == wclicker::State::Paused) {
            title = L"WClicker — пауза";
            button = L"Продолжить · F6";
        }
        if (error != wclicker::Error::None) title = L"WClicker — ошибка ввода";
        SetWindowTextW(window_, title);
        SetWindowTextW(toggle_button_, button);
        InvalidateRect(window_, nullptr, FALSE);
    }

    void Text(HDC dc, const wchar_t* text, HFONT font, COLORREF color,
              int left, int top, int right, int bottom, UINT flags = DT_LEFT | DT_SINGLELINE) {
        SelectObject(dc, font);
        SetTextColor(dc, color);
        RECT area{Scale(left), Scale(top), Scale(right), Scale(bottom)};
        DrawTextW(dc, text, -1, &area, flags);
    }

    void Paint() {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window_, &paint);
        const int saved = SaveDC(dc);
        RECT client{};
        GetClientRect(window_, &client);
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        SetBkMode(dc, TRANSPARENT);
        Text(dc, L"WClicker", title_font_, RGB(24, 35, 52), 24, 20, 496, 57);
        Text(dc, L"Удержание W  +  левый клик каждую секунду", body_font_,
             RGB(82, 97, 115), 24, 66, 496, 92);

        HBRUSH card = CreateSolidBrush(RGB(242, 245, 249));
        SelectObject(dc, card);
        SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, Scale(24), Scale(108), Scale(496), Scale(200), Scale(12), Scale(12));

        const wchar_t* status = L"Готов к запуску";
        const wchar_t* detail = L"Откройте нужное окно и нажмите F6.";
        COLORREF color = RGB(49, 66, 87);
        if (engine_.state() == wclicker::State::Running) {
            status = L"Работает";
            detail = L"W зажата. F6 — пауза, F8 — стоп.";
            color = RGB(22, 119, 72);
        } else if (engine_.state() == wclicker::State::Paused) {
            status = L"Пауза";
            detail = L"W отпущена. Нажмите F6, чтобы продолжить.";
            color = RGB(151, 101, 8);
        }
        if (engine_.error() == wclicker::Error::Release) {
            status = L"Ожидание отпускания клавиш";
            detail = L"Ввод заблокирован. Переключитесь в обычное окно.";
            color = RGB(177, 39, 48);
        } else if (engine_.error() != wclicker::Error::None) {
            status = L"Ввод недоступен";
            detail = L"Проверьте права целевого приложения. Затем F6.";
            color = RGB(177, 39, 48);
        }
        Text(dc, status, status_font_, color, 40, 121, 480, 154);
        Text(dc, detail, small_font_, RGB(82, 97, 115), 40, 163, 480, 190);
        Text(dc, L"При запуске окно сворачивается. Горячие клавиши глобальные.",
             small_font_, RGB(82, 97, 115), 24, 278, 496, 301);
        Text(dc, L"Пауза, стоп и выход отпускают W и прекращают клики.",
             small_font_, RGB(82, 97, 115), 24, 302, 496, 326);
        RestoreDC(dc, saved);
        DeleteObject(card);
        EndPaint(window_, &paint);
    }

    LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_CREATE:
            return Initialize() ? 0 : -1;
        case WM_COMMAND:
        case WM_HOTKEY: {
            const int id = message == WM_HOTKEY ? static_cast<int>(wparam) : LOWORD(wparam);
            if (id == kToggle) Toggle();
            else if (id == kStop) { engine_.Stop(); Update(); }
            else if (id == kExit) Close();
            return 0;
        }
        case WM_TIMER:
            if (wparam == kTimer) {
                const bool ok = engine_.Tick(GetTickCount64());
                if (closing_ && ok) { DestroyWindow(window_); return 0; }
                if (displayed_state_ != engine_.state() || displayed_error_ != engine_.error()) Update();
            }
            return 0;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_CLOSE:
            Close();
            return 0;
        case WM_QUERYENDSESSION:
            engine_.Stop();
            return TRUE;
        case WM_ENDSESSION:
            if (wparam) engine_.Stop();
            return 0;
        case WM_DESTROY:
            KillTimer(window_, kTimer);
            UnregisterHotKey(window_, kToggle);
            UnregisterHotKey(window_, kStop);
            UnregisterHotKey(window_, kExit);
            engine_.Stop();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window_, message, wparam, lparam);
        }
    }

    wclicker::Win32Input input_;
    wclicker::Engine engine_;
    HWND window_ = nullptr;
    HWND toggle_button_ = nullptr;
    HFONT title_font_ = nullptr;
    HFONT status_font_ = nullptr;
    HFONT body_font_ = nullptr;
    HFONT small_font_ = nullptr;
    int dpi_ = 96;
    bool closing_ = false;
    wclicker::State displayed_state_ = wclicker::State::Stopped;
    wclicker::Error displayed_error_ = wclicker::Error::None;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\qqSteve.WClicker.SingleInstance");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        MessageBoxW(nullptr, L"WClicker уже запущен или недоступен. Проверьте панель задач.",
                    L"WClicker", MB_OK | MB_ICONINFORMATION);
        return 1;
    }

    Application app;
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = Application::WindowProc;
    window_class.hInstance = instance;
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hIconSm = window_class.hIcon;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    window_class.lpszClassName = kClassName;
    if (!RegisterClassExW(&window_class)) {
        CloseHandle(mutex);
        return 1;
    }

    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT size{0, 0, app.Scale(520), app.Scale(342)};
    AdjustWindowRectEx(&size, style, FALSE, 0);
    HWND window = CreateWindowExW(0, kClassName, L"WClicker", style,
        CW_USEDEFAULT, CW_USEDEFAULT, size.right - size.left, size.bottom - size.top,
        nullptr, nullptr, instance, &app);
    if (!window) {
        CloseHandle(mutex);
        return 1;
    }
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    int result = 0;
    BOOL received = 0;
    while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (received == -1) result = 1;
    CloseHandle(mutex);
    return result;
}
