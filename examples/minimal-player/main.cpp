#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <string>
#include <cstdio>
#include "player_core.h"

namespace {

constexpr int IDC_BTN_LOAD   = 101;
constexpr int IDC_BTN_START  = 102;
constexpr int IDC_BTN_PAUSE  = 103;
constexpr int IDC_BTN_STOP   = 104;
constexpr int IDC_LBL_FILE   = 105;
constexpr int IDC_LBL_STAT   = 106;
constexpr int IDC_LBL_TIME   = 107;
constexpr int IDC_SEEKBAR    = 108;

constexpr int SEEBAR_ID = 200;

HWND g_hBtnLoad  = nullptr;
HWND g_hBtnStart = nullptr;
HWND g_hBtnPause = nullptr;
HWND g_hBtnStop  = nullptr;
HWND g_hLblFile  = nullptr;
HWND g_hLblStat  = nullptr;
HWND g_hLblTime  = nullptr;
HWND g_hSeekbar  = nullptr;

pt7::player::AudioPlayerCore* g_player = nullptr;

// Seek bar state
bool g_seeking = false;         // true while user is dragging
double g_seek_drag_pos = 0.0;   // position shown during drag

std::wstring to_wide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), &wstr[0], size);
    return wstr;
}

std::string format_mmss(double seconds) {
    if (seconds < 0) seconds = 0;
    int total = static_cast<int>(seconds + 0.5);
    int m = total / 60;
    int s = total % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    return std::string(buf);
}

double get_display_position() {
    if (g_seeking) return g_seek_drag_pos;
    if (g_player) return g_player->get_position_seconds();
    return 0.0;
}

double get_display_duration() {
    if (g_player) return g_player->get_duration_seconds();
    return 0.0;
}

void update_seekbar() {
    if (g_hSeekbar) InvalidateRect(g_hSeekbar, nullptr, TRUE);
}

void update_time_label() {
    if (!g_hLblTime) return;
    double pos = get_display_position();
    double dur = get_display_duration();
    std::string text = format_mmss(pos) + " / " + format_mmss(dur);
    SetWindowTextW(g_hLblTime, to_wide(text).c_str());
}

// Seek bar Window Proc (custom-drawn)
LRESULT CALLBACK SeekbarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rc;
            GetClientRect(hwnd, &rc);
            const int w = rc.right - rc.left;

            // Background
            FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));

            // Filled portion
            double dur = get_display_duration();
            double pos = get_display_position();
            double frac = (dur > 0.0) ? (pos / dur) : 0.0;
            if (frac < 0.0) frac = 0.0;
            if (frac > 1.0) frac = 1.0;

            RECT rcFill = rc;
            rcFill.right = rc.left + static_cast<int>(frac * w);

            HBRUSH hbrFill = CreateSolidBrush(RGB(0, 120, 215));
            FillRect(hdc, &rcFill, hbrFill);
            DeleteObject(hbrFill);

            // Border
            FrameRect(hdc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

            // Knob
            int knobX = rc.left + static_cast<int>(frac * w);
            RECT rcKnob;
            rcKnob.left = knobX - 5;
            rcKnob.right = knobX + 5;
            rcKnob.top = rc.top + 2;
            rcKnob.bottom = rc.bottom - 2;
            HBRUSH hbrKnob = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(hdc, &rcKnob, hbrKnob);
            DeleteObject(hbrKnob);
            FrameRect(hdc, &rcKnob, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            SetCapture(hwnd);
            g_seeking = true;
            // fall through to compute position
        }
        // fall through

        case WM_MOUSEMOVE: {
            if (!g_seeking) return 0;

            RECT rc;
            GetClientRect(hwnd, &rc);
            int x = GET_X_LPARAM(lParam);
            double frac = static_cast<double>(x - rc.left) / (rc.right - rc.left);
            if (frac < 0.0) frac = 0.0;
            if (frac > 1.0) frac = 1.0;

            double dur = get_display_duration();
            g_seek_drag_pos = frac * dur;

            InvalidateRect(hwnd, nullptr, FALSE);
            update_time_label();
            return 0;
        }

        case WM_LBUTTONUP: {
            if (!g_seeking) return 0;
            g_seeking = false;
            ReleaseCapture();

            // Apply the seek
            if (g_player && g_player->get_state() != pt7::player::PlayerState::NoFile) {
                g_player->seek(g_seek_drag_pos);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            update_time_label();
            return 0;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void register_seekbar_class() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = SeekbarProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH));
    wc.lpszClassName = L"PT7MP3_Seekbar";
    RegisterClassExW(&wc);
}

void update_ui_state(HWND hwnd) {
    if (!g_player) return;

    pt7::player::PlayerState st = g_player->get_state();
    std::wstring fileName = g_player->get_file_name();

    std::wstring fileText = L"File: " + (fileName.empty() ? L"(No file loaded)" : fileName);
    SetWindowTextW(g_hLblFile, fileText.c_str());

    std::wstring statText = L"Status: " + to_wide(g_player->get_state_string());
    if (st == pt7::player::PlayerState::Playing || st == pt7::player::PlayerState::Loaded || st == pt7::player::PlayerState::Paused) {
        const auto& info = g_player->get_stream_info();
        if (info.sample_rate > 0) {
            statText += L" (" + to_wide(info.mpeg_version) + L", " +
                        std::to_wstring(info.sample_rate) + L" Hz, " +
                        (info.channels == 1 ? L"Mono" : L"Stereo") + L", " +
                        std::to_wstring(info.bitrate_kbps) + L" kbps)";
        }
    }
    SetWindowTextW(g_hLblStat, statText.c_str());

    switch (st) {
        case pt7::player::PlayerState::NoFile:
            EnableWindow(g_hBtnLoad,  TRUE);
            EnableWindow(g_hBtnStart, FALSE);
            EnableWindow(g_hBtnPause, FALSE);
            EnableWindow(g_hBtnStop,  FALSE);
            break;
        case pt7::player::PlayerState::Loaded:
        case pt7::player::PlayerState::Stopped:
            EnableWindow(g_hBtnLoad,  TRUE);
            EnableWindow(g_hBtnStart, TRUE);
            EnableWindow(g_hBtnPause, FALSE);
            EnableWindow(g_hBtnStop,  FALSE);
            break;
        case pt7::player::PlayerState::Playing:
            EnableWindow(g_hBtnLoad,  TRUE);
            EnableWindow(g_hBtnStart, FALSE);
            EnableWindow(g_hBtnPause, TRUE);
            EnableWindow(g_hBtnStop,  TRUE);
            break;
        case pt7::player::PlayerState::Paused:
            EnableWindow(g_hBtnLoad,  TRUE);
            EnableWindow(g_hBtnStart, TRUE);
            EnableWindow(g_hBtnPause, FALSE);
            EnableWindow(g_hBtnStop,  TRUE);
            break;
    }
    (void)hwnd;
}

void on_load_file(HWND hwnd) {
    wchar_t szFile[MAX_PATH] = { 0 };

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"MP3 Audio (*.mp3)\0*.mp3\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        std::string err;
        if (!g_player->load_file(szFile, &err)) {
            std::wstring werr = to_wide(err);
            MessageBoxW(hwnd, werr.empty() ? L"Failed to load MP3 file." : werr.c_str(),
                        L"Error Loading MP3", MB_OK | MB_ICONERROR);
        }
        update_ui_state(hwnd);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            register_seekbar_class();
            HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            // File label
            g_hLblFile = CreateWindowExW(0, L"STATIC", L"File: (No file loaded)",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS,
                16, 14, 432, 20, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LBL_FILE)), nullptr, nullptr);

            // Status label
            g_hLblStat = CreateWindowExW(0, L"STATIC", L"Status: No file",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                16, 38, 432, 20, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LBL_STAT)), nullptr, nullptr);

            // Time label
            g_hLblTime = CreateWindowExW(0, L"STATIC", L"00:00 / 00:00",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                16, 64, 432, 18, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LBL_TIME)), nullptr, nullptr);

            // Seek bar (custom-drawn)
            g_hSeekbar = CreateWindowExW(0, L"PT7MP3_Seekbar", L"",
                WS_CHILD | WS_VISIBLE,
                16, 86, 432, 18, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SEEKBAR)), nullptr, nullptr);

            // Buttons
            g_hBtnLoad = CreateWindowExW(0, L"BUTTON", L"Load MP3",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                16, 112, 95, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_LOAD)), nullptr, nullptr);

            g_hBtnStart = CreateWindowExW(0, L"BUTTON", L"Start",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
                120, 112, 95, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_START)), nullptr, nullptr);

            g_hBtnPause = CreateWindowExW(0, L"BUTTON", L"Pause",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
                224, 112, 95, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_PAUSE)), nullptr, nullptr);

            g_hBtnStop = CreateWindowExW(0, L"BUTTON", L"Stop",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
                328, 112, 95, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_STOP)), nullptr, nullptr);

            SendMessageW(g_hLblFile, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
            SendMessageW(g_hLblStat, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
            SendMessageW(g_hLblTime, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
            SendMessageW(g_hBtnLoad, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
            SendMessageW(g_hBtnStart, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
            SendMessageW(g_hBtnPause, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
            SendMessageW(g_hBtnStop, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

            SetTimer(hwnd, 1, 100, nullptr);
            return 0;
        }

        case WM_TIMER: {
            if (wParam == 1) {
                update_ui_state(hwnd);
                update_time_label();
                update_seekbar();
            }
            return 0;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case IDC_BTN_LOAD:
                    on_load_file(hwnd);
                    break;
                case IDC_BTN_START:
                    if (g_player) {
                        if (!g_player->play()) {
                            MessageBoxW(hwnd, L"Playback failed to start.", L"Playback Error", MB_OK | MB_ICONERROR);
                        }
                    }
                    update_ui_state(hwnd);
                    break;
                case IDC_BTN_PAUSE:
                    if (g_player) {
                        g_player->pause();
                    }
                    update_ui_state(hwnd);
                    break;
                case IDC_BTN_STOP:
                    if (g_player) {
                        g_player->stop();
                    }
                    update_ui_state(hwnd);
                    break;
            }
            return 0;
        }

        case WM_CLOSE: {
            KillTimer(hwnd, 1);
            if (g_player) {
                g_player->stop();
            }
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    g_player = new pt7::player::AudioPlayerCore();

    const wchar_t CLASS_NAME[] = L"PT7_MP3_MinimalPlayer_WndClass";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = CLASS_NAME;

    RegisterClassExW(&wc);

    const int wnd_width = 470;
    const int wnd_height = 200;

    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screen_w - wnd_width) / 2;
    int posY = (screen_h - wnd_height) / 2;

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"PT7-MP3 Minimal Player",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        posX, posY, wnd_width, wnd_height,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd) {
        delete g_player;
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    delete g_player;
    g_player = nullptr;

    return static_cast<int>(msg.wParam);
}

// Support standard main() as entry point as well
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    return WinMain(GetModuleHandleW(nullptr), nullptr, nullptr, SW_SHOWNORMAL);
}
