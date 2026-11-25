#ifndef VIGIL_RESOURCE_H
#define VIGIL_RESOURCE_H

// Icons
#define IDI_ACTIVE              101
#define IDI_PAUSED             102

// Tray callback + timers
#define WM_VIGIL_TRAY          (WM_APP + 1)
#define TIMER_PULSE            1
#define TIMER_SESSION          2

// Menu command IDs
#define IDM_TOGGLE             2001
#define IDM_KEEPDISPLAY        2002
#define IDM_ACTIVE_ON_START    2003
#define IDM_START_WITH_WIN     2004
#define IDM_ABOUT              2005
#define IDM_EXIT               2006

// Method (radio)
#define IDM_METHOD_F15         2100
#define IDM_METHOD_SCROLL      2101
#define IDM_METHOD_MOUSE       2102

// Interval (radio)
#define IDM_INT_30             2200
#define IDM_INT_50             2201
#define IDM_INT_59             2202
#define IDM_INT_120            2203

// Timed session (radio)
#define IDM_SESS_OFF           2300
#define IDM_SESS_30M           2301
#define IDM_SESS_1H            2302
#define IDM_SESS_2H            2303
#define IDM_SESS_4H            2304

// Language (radio)
#define IDM_LANG_EN            2400
#define IDM_LANG_HE            2401

#endif // VIGIL_RESOURCE_H
