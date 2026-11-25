// =============================================================================
//  Vigil - a tiny Windows 11 tray tool that keeps your PC awake.
//
//  Two layers, used together, so it survives even strict corporate policy:
//    1. SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED [| ES_DISPLAY_REQUIRED])
//       -> tells Windows "stay awake / keep the screen on" (sleep & display timers).
//    2. A periodic, benign synthetic input pulse (default: the F15 key, which no
//       application uses) -> resets the *input idle timer* that GetLastInputInfo
//       reports. Group Policy "Machine inactivity limit" (InactivityTimeoutSecs)
//       and the secure screensaver both lock the machine off that idle timer, so
//       the pulse is what defeats a forced lock while a video plays in the browser.
//
//  Native Win32, single file, no third-party libraries, no admin rights,
//  per-user registry only. Idle CPU ~0%, RAM a few MB.
//
//  Left click  : toggle keep-awake on/off.
//  Right click : full settings menu.
//
//  MIT License.
// =============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <strsafe.h>
#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

// ---------------------------------------------------------------------------
//  Constants
// ---------------------------------------------------------------------------
static const wchar_t* kWndClass   = L"VigilTrayWindowClass";
static const wchar_t* kMutexName  = L"Vigil_SingleInstance_Mutex_{7F3C1A90-2B4D-4E6A-9C11-0A2B3C4D5E6F}";
static const wchar_t* kRunKey     = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunValue   = L"Vigil";

// Simulation methods
enum Method { METHOD_F15 = 0, METHOD_SCROLL = 1, METHOD_MOUSE = 2 };

// ---------------------------------------------------------------------------
//  Settings (persisted to %APPDATA%\Vigil\settings.ini)
// ---------------------------------------------------------------------------
struct Settings {
    bool activeOnStartup = true;   // default keep-awake state when Vigil launches
    bool keepDisplayOn   = true;   // also keep the screen from turning off
    bool startWithWindows= true;   // add to per-user startup
    int  method          = METHOD_F15;
    int  intervalSec     = 50;     // pulse interval (seconds)
    int  sessionMinutes  = 0;      // 0 = run until turned off; else auto-off after N minutes
    int  lang            = -1;     // -1 = auto, 0 = English, 1 = Hebrew
};

static Settings      g_cfg;
static bool          g_enabled      = true;   // current runtime state
static ULONGLONG     g_sessionEnd   = 0;       // GetTickCount64 when a timed session ends (0 = none)
static HWND          g_hwnd         = nullptr;
static HINSTANCE     g_hInst        = nullptr;
static NOTIFYICONDATAW g_nid        = {};
static HICON         g_icoActive    = nullptr;
static HICON         g_icoPaused    = nullptr;
static UINT          g_taskbarCreated = 0;      // "TaskbarCreated" message (Explorer restart)

// ---------------------------------------------------------------------------
//  Localisation (English / Hebrew)
// ---------------------------------------------------------------------------
enum StrId {
    S_TIP_ON, S_TIP_OFF, S_HEADER, S_ST_ON, S_ST_OFF, S_ST_SESSION,
    S_ENABLE, S_KEEPDISPLAY, S_METHOD, S_M_F15, S_M_SCROLL, S_M_MOUSE,
    S_INTERVAL, S_SEC, S_MIN, S_SESSION, S_SESS_OFF, S_HALFHOUR, S_1H, S_2H, S_4H,
    S_ACTIVE_ON_START, S_START_WITH_WIN, S_LANG, S_EN, S_HE, S_ABOUT, S_EXIT,
    S_BALLOON_TITLE, S_BALLOON_SESSION_END, S_ABOUT_TEXT, S_COUNT
};

static const wchar_t* kStr[S_COUNT][2] = {
    /* S_TIP_ON            */ { L"Vigil - keeping your PC awake",      L"Vigil - מונע נעילה" },
    /* S_TIP_OFF           */ { L"Vigil - paused (PC may lock)",       L"Vigil - מושהת (המחשב עלול להינעל)" },
    /* S_HEADER            */ { L"Vigil 1.0.0",                        L"Vigil 1.0.0" },
    /* S_ST_ON             */ { L"Status: ACTIVE",                     L"מצב: פעיל" },
    /* S_ST_OFF            */ { L"Status: paused",                     L"מצב: מושהת" },
    /* S_ST_SESSION        */ { L"Status: ACTIVE (%d min left)",       L"מצב: פעיל (נותרו %d דק׳)" },
    /* S_ENABLE            */ { L"Keep awake",                         L"מניעת נעילה" },
    /* S_KEEPDISPLAY       */ { L"Keep the screen on",                 L"להשאיר את המסך דולק" },
    /* S_METHOD            */ { L"Simulation method",                  L"שיטת סימולציה" },
    /* S_M_F15             */ { L"F15 key (recommended)",              L"מקש F15 (מומלץ)" },
    /* S_M_SCROLL          */ { L"Scroll Lock tap",                    L"הקשת Scroll Lock" },
    /* S_M_MOUSE           */ { L"Mouse nudge",                        L"הזזת עכבר" },
    /* S_INTERVAL          */ { L"Pulse interval",                     L"תדירות פעימה" },
    /* S_SEC               */ { L"%d seconds",                         L"%d שניות" },
    /* S_MIN               */ { L"%d min",                             L"%d דק׳" },
    /* S_SESSION           */ { L"Timed session",                      L"פעילות מתוקצבת" },
    /* S_SESS_OFF          */ { L"Until I turn it off",                L"עד כיבוי ידני" },
    /* S_HALFHOUR          */ { L"30 minutes",                         L"30 דקות" },
    /* S_1H                */ { L"1 hour",                             L"שעה" },
    /* S_2H                */ { L"2 hours",                            L"שעתיים" },
    /* S_4H                */ { L"4 hours",                            L"4 שעות" },
    /* S_ACTIVE_ON_START   */ { L"Active when Vigil starts",           L"פעיל בעת הפעלת Vigil" },
    /* S_START_WITH_WIN    */ { L"Start with Windows",                 L"הפעלה עם Windows" },
    /* S_LANG              */ { L"Language",                           L"שפה" },
    /* S_EN                */ { L"English",                            L"English" },
    /* S_HE                */ { L"עברית",      L"עברית" },
    /* S_ABOUT             */ { L"About Vigil",                        L"אודות Vigil" },
    /* S_EXIT              */ { L"Exit",                               L"יציאה" },
    /* S_BALLOON_TITLE     */ { L"Vigil",                              L"Vigil" },
    /* S_BALLOON_SESSION_END*/{ L"Timed session ended - keep-awake is now off.",
                                L"הפעילות המתוקצבת הסתיימה - מניעת הנעילה כבויה." },
    /* S_ABOUT_TEXT        */ {
        L"Vigil 1.0.0\n\n"
        L"Keeps your PC awake and prevents it from locking while you are away.\n\n"
        L"Left click the tray icon to toggle on/off.\n"
        L"Right click for settings.\n\n"
        L"No admin rights. No network. Open source (MIT).",
        L"Vigil 1.0.0\n\n"
        L"מונע מהמחשב להיכנס למצב שינה ולהינעל כשאינך לידו.\n\n"
        L"לחיצה שמאלית על הסמל – הפעלה/כיבוי.\n"
        L"לחיצה ימנית – הגדרות.\n\n"
        L"ללא הרשאות מנהל. ללא רשת. קוד פתוח (MIT)." }
};

static int LangIdx() {
    if (g_cfg.lang == 0) return 0;
    if (g_cfg.lang == 1) return 1;
    // auto: Hebrew UI -> Hebrew, otherwise English
    LANGID ui = GetUserDefaultUILanguage();
    return (PRIMARYLANGID(ui) == LANG_HEBREW) ? 1 : 0;
}
static const wchar_t* T(StrId id) { return kStr[id][LangIdx()]; }

// ---------------------------------------------------------------------------
//  Settings file path:  %APPDATA%\Vigil\settings.ini
// ---------------------------------------------------------------------------
static bool GetIniPath(wchar_t* out, size_t cch) {
    wchar_t appdata[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        // fall back to the classic shell API if %APPDATA% is somehow unset
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdata)))
            return false;
    }
    wchar_t dir[MAX_PATH];
    StringCchPrintfW(dir, MAX_PATH, L"%s\\Vigil", appdata);
    CreateDirectoryW(dir, nullptr);               // ok if it already exists
    StringCchPrintfW(out, cch, L"%s\\settings.ini", dir);
    return true;
}

static void LoadSettings() {
    wchar_t ini[MAX_PATH];
    if (!GetIniPath(ini, MAX_PATH)) return;
    g_cfg.activeOnStartup = GetPrivateProfileIntW(L"Vigil", L"ActiveOnStartup", 1, ini) != 0;
    g_cfg.keepDisplayOn   = GetPrivateProfileIntW(L"Vigil", L"KeepDisplayOn",   1, ini) != 0;
    g_cfg.startWithWindows= GetPrivateProfileIntW(L"Vigil", L"StartWithWindows",1, ini) != 0;
    g_cfg.method          = GetPrivateProfileIntW(L"Vigil", L"Method",          METHOD_F15, ini);
    g_cfg.intervalSec     = GetPrivateProfileIntW(L"Vigil", L"IntervalSec",     50, ini);
    g_cfg.lang            = GetPrivateProfileIntW(L"Vigil", L"Lang",            -1, ini);
    g_cfg.sessionMinutes  = 0;   // timed session is runtime-only, never restored on launch

    if (g_cfg.method < METHOD_F15 || g_cfg.method > METHOD_MOUSE) g_cfg.method = METHOD_F15;
    if (g_cfg.intervalSec < 10)   g_cfg.intervalSec = 10;
    if (g_cfg.intervalSec > 3600) g_cfg.intervalSec = 3600;
}

static void SaveSettings() {
    wchar_t ini[MAX_PATH];
    if (!GetIniPath(ini, MAX_PATH)) return;
    wchar_t buf[16];
    #define WR(k,v) do{ StringCchPrintfW(buf,16,L"%d",(int)(v)); WritePrivateProfileStringW(L"Vigil",k,buf,ini);}while(0)
    WR(L"ActiveOnStartup",  g_cfg.activeOnStartup);
    WR(L"KeepDisplayOn",    g_cfg.keepDisplayOn);
    WR(L"StartWithWindows", g_cfg.startWithWindows);
    WR(L"Method",           g_cfg.method);
    WR(L"IntervalSec",      g_cfg.intervalSec);
    WR(L"Lang",             g_cfg.lang);
    #undef WR
}

// ---------------------------------------------------------------------------
//  Start-with-Windows (per-user HKCU\...\Run)  -- no admin needed
// ---------------------------------------------------------------------------
static void ApplyStartup(bool enable) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hk) != ERROR_SUCCESS)
        return;
    if (enable) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        wchar_t quoted[MAX_PATH + 2];
        StringCchPrintfW(quoted, MAX_PATH + 2, L"\"%s\"", exe);
        RegSetValueExW(hk, kRunValue, 0, REG_SZ,
                       (const BYTE*)quoted, (DWORD)((wcslen(quoted) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hk, kRunValue);
    }
    RegCloseKey(hk);
}

// ---------------------------------------------------------------------------
//  Keep-awake core
// ---------------------------------------------------------------------------
static void ApplyExecutionState() {
    EXECUTION_STATE es = ES_CONTINUOUS;
    if (g_enabled) {
        es |= ES_SYSTEM_REQUIRED;
        if (g_cfg.keepDisplayOn) es |= ES_DISPLAY_REQUIRED;
    }
    SetThreadExecutionState(es);
}

// One benign synthetic input event to reset the idle/lock timer.
static void SendPulse() {
    INPUT in[4] = {};
    UINT n = 0;
    switch (g_cfg.method) {
        case METHOD_SCROLL: {
            // tap Scroll Lock twice so the toggle state is left unchanged
            for (int i = 0; i < 2; ++i) {
                in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = VK_SCROLL; n++;
                in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = VK_SCROLL;
                in[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
            }
            break;
        }
        case METHOD_MOUSE: {
            // nudge one pixel right then back: no visible cursor travel
            in[n].type = INPUT_MOUSE; in[n].mi.dx = 1; in[n].mi.dy = 0;
            in[n].mi.dwFlags = MOUSEEVENTF_MOVE; n++;
            in[n].type = INPUT_MOUSE; in[n].mi.dx = (DWORD)-1; in[n].mi.dy = 0;
            in[n].mi.dwFlags = MOUSEEVENTF_MOVE; n++;
            break;
        }
        case METHOD_F15:
        default: {
            in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = VK_F15; n++;
            in[n].type = INPUT_KEYBOARD; in[n].ki.wVk = VK_F15;
            in[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
            break;
        }
    }
    if (n) SendInput(n, in, sizeof(INPUT));
}

static void RestartPulseTimer() {
    KillTimer(g_hwnd, TIMER_PULSE);
    if (g_enabled)
        SetTimer(g_hwnd, TIMER_PULSE, (UINT)g_cfg.intervalSec * 1000, nullptr);
}

// ---------------------------------------------------------------------------
//  Tray icon
// ---------------------------------------------------------------------------
static void BuildTip(wchar_t* out, size_t cch) {
    if (g_enabled && g_sessionEnd) {
        ULONGLONG now = GetTickCount64();
        int mins = (g_sessionEnd > now) ? (int)((g_sessionEnd - now + 59999) / 60000) : 0;
        StringCchPrintfW(out, cch, T(S_ST_SESSION), mins);
    } else {
        StringCchCopyW(out, cch, g_enabled ? T(S_TIP_ON) : T(S_TIP_OFF));
    }
}

static void UpdateTray(bool add) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd   = g_hwnd;
    g_nid.uID    = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_VIGIL_TRAY;
    g_nid.hIcon  = g_enabled ? g_icoActive : g_icoPaused;
    BuildTip(g_nid.szTip, ARRAYSIZE(g_nid.szTip));
    Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &g_nid);
}

static void ShowBalloon(const wchar_t* title, const wchar_t* text) {
    NOTIFYICONDATAW n = {};
    n.cbSize = sizeof(n);
    n.hWnd = g_hwnd; n.uID = 1;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_INFO;
    StringCchCopyW(n.szInfoTitle, ARRAYSIZE(n.szInfoTitle), title);
    StringCchCopyW(n.szInfo, ARRAYSIZE(n.szInfo), text);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

// ---------------------------------------------------------------------------
//  State changes
// ---------------------------------------------------------------------------
static void SetEnabled(bool on, bool fromSession) {
    (void)fromSession;   // reserved for future callers; state reset is unconditional on off
    g_enabled = on;
    if (!on) {
        // turning off (manually or by session expiry) cancels any timed session
        g_sessionEnd = 0;
        g_cfg.sessionMinutes = 0;
        KillTimer(g_hwnd, TIMER_SESSION);
    }
    ApplyExecutionState();
    if (on) SendPulse();          // immediate first pulse
    RestartPulseTimer();
    UpdateTray(false);
}

static void StartTimedSession(int minutes) {
    g_cfg.sessionMinutes = minutes;
    KillTimer(g_hwnd, TIMER_SESSION);
    if (minutes > 0) {
        g_sessionEnd = GetTickCount64() + (ULONGLONG)minutes * 60000ULL;
        SetTimer(g_hwnd, TIMER_SESSION, (UINT)minutes * 60000, nullptr);
        SetEnabled(true, true);
    } else {
        g_sessionEnd = 0;
        UpdateTray(false);
    }
}

// ---------------------------------------------------------------------------
//  Context menu
// ---------------------------------------------------------------------------
static void AddItem(HMENU m, UINT id, const wchar_t* text, bool checked, bool radio=false, bool disabled=false) {
    MENUITEMINFOW mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask  = MIIM_ID | MIIM_STRING | MIIM_STATE | MIIM_FTYPE;
    mi.fType  = radio ? MFT_RADIOCHECK : MFT_STRING;
    mi.wID    = id;
    mi.dwTypeData = (LPWSTR)text;
    mi.fState = (checked ? MFS_CHECKED : 0) | (disabled ? MFS_DISABLED : 0);
    InsertMenuItemW(m, GetMenuItemCount(m), TRUE, &mi);
}

static void ShowMenu() {
    HMENU m = CreatePopupMenu();

    AddItem(m, 0, T(S_HEADER), false, false, true);
    { // live status line
        wchar_t st[96];
        if (g_enabled && g_sessionEnd) {
            int mins = (int)((g_sessionEnd - GetTickCount64() + 59999) / 60000);
            StringCchPrintfW(st, 96, T(S_ST_SESSION), mins > 0 ? mins : 0);
        } else {
            StringCchCopyW(st, 96, g_enabled ? T(S_ST_ON) : T(S_ST_OFF));
        }
        AddItem(m, 0, st, false, false, true);
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    AddItem(m, IDM_TOGGLE,       T(S_ENABLE),       g_enabled);
    AddItem(m, IDM_KEEPDISPLAY,  T(S_KEEPDISPLAY),  g_cfg.keepDisplayOn);

    // Method submenu
    HMENU method = CreatePopupMenu();
    AddItem(method, IDM_METHOD_F15,    T(S_M_F15),    g_cfg.method == METHOD_F15,    true);
    AddItem(method, IDM_METHOD_SCROLL, T(S_M_SCROLL), g_cfg.method == METHOD_SCROLL, true);
    AddItem(method, IDM_METHOD_MOUSE,  T(S_M_MOUSE),  g_cfg.method == METHOD_MOUSE,  true);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)method, T(S_METHOD));

    // Interval submenu
    HMENU interval = CreatePopupMenu();
    wchar_t lbl[32];
    StringCchPrintfW(lbl, 32, T(S_SEC), 30);  AddItem(interval, IDM_INT_30,  lbl, g_cfg.intervalSec == 30,  true);
    StringCchPrintfW(lbl, 32, T(S_SEC), 50);  AddItem(interval, IDM_INT_50,  lbl, g_cfg.intervalSec == 50,  true);
    StringCchPrintfW(lbl, 32, T(S_SEC), 59);  AddItem(interval, IDM_INT_59,  lbl, g_cfg.intervalSec == 59,  true);
    StringCchPrintfW(lbl, 32, T(S_MIN), 2);   AddItem(interval, IDM_INT_120, lbl, g_cfg.intervalSec == 120, true);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)interval, T(S_INTERVAL));

    // Timed session submenu
    HMENU sess = CreatePopupMenu();
    AddItem(sess, IDM_SESS_OFF, T(S_SESS_OFF), g_cfg.sessionMinutes == 0,   true);
    AddItem(sess, IDM_SESS_30M, T(S_HALFHOUR), g_cfg.sessionMinutes == 30,  true);
    AddItem(sess, IDM_SESS_1H,  T(S_1H),       g_cfg.sessionMinutes == 60,  true);
    AddItem(sess, IDM_SESS_2H,  T(S_2H),       g_cfg.sessionMinutes == 120, true);
    AddItem(sess, IDM_SESS_4H,  T(S_4H),       g_cfg.sessionMinutes == 240, true);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)sess, T(S_SESSION));

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AddItem(m, IDM_ACTIVE_ON_START, T(S_ACTIVE_ON_START), g_cfg.activeOnStartup);
    AddItem(m, IDM_START_WITH_WIN,  T(S_START_WITH_WIN),  g_cfg.startWithWindows);

    // Language submenu
    HMENU lang = CreatePopupMenu();
    AddItem(lang, IDM_LANG_EN, T(S_EN), LangIdx() == 0, true);
    AddItem(lang, IDM_LANG_HE, T(S_HE), LangIdx() == 1, true);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)lang, T(S_LANG));

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AddItem(m, IDM_ABOUT, T(S_ABOUT), false);
    AddItem(m, IDM_EXIT,  T(S_EXIT),  false);

    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);                 // so the menu dismisses correctly
    UINT flags = TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD;
    if (GetSystemMetrics(SM_MENUDROPALIGNMENT)) flags |= TPM_RIGHTALIGN; else flags |= TPM_LEFTALIGN;
    int cmd = TrackPopupMenu(m, flags, pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd) PostMessageW(g_hwnd, WM_COMMAND, (WPARAM)cmd, 0);
}

// ---------------------------------------------------------------------------
//  Command handling
// ---------------------------------------------------------------------------
static void OnCommand(UINT id) {
    switch (id) {
        case IDM_TOGGLE:        SetEnabled(!g_enabled, false); break;
        case IDM_KEEPDISPLAY:
            g_cfg.keepDisplayOn = !g_cfg.keepDisplayOn; SaveSettings(); ApplyExecutionState(); break;

        case IDM_METHOD_F15:    g_cfg.method = METHOD_F15;    SaveSettings(); if (g_enabled) SendPulse(); break;
        case IDM_METHOD_SCROLL: g_cfg.method = METHOD_SCROLL; SaveSettings(); if (g_enabled) SendPulse(); break;
        case IDM_METHOD_MOUSE:  g_cfg.method = METHOD_MOUSE;  SaveSettings(); if (g_enabled) SendPulse(); break;

        case IDM_INT_30:  g_cfg.intervalSec = 30;  SaveSettings(); RestartPulseTimer(); break;
        case IDM_INT_50:  g_cfg.intervalSec = 50;  SaveSettings(); RestartPulseTimer(); break;
        case IDM_INT_59:  g_cfg.intervalSec = 59;  SaveSettings(); RestartPulseTimer(); break;
        case IDM_INT_120: g_cfg.intervalSec = 120; SaveSettings(); RestartPulseTimer(); break;

        case IDM_SESS_OFF: StartTimedSession(0);   break;
        case IDM_SESS_30M: StartTimedSession(30);  break;
        case IDM_SESS_1H:  StartTimedSession(60);  break;
        case IDM_SESS_2H:  StartTimedSession(120); break;
        case IDM_SESS_4H:  StartTimedSession(240); break;

        case IDM_ACTIVE_ON_START:
            g_cfg.activeOnStartup = !g_cfg.activeOnStartup; SaveSettings(); break;
        case IDM_START_WITH_WIN:
            g_cfg.startWithWindows = !g_cfg.startWithWindows; SaveSettings();
            ApplyStartup(g_cfg.startWithWindows); break;

        case IDM_LANG_EN: g_cfg.lang = 0; SaveSettings(); UpdateTray(false); break;
        case IDM_LANG_HE: g_cfg.lang = 1; SaveSettings(); UpdateTray(false); break;

        case IDM_ABOUT:
            MessageBoxW(nullptr, T(S_ABOUT_TEXT), T(S_BALLOON_TITLE),
                        MB_OK | MB_ICONINFORMATION | (LangIdx() == 1 ? MB_RTLREADING | MB_RIGHT : 0));
            break;
        case IDM_EXIT:
            DestroyWindow(g_hwnd);
            break;
    }
}

// ---------------------------------------------------------------------------
//  Window procedure
// ---------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_VIGIL_TRAY:
            switch (LOWORD(lp)) {
                case WM_LBUTTONUP:  SetEnabled(!g_enabled, false); break;
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU: ShowMenu(); break;
            }
            return 0;

        case WM_COMMAND:
            OnCommand(LOWORD(wp));
            return 0;

        case WM_TIMER:
            if (wp == TIMER_PULSE) {
                if (g_enabled) { ApplyExecutionState(); SendPulse(); }
            } else if (wp == TIMER_SESSION) {
                SetEnabled(false, true);   // clears session state + releases the lock
                ShowBalloon(T(S_BALLOON_TITLE), T(S_BALLOON_SESSION_END));
            }
            return 0;

        case WM_POWERBROADCAST:
            // After resume from sleep, re-assert our state.
            if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
                ApplyExecutionState();
                if (g_enabled) SendPulse();
            }
            return TRUE;

        case WM_ENDSESSION:
            // being logged off / shut down: tidy up
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            SetThreadExecutionState(ES_CONTINUOUS);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, TIMER_PULSE);
            KillTimer(hwnd, TIMER_SESSION);
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            SetThreadExecutionState(ES_CONTINUOUS);     // release the lock
            PostQuitMessage(0);
            return 0;

        default:
            if (msg == g_taskbarCreated) {              // Explorer restarted -> re-add icon
                UpdateTray(true);
                return 0;
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
//  Entry point
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    // single instance
    HANDLE mtx = CreateMutexW(nullptr, TRUE, kMutexName);
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mtx);
        return 0;   // already running
    }

    g_hInst = hInst;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LoadSettings();
    g_enabled = g_cfg.activeOnStartup;

    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    // load icons at the small tray size
    int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
    g_icoActive = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_ACTIVE), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    g_icoPaused = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_PAUSED), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    if (!g_icoActive) g_icoActive = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_ACTIVE));
    if (!g_icoPaused) g_icoPaused = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_PAUSED));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    // A normal top-level window that we never show: unlike a message-only
    // window it can take the foreground (so the popup menu dismisses correctly)
    // and it receives the "TaskbarCreated" broadcast when Explorer restarts.
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWndClass, L"Vigil", WS_POPUP,
                             0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    // keep the per-user startup registry entry in sync with the setting
    ApplyStartup(g_cfg.startWithWindows);

    // add tray + apply initial state
    UpdateTray(true);
    ApplyExecutionState();
    if (g_enabled) { SendPulse(); }

    RestartPulseTimer();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (mtx) { ReleaseMutex(mtx); CloseHandle(mtx); }
    CoUninitialize();
    return 0;
}
