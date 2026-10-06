// PikPlayer - reproductor de vídeo minimalista para Windows (libmpv + Win32 + GDI+)
// Estilo "Películas y TV": ventana oscura, controles flotantes que se ocultan solos.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <windows.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <oleidl.h>
#include <shlobj.h>
#ifndef WM_COPYGLOBALDATA
#define WM_COPYGLOBALDATA 0x0049
#endif
#ifndef MSGFLT_ALLOW
#define MSGFLT_ALLOW 1
#endif
#include <algorithm>
using std::min; using std::max;
#include <objidl.h>
#include <gdiplus.h>
#include <mpv/client.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <utility>
#include <string>
#include <thread>
#include <vector>
#include <numeric>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ksmedia.h>
#include "usm.h"
#include "install_common.h"
#include "dllname.h"   // generado por Compilar.bat (MPV_DLL_NAME)

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "delayimp.lib")

using namespace Gdiplus;
namespace fsys = std::filesystem;

enum { WM_MPV = WM_APP + 1, WM_USMDONE = WM_APP + 2, WM_OPENARG = WM_APP + 3, WM_RESETOVL = WM_APP + 4 };
enum { T_UI = 1, T_CLICK = 2, T_THUMB = 3, T_VIS = 4 };
enum Hit { H_NONE, H_SEEK, H_PLAY, H_BACK, H_FWD, H_MUTE, H_VOL, H_TRACKS, H_INFO, H_FS };
enum OvlRole { OV_BAR = 0, OV_PREV, OV_NEXT, OV_INFO, OV_CLOSEFS, OV_AUDIOVIS };

static HINSTANCE g_inst;
static HWND g_main, g_video, g_ovl, g_prev, g_next, g_info, g_closefs, g_audioVis;   // overlays: barra, flechas, info y X de pantalla completa
static mpv_handle* g_mpv;
static int g_dpi = 96;
static bool g_fs, g_loaded, g_pause = true, g_mute, g_eof, g_fitPending, g_audioOnly;
static double g_visPhase;
static constexpr int kFftBars = 2000;
static std::array<float, kFftBars> g_fftLevels{};
static std::mutex g_fftMutex;
static std::atomic<bool> g_fftRun{ false };
static std::thread g_fftThread;
static double g_pos, g_dur, g_vol = 100;
static double g_usmPendingDur = 0;
static ULONGLONG g_lastMove;
static WINDOWPLACEMENT g_wp;
static LONG g_style;
static std::wstring g_status, g_curName, g_tmpRoot, g_curTmp;
static unsigned g_gen;
static Hit g_drag = H_NONE, g_hover = H_NONE;
static bool g_seekWasPaused = true;
static bool g_menuOpen, g_showInfo, g_noFit;
static std::wstring g_curExt;                 // extensión del archivo abierto (minúsculas, con punto)
static int g_hoverNav;                        // -1 flecha izquierda, 1 derecha, 0 ninguna
static int g_sizeType = SIZE_RESTORED;
static std::vector<std::wstring> g_list;     // vídeos de la carpeta actual (orden natural)
static int g_idx = -1;                       // posición del vídeo actual en g_list
static ULONG_PTR g_gdipToken;
static HICON g_blankIcon;

static int S(int v) { return MulDiv(v, g_dpi, 96); }
static Gdiplus::REAL SR(float v) { return v * (Gdiplus::REAL)g_dpi / 96.0f; }

// ---------- utilidades ----------
static std::string U8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr); return s;
}
static std::wstring W(const char* s) {
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n); w.resize(n - 1); return w;
}
static std::wstring Lower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
static std::wstring FmtTime(double t) {
    if (t < 0 || t != t) t = 0;
    int s = (int)t, h = s / 3600, m = (s / 60) % 60; s %= 60;
    wchar_t b[32];
    if (h) swprintf_s(b, L"%d:%02d:%02d", h, m, s); else swprintf_s(b, L"%d:%02d", m, s);
    return b;
}
static std::string Prop(const char* name) {
    char* v = mpv_get_property_string(g_mpv, name); if (!v) return {};
    std::string s = v; mpv_free(v); return s;
}
static void Cmd(const char* c) { if (g_mpv) mpv_command_string(g_mpv, c); }
static void SetProp(const char* k, const std::string& v) { if (g_mpv) mpv_set_property_string(g_mpv, k, v.c_str()); }
static bool SetPropDouble(const char* k, double v) {
    return g_mpv && mpv_set_property(g_mpv, k, MPV_FORMAT_DOUBLE, &v) >= 0;
}
static bool SetSpeed(double v) {
    if (!g_mpv || v < 0.01 || v > 100.0) return false;
    char value[64]; sprintf_s(value, "%.6f", v);
    // Use mpv's native input command first. This is the same path used by
    // mpv's own key bindings and avoids any ambiguity in the menu layer.
    const char* cmd[] = { "set", "speed", value, nullptr };
    int r = mpv_command(g_mpv, cmd);
    double actual = 0.0;
    if (r >= 0 && mpv_get_property(g_mpv, "speed", MPV_FORMAT_DOUBLE, &actual) >= 0 && fabs(actual - v) < 0.0005)
        return true;
    // Fallback to the typed libmpv property API.
    if (SetPropDouble("speed", v)) {
        actual = 0.0;
        if (mpv_get_property(g_mpv, "speed", MPV_FORMAT_DOUBLE, &actual) >= 0 && fabs(actual - v) < 0.0005)
            return true;
    }
    // Last fallback: string property API.
    if (mpv_set_property_string(g_mpv, "speed", value) >= 0) {
        actual = 0.0;
        if (mpv_get_property(g_mpv, "speed", MPV_FORMAT_DOUBLE, &actual) >= 0 && fabs(actual - v) < 0.0005)
            return true;
    }
    return false;
}

static std::wstring Tr(const wchar_t* es);
static void Redraw();
static void RedrawAll();
static void UpdateOverlayVisibility();
static void ToggleFS();
static void OpenDialog();
static void Nav(int dir);

// ---------- carga de archivos ----------
static void LoadMpv(const std::string& path) {
    const char* a[] = { "loadfile", path.c_str(), "replace", nullptr };
    mpv_command(g_mpv, a);
}

static void ResetLoadOptions() {
    SetProp("demuxer", ""); SetProp("demuxer-lavf-format", ""); SetProp("demuxer-lavf-o", ""); SetProp("audio-files", "");
    SetProp("force-seekable", "no");
    SetProp("hr-seek", "yes"); // mantener seek exacto también tras cambiar de archivo
    SetProp("demuxer-max-bytes", "150MiB"); SetProp("demuxer-readahead-secs", "1");
}

static bool IsMediaExt(const std::wstring& ext) {
    for (auto e : inst::kExts) if (ext == e) return true;
    return false;
}
// Reúne los vídeos/audios de la carpeta del archivo (orden natural: 2 antes que 10) y localiza el actual.
static void BuildList(const std::wstring& p) {
    g_list.clear(); g_idx = -1;
    std::error_code ec;
    fsys::path full = fsys::absolute(fsys::path(p), ec).lexically_normal();
    fsys::path dir = full.parent_path();
    if (dir.empty()) return;
    fsys::directory_iterator it(dir, fsys::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && IsMediaExt(Lower(it->path().extension().wstring()))) g_list.push_back(it->path().wstring());
    }
    auto less = [](const std::wstring& a, const std::wstring& b) { return StrCmpLogicalW(a.c_str(), b.c_str()) < 0; };
    auto find = [&]() {
        for (size_t i = 0; i < g_list.size(); i++) if (_wcsicmp(g_list[i].c_str(), full.c_str()) == 0) return (int)i;
        for (size_t i = 0; i < g_list.size(); i++) { std::error_code e3; if (fsys::equivalent(g_list[i], full, e3)) return (int)i; }
        return -1;
    };
    std::sort(g_list.begin(), g_list.end(), less);
    g_idx = find();
    if (g_idx < 0) { g_list.push_back(full.wstring()); std::sort(g_list.begin(), g_list.end(), less); g_idx = find(); }
}

static void LoadPath(const std::wstring& p, bool nav = false) {
    if (!g_mpv) return;
    if (!nav) BuildList(p);
    g_noFit = nav;   // al cambiar con las flechas la ventana no se redimensiona (las flechas no se mueven de sitio)
    unsigned gen = ++g_gen;
    std::error_code ec;
    if (!g_curTmp.empty()) { fsys::remove_all(g_curTmp, ec); g_curTmp.clear(); }
    g_curName = fsys::path(p).stem().wstring();
    g_curExt = Lower(fsys::path(p).extension().wstring());
    g_status.clear(); g_eof = false; g_audioOnly = false; g_visPhase = 0.0;
    ResetLoadOptions();
    if (Lower(fsys::path(p).extension().wstring()) == L".usm") {
        g_status = Tr(L"Preparando video…"); g_loaded = false;
        InvalidateRect(g_video, nullptr, TRUE); SetWindowTextW(g_main, g_curName.c_str());
        std::wstring dir = g_tmpRoot + L"\\" + std::to_wstring(gen);
        std::thread([p, dir, gen] {
            auto* r = new std::pair<unsigned, UsmResult>(gen, UsmDemux(p, dir));
            if (!PostMessageW(g_main, WM_USMDONE, 0, (LPARAM)r)) delete r;
        }).detach();
        return;
    }
    LoadMpv(U8(p));
}

static void OnUsmDone(std::pair<unsigned, UsmResult>* r) {
    std::unique_ptr<std::pair<unsigned, UsmResult>> hold(r);
    if (r->first != g_gen) { std::error_code ec; fsys::remove_all(g_tmpRoot + L"\\" + std::to_wstring(r->first), ec); return; }
    UsmResult& u = r->second;
    g_curTmp = g_tmpRoot + L"\\" + std::to_wstring(r->first);
    if (!u.ok) { g_status = u.error; InvalidateRect(g_video, nullptr, TRUE); return; }
    g_status.clear();
    // Seek preciso en USM: exact + force-seekable (evita saltar al keyframe anterior)
    SetProp("hr-seek", "yes");
    SetProp("force-seekable", "yes");
    if (!u.format.empty()) {
        SetProp("demuxer", "lavf"); SetProp("demuxer-lavf-format", u.format); SetProp("demuxer-lavf-o", "framerate=" + u.fps);
        SetProp("demuxer-max-bytes", "768MiB"); SetProp("demuxer-readahead-secs", "7200");
    } else {
        // VP9→WebM con índice: también exact para que la barra coincida con el clic
        SetProp("demuxer-max-bytes", "150MiB"); SetProp("demuxer-readahead-secs", "2");
    }
    std::string audio;
    for (auto& a : u.audioPaths) { if (!audio.empty()) audio += ";"; audio += U8(a); }
    SetProp("audio-files", audio);
    LoadMpv(U8(u.videoPath));
    if (u.duration > 0) g_usmPendingDur = u.duration;
    if (!u.note.empty()) { g_status = u.note; InvalidateRect(g_video, nullptr, TRUE); }
}

static void OpenDialog() {
    wchar_t file[MAX_PATH * 4] = L"";
    OPENFILENAMEW o{ sizeof o };
    o.hwndOwner = g_main; o.lpstrFile = file; o.nMaxFile = (DWORD)std::size(file);
    o.lpstrFilter = L"Vídeo y audio\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.webm;*.flv;*.f4v;*.m4v;*.mpg;*.mpeg;*.mpe;*.ts;*.m2ts;*.mts;*.mxf;*.3gp;*.3g2;*.ogv;*.ogm;*.vob;*.asf;*.divx;*.rm;*.rmvb;*.qt;*.nut;*.usm;*.mp3;*.flac;*.m4a;*.m4b;*.aac;*.ogg;*.oga;*.opus;*.wav;*.aif;*.aiff;*.ape;*.ac3;*.dts;*.amr;*.mka;*.mp2;*.mpa;*.wma\0Todos los archivos\0*.*\0";
    o.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    g_menuOpen = true;
    if (GetOpenFileNameW(&o)) LoadPath(file);
    g_menuOpen = false;
}

// ---------- acciones ----------
static void Nav(int dir) {
    int ni = g_idx + dir;
    if (g_idx < 0 || ni < 0 || ni >= (int)g_list.size()) return;
    g_idx = ni; g_lastMove = GetTickCount64();
    LoadPath(g_list[ni], true);
    UpdateOverlayVisibility(); RedrawAll();
}
static void TogglePause() {
    if (!g_loaded) return;
    if (g_eof) { Cmd("seek 0 absolute"); SetProp("pause", "no"); return; }
    Cmd("cycle pause");
}
static void Seek(double d) { char b[64]; sprintf_s(b, "seek %.1f", d); Cmd(b); }
static void AddVol(double d) { char b[64]; sprintf_s(b, "add volume %.0f", d); Cmd(b); }

// Trae PikPlayer al primer plano aunque haya otra ventana delante (un clic dentro de la ventana basta).
static void BringFront() {
    if (!g_main) return;
    if (IsIconic(g_main)) ShowWindow(g_main, SW_RESTORE);
    HWND fg = GetForegroundWindow();
    if (fg == g_main) return;
    DWORD ft = fg ? GetWindowThreadProcessId(fg, nullptr) : 0, mt = GetCurrentThreadId();
    bool att = ft && ft != mt && AttachThreadInput(mt, ft, TRUE);
    BringWindowToTop(g_main); SetForegroundWindow(g_main);
    if (att) AttachThreadInput(mt, ft, FALSE);
}
static void CloseInfo() { if (g_showInfo) { g_showInfo = false; UpdateOverlayVisibility(); RedrawAll(); } }

static void DoAction(const std::string& a, const std::string& arg) {
    if (a == "pp-toggle") TogglePause();
    else if (a == "pp-seek") Seek(atof(arg.c_str()));
    else if (a == "pp-vol") AddVol(atof(arg.c_str()));
    else if (a == "pp-mute") Cmd("cycle mute");
    else if (a == "pp-fs") ToggleFS();
    else if (a == "pp-esc") { if (g_showInfo) CloseInfo(); else if (g_fs) ToggleFS(); }
    else if (a == "pp-open") OpenDialog();
    else if (a == "pp-speed") { SetSpeed(atof(arg.c_str())); RedrawAll(); }
    else if (a == "pp-speeddown") { double v = atof(Prop("speed").c_str()); SetSpeed(std::max(0.01, v * 0.9)); RedrawAll(); }
    else if (a == "pp-speedup") { double v = atof(Prop("speed").c_str()); SetSpeed(std::min(100.0, v * 1.1)); RedrawAll(); }
    else if (a == "pp-step") Cmd(arg == "1" ? "frame-step" : "frame-back-step");
    else if (a == "pp-audio") Cmd("cycle aid");
    else if (a == "pp-sub") Cmd("cycle sid");
    else if (a == "pp-shot") Cmd("screenshot");
    else if (a == "pp-click") {
        if (GetForegroundWindow() != g_main) { BringFront(); return; }   // el primer clic solo trae la ventana al frente
        if (g_showInfo) { CloseInfo(); return; }                          // clic fuera del panel: se cierra
        SetTimer(g_main, T_CLICK, GetDoubleClickTime(), nullptr);
    }
    else if (a == "pp-dbl") { KillTimer(g_main, T_CLICK); ToggleFS(); }
    else if (a == "pp-move") { g_lastMove = GetTickCount64(); }
    else if (a == "pp-info") { g_showInfo = !g_showInfo; UpdateOverlayVisibility(); RedrawAll(); }
    else if (a == "pp-prev") Nav(-1);
    else if (a == "pp-next") Nav(1);
}

static void Bind(const char* key, const char* msg) {
    std::string c = std::string("script-message ") + msg;
    const char* a[] = { "keybind", key, c.c_str(), nullptr };
    mpv_command(g_mpv, a);
}


// ---------- libmpv embebida: se guarda dentro del .exe y se extrae sola la primera vez ----------
static bool PrepareMpvDll() {
    HRSRC r = FindResourceW(g_inst, L"MPVDLL", MAKEINTRESOURCEW(10) /*RT_RCDATA*/);
    if (!r) return LoadLibraryW(MPV_DLL_NAME) != nullptr;   // sin recurso: buscar junto al exe
    HGLOBAL hg = LoadResource(g_inst, r); DWORD sz = SizeofResource(g_inst, r);
    const void* data = hg ? LockResource(hg) : nullptr;
    if (!data || !sz) return false;
    wchar_t la[MAX_PATH]; DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH);
    if (!n || n >= MAX_PATH) GetTempPathW(MAX_PATH, la);
    std::error_code ec;
    fsys::path dir = fsys::path(la) / L"PikPlayer" / L"bin";
    fsys::create_directories(dir, ec);
    fsys::path dll = dir / MPV_DLL_NAME;
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    WIN32_FILE_ATTRIBUTE_DATA a{}, b{};
    bool fresh = GetFileAttributesExW(dll.c_str(), GetFileExInfoStandard, &a) &&
                 GetFileAttributesExW(exe, GetFileExInfoStandard, &b) &&
                 a.nFileSizeLow == sz && a.nFileSizeHigh == 0 &&
                 CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) >= 0;
    if (!fresh) {
        fsys::path tmp = dll; tmp += L".tmp";
        HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD w = 0; bool ok = WriteFile(f, data, sz, &w, nullptr) && w == sz; CloseHandle(f);
            if (ok) ok = MoveFileExW(tmp.c_str(), dll.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
            if (!ok) DeleteFileW(tmp.c_str());
        }
    }
    return LoadLibraryW(dll.c_str()) != nullptr;
}

// ---------- HDR / color ----------
// mpv/libplacebo handles HDR/SDR negotiation automatically. No manual
// Windows HDR detection or forced PQ/BT.2020 output is used.

static bool InitMpv() {
    g_mpv = mpv_create();
    if (!g_mpv) return false;
    auto o = [&](const char* k, const char* v) { return mpv_set_option_string(g_mpv, k, v); };
    char wid[32]; sprintf_s(wid, "%lld", (long long)(intptr_t)g_video);
    o("wid", wid);
    o("config", "no"); o("load-scripts", "no"); o("ytdl", "no");
    o("vo", "gpu-next"); o("gpu-api", "d3d11"); o("gpu-context", "d3d11");
    o("hwdec", "auto-safe");
    // Dejar que gpu-next/D3D11 negocien automaticamente el espacio de color
    // con la pantalla. Esto permite HDR nativo cuando Windows esta en HDR y
    // tone-mapping correcto cuando la pantalla esta en SDR.
    o("target-colorspace-hint", "auto");
    o("target-colorspace-hint-mode", "target");
    o("tone-mapping", "auto");
    o("icc-profile-auto", "no");
    o("osc", "no"); o("osd-level", "0"); o("input-default-bindings", "no"); o("input-vo-keyboard", "yes");
    o("input-cursor", "yes"); o("cursor-autohide", "2500"); o("drag-and-drop", "no");
    o("idle", "yes"); o("keep-open", "yes"); o("force-window", "no");
    o("volume-max", "130");
    o("sub-auto", "fuzzy");
    o("sub-visibility", "yes");
    o("track-auto-selection", "yes");
    o("audio-display", "embedded-first");
    // Seek preciso en la barra de tiempo (todos los formatos)
    o("hr-seek", "yes");
    o("hr-seek-framedrop", "yes");
    o("screenshot-directory", "~~desktop/");
    o("screenshot-format", "png");
    o("video-sync", "display-desync");
    if (mpv_initialize(g_mpv) < 0) return false;

    mpv_observe_property(g_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(g_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(g_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(g_mpv, 0, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(g_mpv, 0, "aid", MPV_FORMAT_STRING);
    mpv_observe_property(g_mpv, 0, "sid", MPV_FORMAT_STRING);
    mpv_observe_property(g_mpv, 0, "track-list", MPV_FORMAT_NODE);
    mpv_set_wakeup_callback(g_mpv, [](void*) { PostMessageW(g_main, WM_MPV, 0, 0); }, nullptr);

    Bind("SPACE", "pp-toggle"); Bind("MBTN_LEFT", "pp-click"); Bind("MBTN_LEFT_DBL", "pp-dbl");
    Bind("MOUSE_MOVE", "pp-move"); Bind("f", "pp-fs"); Bind("ENTER", "pp-fs"); Bind("ESC", "pp-esc");
    Bind("m", "pp-mute"); Bind("Ctrl+o", "pp-open"); Bind("a", "pp-audio"); Bind("t", "pp-sub"); Bind("s", "pp-shot");
    Bind("WHEEL_UP", "pp-vol 5"); Bind("WHEEL_DOWN", "pp-vol -5"); Bind("UP", "pp-vol 5"); Bind("DOWN", "pp-vol -5");
    Bind("LEFT", "pp-seek -5"); Bind("RIGHT", "pp-seek 5"); Bind("j", "pp-seek -10"); Bind("l", "pp-seek 10");
    Bind("Shift+LEFT", "pp-seek -30"); Bind("Shift+RIGHT", "pp-seek 30");
    Bind("[", "pp-speeddown"); Bind("]", "pp-speedup");
    Bind(".", "pp-step 1"); Bind(",", "pp-step 0");
    Bind("i", "pp-info"); Bind("PGUP", "pp-prev"); Bind("PGDWN", "pp-next"); Bind("p", "pp-prev"); Bind("n", "pp-next");
    Bind("MBTN_BACK", "pp-prev"); Bind("MBTN_FORWARD", "pp-next");
    return true;
}

static void FitWindowToVideo() {
    int64_t dw = 0, dh = 0;
    mpv_get_property(g_mpv, "dwidth", MPV_FORMAT_INT64, &dw);
    mpv_get_property(g_mpv, "dheight", MPV_FORMAT_INT64, &dh);
    if (dw <= 0 || dh <= 0 || g_fs || IsZoomed(g_main) || IsIconic(g_main)) return;
    MONITORINFO mi{ sizeof mi }; GetMonitorInfoW(MonitorFromWindow(g_main, MONITOR_DEFAULTTONEAREST), &mi);
    double maxW = (mi.rcWork.right - mi.rcWork.left) * 0.85, maxH = (mi.rcWork.bottom - mi.rcWork.top) * 0.85;
    double k = std::min({ 1.0, maxW / (double)dw, maxH / (double)dh });
    int cw = std::max(560, (int)(dw * k)), ch = std::max(270, (int)(dh * k));
    RECT r{ 0, 0, cw, ch };
    AdjustWindowRectExForDpi(&r, GetWindowLongW(g_main, GWL_STYLE), FALSE, 0, g_dpi);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
    int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
    SetWindowPos(g_main, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}


static void ComputeAudioFFT(const float* samples, size_t count, double sampleRate) {
    constexpr int N = 4096;
    std::array<double, N> re{}, im{};
    const int n = (int)std::min<size_t>(count, N);
    for (int i = 0; i < n; ++i) {
        const double window = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / (N - 1));
        re[i] = samples[i] * window;
    }
    // In-place radix-2 FFT.
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= N; len <<= 1) {
        const double ang = -2.0 * 3.14159265358979323846 / len;
        const double wlenR = std::cos(ang), wlenI = std::sin(ang);
        for (int i = 0; i < N; i += len) {
            double wr = 1.0, wi = 0.0;
            const int half = len >> 1;
            for (int j = 0; j < half; ++j) {
                const int u = i + j, v = i + j + half;
                const double vr = re[v] * wr - im[v] * wi;
                const double vi = re[v] * wi + im[v] * wr;
                const double ur = re[u], ui = im[u];
                re[u] = ur + vr; im[u] = ui + vi;
                re[v] = ur - vr; im[v] = ui - vi;
                const double nwr = wr * wlenR - wi * wlenI;
                wi = wr * wlenI + wi * wlenR; wr = nwr;
            }
        }
    }

    std::array<float, kFftBars> levels{};
    // Logarithmic bands from roughly 35 Hz to 16 kHz.
    constexpr double minHz = 35.0, maxHz = 16000.0;
    for (int b = 0; b < kFftBars; ++b) {
        const double f0 = minHz * std::pow(maxHz / minHz, b / (double)kFftBars);
        const double f1 = minHz * std::pow(maxHz / minHz, (b + 1) / (double)kFftBars);
        int k0 = std::max(1, (int)std::floor(f0 * N / sampleRate));
        int k1 = std::min(N / 2, (int)std::ceil(f1 * N / sampleRate));
        double energy = 0.0;
        int bins = 0;
        for (int k = k0; k <= k1; ++k) {
            const double mag = std::hypot(re[k], im[k]) / (N * 0.5);
            energy += mag * mag;
            ++bins;
        }
        const double rms = bins ? std::sqrt(energy / bins) : 0.0;
        // Log compression makes quiet frequencies visible while preserving peaks.
        const double db = 20.0 * std::log10(std::max(rms, 1e-7));
        // Wider dynamic range: avoid hard saturation on normally mastered audio.
        // The exponent below keeps quieter changes visible while leaving headroom for peaks.
        const double normalized = std::clamp((db + 78.0) / 78.0, 0.0, 1.0);
        // Keep substantial headroom: everyday mastered audio should sit well below the ceiling,
        // while louder transients can still use most of the available height.
        levels[b] = (float)std::pow(normalized, 1.65);
    }
    std::lock_guard<std::mutex> lock(g_fftMutex);
    for (int i = 0; i < kFftBars; ++i) {
        // Attack quickly, decay smoothly.
        const float old = g_fftLevels[i];
        const float target = levels[i];
        g_fftLevels[i] = target > old ? old * 0.18f + target * 0.82f : old * 0.68f + target * 0.32f;
    }
}

static void AudioCaptureThread() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return;
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* format = nullptr;
    do {
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator), (void**)&enumerator))) break;
        if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) break;
        if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client))) break;
        if (FAILED(client->GetMixFormat(&format))) break;
        const REFERENCE_TIME buffer = 10000000; // 1 second; shared mode chooses the actual period.
        if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                       buffer, 0, format, nullptr))) break;
        if (FAILED(client->GetService(__uuidof(IAudioCaptureClient), (void**)&capture))) break;
        if (FAILED(client->Start())) break;

        const WAVEFORMATEXTENSIBLE* ext = (const WAVEFORMATEXTENSIBLE*)format;
        const bool isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                             (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        const int channels = std::max<int>(1, format->nChannels);
        const int bits = format->wBitsPerSample;
        const double rate = (double)format->nSamplesPerSec;
        std::array<float, 4096> window{};
        size_t used = 0;

        while (g_fftRun.load(std::memory_order_relaxed)) {
            UINT32 packets = 0;
            if (FAILED(capture->GetNextPacketSize(&packets))) break;
            if (!packets) { Sleep(5); continue; }
            BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) { Sleep(2); continue; }
            if (frames) {
                for (UINT32 f = 0; f < frames && g_fftRun.load(std::memory_order_relaxed); ++f) {
                    float mono = 0.0f;
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        mono = 0.0f;
                    } else if (isFloat && bits == 32) {
                        const float* p = (const float*)data + (size_t)f * channels;
                        for (int c = 0; c < channels; ++c) mono += p[c];
                        mono /= channels;
                    } else if (bits == 16) {
                        const int16_t* p = (const int16_t*)data + (size_t)f * channels;
                        for (int c = 0; c < channels; ++c) mono += p[c] / 32768.0f;
                        mono /= channels;
                    } else if (bits == 32) {
                        const int32_t* p = (const int32_t*)data + (size_t)f * channels;
                        for (int c = 0; c < channels; ++c) mono += p[c] / 2147483648.0f;
                        mono /= channels;
                    }
                    window[used++] = mono;
                    if (used == window.size()) {
                        ComputeAudioFFT(window.data(), window.size(), rate);
                        // 50% overlap keeps the display responsive without excessive CPU use.
                        std::move(window.begin() + 2048, window.end(), window.begin());
                        used = 2048;
                    }
                }
            }
            capture->ReleaseBuffer(frames);
        }
        client->Stop();
    } while (false);
    if (format) CoTaskMemFree(format);
    if (capture) capture->Release();
    if (client) client->Release();
    if (device) device->Release();
    if (enumerator) enumerator->Release();
    CoUninitialize();
}

static void StartAudioCapture() {
    if (g_fftRun.exchange(true)) return;
    {
        std::lock_guard<std::mutex> lock(g_fftMutex);
        g_fftLevels.fill(0.0f);
    }
    g_fftThread = std::thread(AudioCaptureThread);
}

static void StopAudioCapture() {
    if (!g_fftRun.exchange(false)) return;
    if (g_fftThread.joinable()) g_fftThread.join();
    std::lock_guard<std::mutex> lock(g_fftMutex);
    g_fftLevels.fill(0.0f);
}

static void EnsureMpvDropTarget(); // definida más abajo (junto a VideoProc)
static void EnableDropOn(HWND w);
static void DoDrop(HDROP d);

static void HandleMpvEvents() {
    while (g_mpv) {
        mpv_event* e = mpv_wait_event(g_mpv, 0);
        if (e->event_id == MPV_EVENT_NONE) break;
        switch (e->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE: {
            auto* p = (mpv_event_property*)e->data; std::string n = p->name;
            if (n == "time-pos") g_pos = p->format == MPV_FORMAT_DOUBLE ? *(double*)p->data : 0;
            else if (n == "duration") g_dur = p->format == MPV_FORMAT_DOUBLE ? *(double*)p->data : 0;
            else if (n == "pause") g_pause = p->format == MPV_FORMAT_FLAG ? *(int*)p->data != 0 : true;
            else if (n == "mute") g_mute = p->format == MPV_FORMAT_FLAG && *(int*)p->data != 0;
            else if (n == "volume") { if (p->format == MPV_FORMAT_DOUBLE) g_vol = *(double*)p->data; }
            else if (n == "eof-reached") g_eof = p->format == MPV_FORMAT_FLAG && *(int*)p->data != 0;
            break; }
        case MPV_EVENT_START_FILE: g_eof = false; g_pos = 0; g_dur = g_usmPendingDur; g_usmPendingDur = 0; break;
        case MPV_EVENT_FILE_LOADED: {
            EnsureMpvDropTarget();
            // Always let mpv select the default audio/subtitle tracks after the
            // new file has been fully demuxed. This is important for MKV/MP4
            // files containing several streams and for embedded subtitles.
            SetProp("track-auto-selection", "yes");
            SetProp("sub-visibility", "yes");
            SetProp("secondary-sid", "no");
            std::string vf = Prop("video-format");
            g_audioOnly = vf.empty() || vf == "no";
            if (g_audioOnly) StartAudioCapture(); else StopAudioCapture();
            g_loaded = true; g_fitPending = !g_noFit && !g_audioOnly; g_noFit = false; g_lastMove = GetTickCount64();
            g_visPhase = 0.0;
            if (!g_status.empty()) g_status.clear();
            if (!g_curName.empty() && g_main) SetWindowTextW(g_main, g_curName.c_str());
            InvalidateRect(g_video, nullptr, TRUE);
            break; }
        case MPV_EVENT_VIDEO_RECONFIG:
            if (g_fitPending) { g_fitPending = false; FitWindowToVideo(); }
            break;
        case MPV_EVENT_END_FILE: {
            auto* ef = (mpv_event_end_file*)e->data;
            if (ef->reason == MPV_END_FILE_REASON_ERROR) {
                g_loaded = false; StopAudioCapture(); g_audioOnly = false; g_status = Tr(L"No se puede reproducir este archivo."); InvalidateRect(g_video, nullptr, TRUE);
            }
            break; }
        case MPV_EVENT_CLIENT_MESSAGE: {
            auto* m = (mpv_event_client_message*)e->data;
            if (m->num_args >= 1) DoAction(m->args[0], m->num_args >= 2 ? m->args[1] : "");
            break; }
        case MPV_EVENT_SHUTDOWN: PostMessageW(g_main, WM_CLOSE, 0, 0); break;
        default: break;
        }
    }
    Redraw();
}

// ---------- pantalla completa ----------
static RECT g_normRc; static bool g_wasMax; static ULONGLONG g_fsTick;
static void LayoutChildren();
static void ToggleFS() {
    if (GetTickCount64() - g_fsTick < 250) return;   // evita dobles activaciones (autorepetición de teclas)
    g_fsTick = GetTickCount64();
    if (!g_fs) {
        WINDOWPLACEMENT wp{ sizeof wp }; GetWindowPlacement(g_main, &wp);
        g_normRc = wp.rcNormalPosition; g_wasMax = wp.showCmd == SW_SHOWMAXIMIZED;
        MONITORINFO mi{ sizeof mi }; GetMonitorInfoW(MonitorFromWindow(g_main, MONITOR_DEFAULTTONEAREST), &mi);
        if (g_wasMax) ShowWindow(g_main, SW_RESTORE);
        g_style = GetWindowLongW(g_main, GWL_STYLE);
        SetWindowLongW(g_main, GWL_STYLE, g_style & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU));
        g_fs = true;
        SetWindowPos(g_main, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        g_fs = false;
        SetWindowLongW(g_main, GWL_STYLE, g_style & ~WS_MAXIMIZE);
        SetWindowPos(g_main, HWND_NOTOPMOST, g_normRc.left, g_normRc.top, g_normRc.right - g_normRc.left,
                     g_normRc.bottom - g_normRc.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        if (g_wasMax) ShowWindow(g_main, SW_MAXIMIZE);
    }
    LayoutChildren();
    g_lastMove = GetTickCount64();
    RedrawWindow(g_main, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    PostMessageW(g_main, WM_RESETOVL, 0, 0);   // recrea la barra, flechas, info y X de pantalla completa
}

// ---------- barra de controles (ventana superpuesta con alfa por píxel) ----------
struct Layout { RECT seek, play, back, fwd, mute, vol, tracks, info, fs; int w, h; };
static Layout GetLayout(int w, int h) {
    Layout L{}; L.w = w; h = std::max(h, 1); L.h = h;
    int m = S(24), cy2 = h - S(34), cy1 = h - S(76), b = S(40);
    L.seek = { m + S(56), cy1 - S(14), w - m - S(56), cy1 + S(14) };
    int cx = w / 2;
    L.play = { cx - S(24), cy2 - S(24), cx + S(24), cy2 + S(24) };
    L.back = { cx - S(24) - S(64) - b / 2, cy2 - b / 2, cx - S(24) - S(64) + b / 2, cy2 + b / 2 };
    L.fwd = { cx + S(24) + S(64) - b / 2, cy2 - b / 2, cx + S(24) + S(64) + b / 2, cy2 + b / 2 };
    L.mute = { m, cy2 - b / 2, m + b, cy2 + b / 2 };
    L.vol = { m + b + S(6), cy2 - S(12), m + b + S(6) + S(100), cy2 + S(12) };
    L.fs = { w - m - b, cy2 - b / 2, w - m, cy2 + b / 2 };
    L.tracks = { w - m - b * 2 - S(4), cy2 - b / 2, w - m - b - S(4), cy2 + b / 2 };
    L.info = { w - m - b * 3 - S(8), cy2 - b / 2, w - m - b * 2 - S(8), cy2 + b / 2 };
    return L;
}
static bool In(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
static Hit HitTest(const Layout& L, int x, int y) {
    if (In(L.play, x, y)) return H_PLAY; if (In(L.back, x, y)) return H_BACK; if (In(L.fwd, x, y)) return H_FWD;
    if (In(L.mute, x, y)) return H_MUTE; if (In(L.vol, x, y)) return H_VOL; if (In(L.tracks, x, y)) return H_TRACKS;
    if (In(L.info, x, y)) return H_INFO;
    if (In(L.fs, x, y)) return H_FS; if (In(L.seek, x, y)) return H_SEEK;
    return H_NONE;
}

// ---------- iconos vectoriales (se dibujan con GDI+, no dependen de ninguna fuente) ----------
enum Ic { IC_PLAY, IC_PAUSE, IC_BACK, IC_FWD, IC_VOL, IC_MUTE, IC_TRACKS, IC_FS, IC_EXITFS, IC_INFO, IC_MORE };
static void RoundRectPath(GraphicsPath& p, float x, float y, float w, float h, float r) {
    p.AddArc(x, y, 2 * r, 2 * r, 180, 90); p.AddArc(x + w - 2 * r, y, 2 * r, 2 * r, 270, 90);
    p.AddArc(x + w - 2 * r, y + h - 2 * r, 2 * r, 2 * r, 0, 90); p.AddArc(x, y + h - 2 * r, 2 * r, 2 * r, 90, 90); p.CloseFigure();
}
static void DrawIcon(Graphics& g, Ic ic, const RECT& r, double vol, bool hot, float k = 1.0f) {
    float cx = (r.left + r.right) / 2.f, cy = (r.top + r.bottom) / 2.f, s = g_dpi / 96.f * k;
    if (hot) { SolidBrush hb(Color(40, 255, 255, 255)); g.FillEllipse(&hb, (REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left), (REAL)(r.bottom - r.top)); }
    Color white(255, 255, 255, 255);
    Pen pen(white, 1.8f); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound); pen.SetLineJoin(LineJoinRound);
    SolidBrush fill(white);
    GraphicsState st = g.Save();
    g.TranslateTransform(cx, cy); g.ScaleTransform(s, s);
    switch (ic) {
    case IC_PLAY: {
        PointF p[3] = { {-5.f, -8.f}, {-5.f, 8.f}, {8.f, 0.f} };
        Pen pj(white, 2.4f); pj.SetLineJoin(LineJoinRound);
        g.FillPolygon(&fill, p, 3); g.DrawPolygon(&pj, p, 3); break; }
    case IC_PAUSE: {
        Pen pb(white, 4.2f); pb.SetStartCap(LineCapRound); pb.SetEndCap(LineCapRound);
        g.DrawLine(&pb, -4.5f, -5.5f, -4.5f, 5.5f); g.DrawLine(&pb, 4.5f, -5.5f, 4.5f, 5.5f); break; }
    case IC_BACK: case IC_FWD: {
        if (ic == IC_BACK) g.ScaleTransform(-1.f, 1.f);
        const float R = 10.5f;
        g.DrawArc(&pen, -R, -R, 2 * R, 2 * R, -30.f, 300.f);
        PointF P(0.f, -R); const float L = 5.2f;
        g.DrawLine(&pen, P, PointF(P.X - L * 0.77f, P.Y + L * 0.64f));
        g.DrawLine(&pen, P, PointF(P.X - L * 0.77f, P.Y - L * 0.64f));
        break; }
    case IC_VOL: case IC_MUTE: {
        PointF p[6] = { {-10.f, -3.5f}, {-6.f, -3.5f}, {-1.f, -8.f}, {-1.f, 8.f}, {-6.f, 3.5f}, {-10.f, 3.5f} };
        Pen pt(white, 1.4f); pt.SetLineJoin(LineJoinRound);
        g.FillPolygon(&fill, p, 6); g.DrawPolygon(&pt, p, 6);
        if (ic == IC_MUTE) { g.DrawLine(&pen, 3.f, -4.f, 10.f, 4.f); g.DrawLine(&pen, 3.f, 4.f, 10.f, -4.f); }
        else {
            if (vol > 0) g.DrawArc(&pen, 0.f, -4.5f, 9.f, 9.f, -50.f, 100.f);
            if (vol >= 50) g.DrawArc(&pen, -3.f, -9.f, 18.f, 18.f, -50.f, 100.f);
        }
        break; }
    case IC_TRACKS: {
        Pen pt(white, 1.6f); pt.SetLineJoin(LineJoinRound); pt.SetStartCap(LineCapRound); pt.SetEndCap(LineCapRound);
        GraphicsPath bp; RoundRectPath(bp, -10.f, -8.f, 20.f, 14.f, 3.f); g.DrawPath(&pt, &bp);
        g.DrawLine(&pt, -4.f, 6.f, -6.f, 10.f); g.DrawLine(&pt, -6.f, 10.f, 0.f, 6.f);
        g.DrawLine(&pt, -6.f, -3.f, 2.f, -3.f); g.DrawLine(&pt, -6.f, 1.f, 6.f, 1.f);
        break; }
    case IC_INFO: {
        Pen pt(white, 1.8f);
        g.DrawEllipse(&pt, -9.f, -9.f, 18.f, 18.f);
        g.FillEllipse(&fill, -1.6f, -5.8f, 3.2f, 3.2f);
        Pen pl(white, 2.2f); pl.SetStartCap(LineCapRound); pl.SetEndCap(LineCapRound);
        g.DrawLine(&pl, 0.f, -1.2f, 0.f, 5.6f);
        break; }
    case IC_MORE: {
        SolidBrush dot(white);
        g.FillEllipse(&dot, -7.f, -1.7f, 3.4f, 3.4f);
        g.FillEllipse(&dot, -1.7f, -1.7f, 3.4f, 3.4f);
        g.FillEllipse(&dot, 3.6f, -1.7f, 3.4f, 3.4f);
        break; }
    case IC_FS: case IC_EXITFS: {
        for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) {
            float x = (float)sx, y = (float)sy;
            if (ic == IC_FS) { PointF q[3] = { {x * 8.f, y * 3.f}, {x * 8.f, y * 8.f}, {x * 3.f, y * 8.f} }; g.DrawLines(&pen, q, 3); }
            else             { PointF q[3] = { {x * 8.f, y * 3.f}, {x * 3.f, y * 3.f}, {x * 3.f, y * 8.f} }; g.DrawLines(&pen, q, 3); }
        }
        break; }
    }
    g.Restore(st);
    if (ic == IC_BACK || ic == IC_FWD) {
        StringFormat ctr; ctr.SetAlignment(StringAlignmentCenter); ctr.SetLineAlignment(StringAlignmentCenter);
        Font nf(L"Segoe UI Semibold", (REAL)S(9), FontStyleRegular, UnitPixel);
        RectF rf((REAL)r.left, (REAL)r.top + S(1), (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
        g.DrawString(ic == IC_BACK ? L"10" : L"30", -1, &nf, rf, &ctr, &fill);
    }
}

static Color Accent() {
    DWORD c = 0; BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&c, &opaque))) return Color(255, (c >> 16) & 255, (c >> 8) & 255, c & 255);
    return Color(255, 0, 120, 215);
}

// ---------- dibujo en ventanas superpuestas (capas con alfa por píxel) ----------
static void Present(HWND wnd, int x, int y, int w, int h, const std::function<void(Graphics&)>& draw) {
    if (!wnd || w <= 0 || h <= 0) return;
    BITMAPINFO bi{}; bi.bmiHeader = { sizeof bi.bmiHeader, w, -h, 1, 32, BI_RGB };
    HDC scr = GetDC(nullptr), mem = CreateCompatibleDC(scr);
    void* bits = nullptr; HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteDC(mem); ReleaseDC(nullptr, scr); return; }
    HGDIOBJ old = SelectObject(mem, bmp);
    {
        Bitmap bm(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        Graphics g(&bm);
        g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.Clear(Color(0, 0, 0, 0));
        draw(g);
    }
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    POINT org{ x, y }; SIZE sz{ w, h }; POINT src{ 0, 0 };
    UpdateLayeredWindow(wnd, scr, &org, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); ReleaseDC(nullptr, scr);
}

// Posición y tamaño (en pantalla) de la barra inferior
static void BarGeom(int& x, int& y, int& w, int& h) {
    RECT cr; GetClientRect(g_main, &cr);
    w = cr.right; h = std::min((int)cr.bottom, S(130));
    POINT org{ 0, cr.bottom - h }; ClientToScreen(g_main, &org); x = org.x; y = org.y;
}

static void Redraw() {
    if (!g_ovl || !IsWindowVisible(g_ovl)) return;
    int ox, oy, w, h; BarGeom(ox, oy, w, h);
    if (w <= 0 || h <= 0) return;
    Present(g_ovl, ox, oy, w, h, [&](Graphics& g) {
        LinearGradientBrush bg(Point(0, 0), Point(0, h), Color(0, 0, 0, 0), Color(225, 0, 0, 0));
        g.FillRectangle(&bg, 0, 0, w, h);
        Layout L = GetLayout(w, h);
        Color acc = Accent();
        SolidBrush white(Color(255, 255, 255, 255)), dim(Color(255, 190, 190, 190));
        StringFormat ctr; ctr.SetAlignment(StringAlignmentCenter); ctr.SetLineAlignment(StringAlignmentCenter);

        // barra de progreso
        int cy = (L.seek.top + L.seek.bottom) / 2, th = S(4);
        double frac = g_dur > 0 ? std::clamp(g_pos / g_dur, 0.0, 1.0) : 0;
        int x0 = L.seek.left, x1 = L.seek.right, xp = x0 + (int)((x1 - x0) * frac);
        SolidBrush track(Color(90, 255, 255, 255)), fill(acc);
        g.FillRectangle(&track, x0, cy - th / 2, x1 - x0, th);
        g.FillRectangle(&fill, x0, cy - th / 2, xp - x0, th);
        int kr = (g_drag == H_SEEK || g_hover == H_SEEK) ? S(8) : S(6);
        g.FillEllipse(&white, xp - kr, cy - kr, kr * 2, kr * 2);
        Font tf(L"Segoe UI", (REAL)S(12), FontStyleRegular, UnitPixel);
        RectF tl((REAL)S(24), (REAL)(cy - S(12)), (REAL)S(56), (REAL)S(24));
        RectF tr((REAL)(w - S(24) - S(56)), (REAL)(cy - S(12)), (REAL)S(56), (REAL)S(24));
        g.DrawString(FmtTime(g_pos).c_str(), -1, &tf, tl, &ctr, &white);
        g.DrawString(FmtTime(g_dur).c_str(), -1, &tf, tr, &ctr, &white);

        // iconos
        { Pen ring(Color(255, 255, 255, 255), (REAL)S(2)); RECT r = L.play;
          if (g_hover == H_PLAY) { SolidBrush hb(Color(40, 255, 255, 255)); g.FillEllipse(&hb, (REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left), (REAL)(r.bottom - r.top)); }
          g.DrawEllipse(&ring, (REAL)r.left + 1, (REAL)r.top + 1, (REAL)(r.right - r.left - 2), (REAL)(r.bottom - r.top - 2));
          DrawIcon(g, (g_pause || g_eof) ? IC_PLAY : IC_PAUSE, r, 0, false, 1.25f); }
        DrawIcon(g, IC_BACK, L.back, 0, g_hover == H_BACK); DrawIcon(g, IC_FWD, L.fwd, 0, g_hover == H_FWD);
        DrawIcon(g, (g_mute || g_vol <= 0) ? IC_MUTE : IC_VOL, L.mute, g_vol, g_hover == H_MUTE);
        DrawIcon(g, IC_INFO, L.info, 0, g_hover == H_INFO || g_showInfo);
        DrawIcon(g, IC_MORE, L.tracks, 0, g_hover == H_TRACKS);
        DrawIcon(g, g_fs ? IC_EXITFS : IC_FS, L.fs, 0, g_hover == H_FS);

        // volumen
        int vy = (L.vol.top + L.vol.bottom) / 2, vx0 = L.vol.left, vx1 = L.vol.right;
        double vf = std::clamp((g_mute ? 0.0 : g_vol) / 100.0, 0.0, 1.0);
        int vxp = vx0 + (int)((vx1 - vx0) * vf);
        g.FillRectangle(&track, vx0, vy - S(2), vx1 - vx0, S(4)); g.FillRectangle(&fill, vx0, vy - S(2), vxp - vx0, S(4));
        g.FillEllipse(&white, vxp - S(6), vy - S(6), S(12), S(12));
    });
}

// ---------- flechas laterales (vídeo anterior / siguiente) ----------
static void NavGeom(int dir, int& x, int& y, int& sz) {
    RECT cr; GetClientRect(g_main, &cr); POINT o{ 0, 0 }; ClientToScreen(g_main, &o);
    sz = S(36); y = o.y + (cr.bottom - sz) / 2;
    x = dir < 0 ? o.x + S(12) : o.x + cr.right - S(12) - sz;
}
// Solo el chevrón (con una sombra suave para que se vea sobre cualquier imagen); sin círculo.
static void DrawNavArrow(Graphics& g, int dir, bool hot, int sz) {
    SolidBrush hit(Color(2, 0, 0, 0)); g.FillRectangle(&hit, 0, 0, sz, sz);   // casi invisible: hace que toda la zona sea clicable
    float s = g_dpi / 96.f, cx = sz / 2.f, cy = sz / 2.f, d = (float)dir;
    PointF q[3] = { { cx - d * 4.f * s, cy - 10.f * s }, { cx + d * 4.f * s, cy }, { cx - d * 4.f * s, cy + 10.f * s } };
    Pen sh(Color(hot ? 130 : 90, 0, 0, 0), (hot ? 5.2f : 4.6f) * s); sh.SetStartCap(LineCapRound); sh.SetEndCap(LineCapRound); sh.SetLineJoin(LineJoinRound);
    g.DrawLines(&sh, q, 3);
    Pen p(Color(hot ? 255 : 200, 255, 255, 255), (hot ? 3.2f : 2.6f) * s); p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound); p.SetLineJoin(LineJoinRound);
    g.DrawLines(&p, q, 3);
}
static void RedrawNav() {
    for (int dir = -1; dir <= 1; dir += 2) {
        HWND w = dir < 0 ? g_prev : g_next;
        if (!w || !IsWindowVisible(w)) continue;
        int x, y, sz; NavGeom(dir, x, y, sz);
        Present(w, x, y, sz, sz, [&](Graphics& g) { DrawNavArrow(g, dir, g_hoverNav == dir, sz); });
    }
}

// ---------- traduccion automatica segun el idioma de Windows ----------
static std::wstring Tr(const wchar_t* es) {
    const LANGID lang = PRIMARYLANGID(GetUserDefaultUILanguage());
    auto p = [&](const wchar_t* v){ return std::wstring(v); };
    if (lang == LANG_SPANISH) return p(es);
    struct T { const wchar_t* es; const wchar_t* en; const wchar_t* fr; const wchar_t* de; const wchar_t* it; const wchar_t* pt; const wchar_t* nl; const wchar_t* pl; const wchar_t* ru; const wchar_t* ja; const wchar_t* ko; const wchar_t* zh; const wchar_t* zht; const wchar_t* tr; };
    static const T t[] = {
      {L"Archivo",L"File",L"Fichier",L"Datei",L"File",L"Arquivo",L"Bestand",L"Plik",L"Файл",L"ファイル",L"파일",L"文件",L"檔案",L"Dosya"},
      {L"Nombre",L"Name",L"Nom",L"Name",L"Nome",L"Nome",L"Naam",L"Nazwa",L"Имя",L"名前",L"이름",L"名称",L"名稱",L"Ad"},
      {L"Contenedor",L"Container",L"Conteneur",L"Container",L"Contenitore",L"Contêiner",L"Container",L"Kontener",L"Контейнер",L"コンテナ",L"컨테이너",L"容器",L"容器",L"Kapsayıcı"},
      {L"Tamano",L"Size",L"Taille",L"Größe",L"Dimensione",L"Tamanho",L"Grootte",L"Rozmiar",L"Размер",L"サイズ",L"크기",L"大小",L"大小",L"Boyut"},
      {L"Bitrate medio",L"Average bitrate",L"Débit moyen",L"Durchschnittliche Bitrate",L"Bitrate media",L"Bitrate médio",L"Gemiddelde bitrate",L"Średnia przepływność",L"Средний битрейт",L"平均ビットレート",L"평균 비트레이트",L"平均比特率",L"平均位元率",L"Ortalama bit hızı"},
      {L"Video",L"Video",L"Vidéo",L"Video",L"Video",L"Vídeo",L"Video",L"Wideo",L"Видео",L"ビデオ",L"비디오",L"视频",L"影片",L"Video"},
      {L"Codec",L"Codec",L"Codec",L"Codec",L"Codec",L"Codec",L"Codec",L"Kodek",L"Кодек",L"コーデック",L"코덱",L"编码",L"編碼",L"Kodek"},
      {L"Resolucion",L"Resolution",L"Résolution",L"Auflösung",L"Risoluzione",L"Resolução",L"Resolutie",L"Rozdzielczość",L"Разрешение",L"解像度",L"해상도",L"分辨率",L"解析度",L"Çözünürlük"},
      {L"Relacion de aspecto",L"Aspect ratio",L"Format d'image",L"Seitenverhältnis",L"Rapporto d'aspetto",L"Proporção",L"Beeldverhouding",L"Proporcje obrazu",L"Соотношение сторон",L"アスペクト比",L"화면 비율",L"宽高比",L"長寬比",L"En-boy oranı"},
      {L"Fotogramas/s",L"Frames/s",L"Images/s",L"Bilder/s",L"Fotogrammi/s",L"Fotogramas/s",L"Frames/s",L"Klatki/s",L"Кадров/с",L"フレーム/秒",L"프레임/초",L"帧/秒",L"幀/秒",L"Kare/sn"},
      {L"Color",L"Color",L"Couleur",L"Farbe",L"Colore",L"Cor",L"Kleur",L"Kolor",L"Цвет",L"色",L"색상",L"颜色",L"色彩",L"Renk"},
      {L"Salida de pantalla",L"Display output",L"Sortie écran",L"Bildschirmausgabe",L"Uscita display",L"Saída do ecrã",L"Beeldschermuitvoer",L"Wyjście ekranu",L"Вывод на экран",L"ディスプレイ出力",L"디스플레이 출력",L"显示输出",L"顯示輸出",L"Ekran çıkışı"},
      {L"Decodificacion",L"Decoding",L"Décodage",L"Dekodierung",L"Decodifica",L"Descodificação",L"Decodering",L"Dekodowanie",L"Декодирование",L"デコード",L"디코딩",L"解码",L"解碼",L"Kod çözme"},
      {L"Fotogramas perdidos",L"Dropped frames",L"Images perdues",L"Verlorene Frames",L"Fotogrammi persi",L"Fotogramas perdidos",L"Verloren frames",L"Utracone klatki",L"Пропущенные кадры",L"ドロップフレーム",L"드롭 프레임",L"丢帧",L"遺失影格",L"Atlanan kareler"},
      {L"Audio",L"Audio",L"Audio",L"Audio",L"Audio",L"Áudio",L"Audio",L"Audio",L"Аудио",L"オーディオ",L"오디오",L"音频",L"音訊",L"Ses"},
      {L"Pista",L"Track",L"Piste",L"Spur",L"Traccia",L"Faixa",L"Track",L"Ścieżka",L"Дорожка",L"トラック",L"트랙",L"轨道",L"軌道",L"Parça"},
      {L"Sin audio",L"No audio",L"Sans audio",L"Kein Audio",L"Nessun audio",L"Sem áudio",L"Geen audio",L"Brak dźwięku",L"Нет аудио",L"音声なし",L"오디오 없음",L"无音频",L"無音訊",L"Ses yok"},
      {L"Frecuencia",L"Sample rate",L"Fréquence",L"Abtastrate",L"Frequenza",L"Frequência",L"Samplefrequentie",L"Częstotliwość",L"Частота",L"サンプルレート",L"샘플링 레이트",L"采样率",L"取樣率",L"Örnekleme hızı"},
      {L"Canales",L"Channels",L"Canaux",L"Kanäle",L"Canali",L"Canais",L"Kanalen",L"Kanały",L"Каналы",L"チャンネル",L"채널",L"声道",L"聲道",L"Kanallar"},
      {L"Subtitulos",L"Subtitles",L"Sous-titres",L"Untertitel",L"Sottotitoli",L"Legendas",L"Ondertitels",L"Napisy",L"Субтитры",L"字幕",L"자막",L"字幕",L"字幕",L"Altyazılar"},
      {L"Velocidad",L"Speed",L"Vitesse",L"Geschwindigkeit",L"Velocità",L"Velocidade",L"Snelheid",L"Prędkość",L"Скорость",L"速度",L"속도",L"速度",L"速度",L"Hız"},
      {L"Abrir archivo…",L"Open file…",L"Ouvrir un fichier…",L"Datei öffnen…",L"Apri file…",L"Abrir ficheiro…",L"Bestand openen…",L"Otwórz plik…",L"Открыть файл…",L"ファイルを開く…",L"파일 열기…",L"打开文件…",L"開啟檔案…",L"Dosya aç…"},
      {L"←  Volver",L"←  Back",L"←  Retour",L"←  Zurück",L"←  Indietro",L"←  Voltar",L"←  Terug",L"←  Wstecz",L"←  Назад",L"←  戻る",L"←  뒤로",L"←  返回",L"←  返回",L"←  Geri"},
      {L"Desactivados",L"Disabled",L"Désactivés",L"Deaktiviert",L"Disattivati",L"Desativadas",L"Uitgeschakeld",L"Wyłączone",L"Отключены",L"無効",L"사용 안 함",L"禁用",L"停用",L"Devre dışı"},
      {L"Preparando video…",L"Preparing video…",L"Préparation de la vidéo…",L"Video wird vorbereitet…",L"Preparazione video…",L"A preparar vídeo…",L"Video voorbereiden…",L"Przygotowywanie wideo…",L"Подготовка видео…",L"動画を準備中…",L"비디오 준비 중…",L"正在准备视频…",L"正在準備影片…",L"Video hazırlanıyor…"},
      {L"Arrastra un video aqui o pulsa Ctrl+O",L"Drag a video here or press Ctrl+O",L"Glissez une vidéo ici ou appuyez sur Ctrl+O",L"Video hierher ziehen oder Strg+O drücken",L"Trascina un video qui o premi Ctrl+O",L"Arraste um vídeo para aqui ou prima Ctrl+O",L"Sleep een video hierheen of druk op Ctrl+O",L"Przeciągnij tutaj wideo lub naciśnij Ctrl+O",L"Перетащите видео сюда или нажмите Ctrl+O",L"ここに動画をドラッグするか Ctrl+O を押してください",L"여기에 비디오를 끌어 놓거나 Ctrl+O를 누르세요",L"将视频拖到这里或按 Ctrl+O",L"將影片拖曳到這裡或按 Ctrl+O",L"Videoyu buraya sürükleyin veya Ctrl+O'ya basın"},
      {L"No se puede reproducir este archivo.",L"This file cannot be played.",L"Impossible de lire ce fichier.",L"Diese Datei kann nicht wiedergegeben werden.",L"Impossibile riprodurre questo file.",L"Não é possível reproduzir este ficheiro.",L"Dit bestand kan niet worden afgespeeld.",L"Nie można odtworzyć tego pliku.",L"Не удается воспроизвести этот файл.",L"このファイルを再生できません。",L"이 파일을 재생할 수 없습니다.",L"无法播放此文件。",L"無法播放此檔案。",L"Bu dosya oynatılamıyor."},
      {L"(sin subtitulos)",L"(no subtitles)",L"(aucun sous-titre)",L"(keine Untertitel)",L"(nessun sottotitolo)",L"(sem legendas)",L"(geen ondertitels)",L"(brak napisów)",L"(нет субтитров)",L"(字幕なし)",L"(자막 없음)",L"(无字幕)",L"(無字幕)",L"(altyazı yok)"},
      {L"(sin pistas de audio)",L"(no audio tracks)",L"(aucune piste audio)",L"(keine Audiospuren)",L"(nessuna traccia audio)",L"(sem faixas de áudio)",L"(geen audiotracks)",L"(brak ścieżek audio)",L"(нет аудиодорожек)",L"(音声トラックなし)",L"(오디오 트랙 없음)",L"(无音轨)",L"(無音訊軌)",L"(ses parçası yok)"},
      {L"Subtitulo ",L"Subtitle ",L"Sous-titre ",L"Untertitel ",L"Sottotitolo ",L"Legenda ",L"Ondertitel ",L"Napisy ",L"Субтитр ",L"字幕 ",L"자막 ",L"字幕 ",L"字幕 ",L"Altyazı "},
      {L"Pista de audio ",L"Audio track ",L"Piste audio ",L"Audiospur ",L"Traccia audio ",L"Faixa de áudio ",L"Audiotrack ",L"Ścieżka audio ",L"Аудиодорожка ",L"オーディオトラック ",L"오디오 트랙 ",L"音频轨道 ",L"音訊軌 ",L"Ses parçası "},
      {L"HDR nativo",L"Native HDR",L"HDR natif",L"Natives HDR",L"HDR nativo",L"HDR nativo",L"Native HDR",L"Natywne HDR",L"Нативный HDR",L"ネイティブHDR",L"네이티브 HDR",L"原生 HDR",L"原生 HDR",L"Yerel HDR"},
      {L"SDR con tone-mapping",L"SDR tone-mapped",L"SDR avec tone mapping",L"SDR mit Tone-Mapping",L"SDR con tone mapping",L"SDR com tone mapping",L"SDR met tone mapping",L"SDR z mapowaniem tonów",L"SDR с тональным отображением",L"SDRトーンマッピング",L"SDR 톤 매핑",L"SDR 色调映射",L"SDR 色調映射",L"SDR ton eşlemeli"},
      {L"Bitrate",L"Bitrate",L"Débit",L"Bitrate",L"Bitrate",L"Bitrate",L"Bitrate",L"Przepływność",L"Битрейт",L"ビットレート",L"비트레이트",L"比特率",L"位元率",L"Bit hızı"},
      {L"Software",L"Software",L"Logiciel",L"Software",L"Software",L"Software",L"Software",L"Programowe",L"Программное",L"ソフトウェア",L"소프트웨어",L"软件",L"軟體",L"Yazılım"}
    };
    for (const auto& x : t) if (!wcscmp(es, x.es)) {
        switch (lang) {
        case LANG_ENGLISH: return p(x.en); case LANG_FRENCH: return p(x.fr); case LANG_GERMAN: return p(x.de); case LANG_ITALIAN: return p(x.it);
        case LANG_PORTUGUESE: return p(x.pt); case LANG_DUTCH: return p(x.nl); case LANG_POLISH: return p(x.pl); case LANG_RUSSIAN: return p(x.ru);
        case LANG_JAPANESE: return p(x.ja); case LANG_KOREAN: return p(x.ko); case LANG_CHINESE: return p(x.zh); case LANG_TURKISH: return p(x.tr);
        default: return p(es);
        }
    }
    return p(es);
}

// ---------- panel de información en tiempo real ----------
struct InfoRow { std::wstring k, v; bool head; };
static std::wstring PW(const char* n) { return W(Prop(n).c_str()); }
static std::wstring Dec(const std::string& s, int d) {
    if (s.empty()) return {};
    wchar_t b[48]; swprintf_s(b, L"%.*f", d, atof(s.c_str())); std::wstring r = b;
    if (d > 0 && r.find(L'.') != std::wstring::npos) { while (r.back() == L'0') r.pop_back(); if (r.back() == L'.') r.pop_back(); }
    return r;
}
static std::wstring FmtSize(double b) {
    wchar_t s[48];
    if (b >= 1073741824.0) swprintf_s(s, L"%.2f GB", b / 1073741824.0);
    else if (b >= 1048576.0) swprintf_s(s, L"%.1f MB", b / 1048576.0);
    else swprintf_s(s, L"%.0f KB", b / 1024.0);
    return s;
}
static std::wstring FmtRate(const std::string& str) {   // bits por segundo
    if (str.empty()) return {};
    double b = atof(str.c_str()); if (b <= 0) return {};
    if (b < 5000) b *= 1000;   // por si la versión de libmpv lo da en kbit/s
    wchar_t s[48];
    if (b >= 1e6) swprintf_s(s, L"%.2f Mbps", b / 1e6); else swprintf_s(s, L"%.0f kbps", b / 1e3);
    return s;
}
// "file-format" de mpv devuelve la lista de nombres del demuxer (p. ej. "mov,mp4,m4a,3gp,3g2,mj2"): se muestra solo el real.
static std::wstring ContainerName() {
    if (g_curExt == L".usm") return L"USM (CRI Sofdec2)";
    std::string raw = Prop("file-format"); if (raw.empty()) return {};
    std::vector<std::string> n; size_t st = 0;
    while (st <= raw.size()) { size_t c = raw.find(',', st); if (c == std::string::npos) c = raw.size(); if (c > st) n.push_back(raw.substr(st, c - st)); st = c + 1; }
    if (n.empty()) return {};
    auto has = [&](const char* k) { for (auto& x : n) if (x == k) return true; return false; };
    std::wstring e = g_curExt;
    if (has("matroska")) return e == L".webm" ? L"WebM" : (e == L".mka" ? L"Matroska (audio)" : L"Matroska (MKV)");
    if (has("mov")) {
        if (e == L".mov" || e == L".qt") return L"QuickTime (MOV)";
        if (e == L".3gp" || e == L".3g2") return L"3GP";
        if (e == L".m4a") return L"MPEG-4 audio (M4A)";
        return L"MPEG-4 (MP4)";
    }
    if (has("mpegts")) return L"MPEG-TS";
    if (has("mpeg")) return L"MPEG-PS";
    if (has("avi")) return L"AVI";
    if (has("asf")) return L"ASF / WMV";
    if (has("flv")) return L"FLV";
    if (has("ogg")) return L"Ogg";
    if (has("wav")) return L"WAV";
    for (auto& x : n) if (!e.empty() && W(x.c_str()) == e.substr(1)) return W(x.c_str());   // el que coincide con la extensión
    std::wstring r = W(n[0].c_str()); for (auto& c : r) c = (wchar_t)towupper(c);
    return r;
}
static std::wstring AspectRatioText(const std::string& a) {
    if (a.empty()) return {};
    double v = atof(a.c_str());
    if (v <= 0.0) return {};
    const double common[][2] = {{16,9},{4,3},{21,9},{3,2},{1,1},{5,4},{16,10},{32,9}};
    for (auto &q : common) if (fabs(v - q[0]/q[1]) < 0.015) return std::to_wstring((int)q[0]) + L":" + std::to_wstring((int)q[1]);
    int den = 1000, num = (int)std::lround(v * den); int g = std::gcd(num, den);
    return std::to_wstring(num/g) + L":" + std::to_wstring(den/g);
}

static std::vector<InfoRow> BuildInfo() {
    std::vector<InfoRow> r;
    auto head = [&](const std::wstring& t) { r.push_back({ t, std::wstring(), true }); };
    auto row = [&](const std::wstring& k, const std::wstring& v) { if (!v.empty()) r.push_back({ k, v, false }); };

    head(Tr(L"Archivo"));
    row(Tr(L"Nombre"), g_curName);
    row(Tr(L"Contenedor"), ContainerName());
    std::string fs = Prop("file-size");
    if (!fs.empty()) row(Tr(L"Tamano"), FmtSize(atof(fs.c_str())));
    if (!fs.empty() && g_dur > 0) row(Tr(L"Bitrate medio"), FmtRate(std::to_string(atof(fs.c_str()) * 8.0 / g_dur)));

    std::string vc = Prop("video-codec");
    if (!vc.empty()) {
        head(Tr(L"Video"));
        row(Tr(L"Codec"), W(vc.c_str()));
        std::wstring vw = PW("video-params/w"), vh = PW("video-params/h");
        if (vw.empty()) { vw = PW("dwidth"); vh = PW("dheight"); }
        if (!vw.empty()) row(Tr(L"Resolucion"), vw + L" × " + vh);
        row(Tr(L"Relacion de aspecto"), AspectRatioText(Prop("video-params/aspect")));
        std::wstring cf = Dec(Prop("container-fps"), 3), ef = Dec(Prop("estimated-vf-fps"), 1);
        if (!cf.empty()) row(Tr(L"Fotogramas/s"), cf);
        row(Tr(L"Bitrate"), FmtRate(Prop("video-bitrate")));
        std::string prim = Prop("video-params/primaries"), gam = Prop("video-params/gamma");
        std::wstring col = W(prim.c_str());
        if (!gam.empty()) col += (col.empty() ? std::wstring() : std::wstring(L" / ")) + W(gam.c_str());
        row(Tr(L"Color"), col);
        bool hdr = (gam == "pq" || gam == "hlg") && (prim == "bt.2020" || prim == "dci-p3");
        if (hdr) {
            row(Tr(L"HDR"), gam == "pq" ? Tr(L"HDR activo") : Tr(L"HDR activo (HLG)"));
        } else {
            row(Tr(L"HDR"), Tr(L"No (SDR)"));
        }
        std::wstring d1 = PW("frame-drop-count"), d2 = PW("decoder-frame-drop-count");
        std::wstring seen = PW("estimated-frame-number");
        long long dropped = 0, n1 = 0, n2 = 0;
        if (!d1.empty()) n1 = _wtoll(d1.c_str());
        if (!d2.empty()) n2 = _wtoll(d2.c_str());
        dropped = std::max(0LL, n1) + std::max(0LL, n2);
        if (seen.empty()) seen = L"0";
        row(Tr(L"Fotogramas perdidos"), std::to_wstring(dropped) + L" / " + seen);
    }

    head(Tr(L"Audio"));
    std::string ac = Prop("audio-codec-name"); if (ac.empty()) ac = Prop("audio-codec");
    if (ac.empty() || Prop("aid") == "no") row(Tr(L"Pista"), Tr(L"Sin audio"));
    else {
        row(Tr(L"Codec"), W(ac.c_str()));
        std::wstring sr = PW("audio-params/samplerate"); if (!sr.empty()) row(Tr(L"Frecuencia"), sr + L" Hz");
        std::wstring ch = PW("audio-params/hr-channels"); if (ch.empty()) ch = PW("audio-params/channel-count");
        row(Tr(L"Canales"), ch);
        row(Tr(L"Bitrate"), FmtRate(Prop("audio-bitrate")));
    }

    return r;
}

static void RedrawInfo() {
    if (!g_info || !IsWindowVisible(g_info)) return;
    std::vector<InfoRow> rows = BuildInfo();
    RECT cr; GetClientRect(g_main, &cr); POINT o{ 0, 0 }; ClientToScreen(g_main, &o);
    int pw = S(390), pad = S(14), keyW = S(128), rh = S(19), hh = S(28);
    int ph = pad * 2; for (auto& x : rows) ph += x.head ? hh : rh;
    ph = std::min(ph, std::max((int)cr.bottom - S(32), S(60)));
    pw = std::min(pw, std::max((int)cr.right - S(32), S(120)));
    Color acc = Accent();
    Present(g_info, o.x + S(16), o.y + S(16), pw, ph, [&](Graphics& g) {
        GraphicsPath bp; RoundRectPath(bp, 0.5f, 0.5f, (REAL)pw - 1.f, (REAL)ph - 1.f, (REAL)S(10));
        SolidBrush bg(Color(215, 16, 16, 16)); g.FillPath(&bg, &bp);
        Pen bd(Color(70, 255, 255, 255), 1.f); g.DrawPath(&bd, &bp);
        Font f(L"Segoe UI", (REAL)S(12), FontStyleRegular, UnitPixel), fh(L"Segoe UI Semibold", (REAL)S(12), FontStyleRegular, UnitPixel);
        SolidBrush dim(Color(255, 160, 160, 160)), white(Color(255, 245, 245, 245)), ab(acc);
        StringFormat sf; sf.SetTrimming(StringTrimmingEllipsisCharacter); sf.SetFormatFlags(StringFormatFlagsNoWrap); sf.SetLineAlignment(StringAlignmentCenter);
        int y = pad;
        for (auto& x : rows) {
            int hgt = x.head ? hh : rh;
            if (y + hgt > ph - pad + S(2)) break;
            if (x.head) {
                std::wstring t = x.k; for (auto& c : t) c = (wchar_t)towupper(c);
                g.DrawString(t.c_str(), -1, &fh, RectF((REAL)pad, (REAL)(y + S(6)), (REAL)(pw - pad * 2), (REAL)(hgt - S(6))), &sf, &ab);
            } else {
                g.DrawString(x.k.c_str(), -1, &f, RectF((REAL)pad, (REAL)y, (REAL)keyW, (REAL)hgt), &sf, &dim);
                g.DrawString(x.v.c_str(), -1, &f, RectF((REAL)(pad + keyW), (REAL)y, (REAL)(pw - pad * 2 - keyW), (REAL)hgt), &sf, &white);
            }
            y += hgt;
        }
    });
}

static void RedrawAll() { Redraw(); RedrawNav(); RedrawInfo(); }

// ---------- visibilidad de las superposiciones ----------
static void SetVis(HWND w, bool want, void (*draw)()) {
    if (!w) return;
    bool vis = IsWindowVisible(w) != 0;
    if (want && !vis) { ShowWindow(w, SW_SHOWNOACTIVATE); if (draw) draw(); }
    else if (!want && vis) ShowWindow(w, SW_HIDE);
}

// Garantiza que la superposición esté realmente encima de la ventana principal (y con el mismo estado "siempre visible").
static void EnsureTop(HWND w) {
    if (!w || !IsWindowVisible(w)) return;
    bool mainTop = (GetWindowLongW(g_main, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    bool ovTop = (GetWindowLongW(w, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    if (ovTop != mainTop) { SetWindowPos(w, mainTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); return; }
    for (HWND p = GetWindow(w, GW_HWNDPREV); p; p = GetWindow(p, GW_HWNDPREV))
        if (p == g_main) { SetWindowPos(w, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); return; }
}

// La actividad del ratón se detecta por sondeo, sin depender de que mpv avise: así los controles
// reaparecen siempre, también tras maximizar/restaurar o entrar y salir de pantalla completa.
static POINT g_lastCur{ -1, -1 };
static void PollCursor() {
    POINT p; if (!GetCursorPos(&p)) return;
    if (p.x == g_lastCur.x && p.y == g_lastCur.y) return;
    g_lastCur = p;
    HWND u = WindowFromPoint(p);
    if (u && GetAncestor(u, GA_ROOTOWNER) == g_main) g_lastMove = GetTickCount64();
}

static void CloseFsGeom(int& x, int& y, int& sz) {
    RECT cr; GetClientRect(g_main, &cr);
    POINT o{0,0}; ClientToScreen(g_main, &o);
    sz = S(44); x = o.x + cr.right - sz - S(14); y = o.y + S(14);
}

static void RedrawCloseFs() {
    if (!g_closefs || !g_fs) return;
    int x,y,sz; CloseFsGeom(x,y,sz);
    Present(g_closefs, x, y, sz, sz, [&](Graphics& g) {
        // La X es únicamente la X: no se dibuja ningún círculo/fondo alrededor.
        Pen pen(Color(245,255,255,255), SR(2.4f));
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        float a=(float)S(13), b=(float)S(31);
        g.DrawLine(&pen,a,a,b,b);
        g.DrawLine(&pen,b,a,a,b);
    });
}


static void AudioVisGeom(int& x, int& y, int& w, int& h) {
    RECT cr; GetClientRect(g_main, &cr);
    POINT o{ 0, 0 }; ClientToScreen(g_main, &o);
    x = o.x; y = o.y; w = cr.right; h = cr.bottom;
}

// Visualizador FFT para archivos sin pista de vídeo. Captura el audio del dispositivo
// de salida de Windows y muestra las bandas de frecuencia reales.
static void RedrawAudioVis() {
    if (!g_audioVis || !g_audioOnly || !g_loaded) return;
    int x, y, w, h; AudioVisGeom(x, y, w, h);
    if (w <= 0 || h <= 0) return;
    std::array<float, kFftBars> levels;
    { std::lock_guard<std::mutex> lock(g_fftMutex); levels = g_fftLevels; }

    Present(g_audioVis, x, y, w, h, [&](Graphics& g) {
        const int n = kFftBars;
        // The spectrum is centered vertically: the same signal is visible above
        // and below the center line, like a classic symmetrical equalizer.
        const REAL centerY = (REAL)h * 0.50f;
        const REAL maxH = (REAL)std::min(S(520), std::max(S(220), (int)(h * 0.43)));
        const double pauseMul = g_pause ? 0.10 : 1.0;

        // Keep the 2000-frequency resolution internally, but only create enough
        // visible samples for the actual pixel width. This avoids a huge GDI+ path
        // while preserving the detail of the 2000-band spectrum.
        const int samples = std::min(std::max(240, w / 2), n);
        std::vector<float> values(samples);
        for (int px = 0; px < samples; ++px) {
            const double pos = (double)px * (n - 1) / (double)(samples - 1);
            const int i0 = std::clamp((int)std::floor(pos), 0, n - 1);
            const int i1 = std::clamp(i0 + 1, 0, n - 1);
            const float frac = (float)(pos - std::floor(pos));
            const float v = levels[i0] + (levels[i1] - levels[i0]) * frac;
            // A little extra compression keeps loud low-frequency sections from
            // becoming a flat wall while retaining visible movement everywhere.
            values[px] = (float)std::clamp(std::pow((double)v, 0.88), 0.0, 1.0) * (float)pauseMul;
        }

        // Smooth the displayed envelope. This rounds the tips instead of producing
        // square staircase tops when several adjacent FFT bands are similar.
        std::vector<float> smooth(samples);
        for (int i = 0; i < samples; ++i) {
            const int a = std::max(0, i - 2);
            const int b = std::min(samples - 1, i + 2);
            float sum = 0.0f;
            float weight = 0.0f;
            for (int j = a; j <= b; ++j) {
                const float d = (float)std::abs(j - i);
                const float ww = (d == 0.0f) ? 1.0f : (d == 1.0f ? 0.65f : 0.30f);
                sum += values[j] * ww;
                weight += ww;
            }
            smooth[i] = weight > 0.0f ? sum / weight : values[i];
        }

        GraphicsPath spectrumPath;
        GraphicsPath glowPath;
        spectrumPath.SetFillMode(FillModeWinding);
        glowPath.SetFillMode(FillModeWinding);

        auto pointAt = [&](int i, bool lower, REAL extra) -> PointF {
            const REAL px = (REAL)i * (REAL)w / (REAL)(samples - 1);
            const REAL amp = maxH * smooth[i] + extra;
            return lower ? PointF(px, centerY + amp) : PointF(px, centerY - amp);
        };

        // Build a smooth cubic curve through the spectrum points. The lower edge
        // mirrors the upper edge, so there is no empty half of the visualizer.
        if (samples >= 3) {
            std::vector<PointF> upper;
            std::vector<PointF> lower;
            upper.reserve(samples);
            lower.reserve(samples);
            for (int i = 0; i < samples; ++i) {
                upper.push_back(pointAt(i, false, 0.0f));
                lower.push_back(pointAt(i, true, 0.0f));
            }

            spectrumPath.StartFigure();
            spectrumPath.AddLine(PointF(0.0f, centerY), upper[0]);
            for (int i = 1; i < samples; ++i) {
                const PointF prev = upper[i - 1];
                const PointF cur = upper[i];
                const PointF next = (i + 1 < samples) ? upper[i + 1] : cur;
                const REAL dx = (cur.X - prev.X) * 0.32f;
                PointF c1(prev.X + dx, prev.Y);
                PointF c2(cur.X - (next.X - prev.X) * 0.12f, cur.Y);
                spectrumPath.AddBezier(prev, c1, c2, cur);
            }
            spectrumPath.AddLine(PointF((REAL)w, centerY), lower.back());
            for (int i = samples - 1; i > 0; --i) {
                const PointF prev = lower[i];
                const PointF cur = lower[i - 1];
                const PointF next = (i >= 2) ? lower[i - 2] : cur;
                const REAL dx = (prev.X - cur.X) * 0.32f;
                PointF c1(prev.X - dx, prev.Y);
                PointF c2(cur.X + (prev.X - next.X) * 0.12f, cur.Y);
                spectrumPath.AddBezier(prev, c1, c2, cur);
            }
            spectrumPath.AddLine(lower[0], PointF(0.0f, centerY));
            spectrumPath.CloseFigure();

            glowPath.StartFigure();
            const REAL gOff = SR(2.0f);
            PointF gu = pointAt(0, false, -gOff);
            glowPath.AddLine(PointF(0.0f, centerY + gOff), gu);
            for (int i = 1; i < samples; ++i) {
                PointF prev = pointAt(i - 1, false, -gOff);
                PointF cur = pointAt(i, false, -gOff);
                PointF next = (i + 1 < samples) ? pointAt(i + 1, false, -gOff) : cur;
                REAL dx = (cur.X - prev.X) * 0.32f;
                glowPath.AddBezier(prev, PointF(prev.X + dx, prev.Y),
                    PointF(cur.X - (next.X - prev.X) * 0.12f, cur.Y), cur);
            }
            glowPath.AddLine(PointF((REAL)w, centerY + gOff), pointAt(samples - 1, true, gOff));
            for (int i = samples - 1; i > 0; --i) {
                PointF prev = pointAt(i, true, gOff);
                PointF cur = pointAt(i - 1, true, gOff);
                PointF next = (i >= 2) ? pointAt(i - 2, true, gOff) : cur;
                REAL dx = (prev.X - cur.X) * 0.32f;
                glowPath.AddBezier(prev, PointF(prev.X - dx, prev.Y),
                    PointF(cur.X + (prev.X - next.X) * 0.12f, cur.Y), cur);
            }
            glowPath.AddLine(pointAt(0, true, gOff), PointF(0.0f, centerY + gOff));
            glowPath.CloseFigure();
        }

        LinearGradientBrush spectrum(
            PointF(0.0f, 0.0f), PointF((REAL)w, 0.0f),
            Color(235, 48, 196, 255), Color(235, 195, 64, 255));
        Color spectrumColors[3] = {
            Color(235, 48, 196, 255), Color(235, 90, 126, 255), Color(235, 195, 64, 255)
        };
        REAL spectrumPositions[3] = { 0.0f, 0.58f, 1.0f };
        spectrum.SetInterpolationColors(spectrumColors, spectrumPositions, 3);

        LinearGradientBrush glowBrush(
            PointF(0.0f, 0.0f), PointF((REAL)w, 0.0f),
            Color(24, 48, 196, 255), Color(24, 195, 64, 255));
        Color glowColors[3] = {
            Color(24, 48, 196, 255), Color(24, 90, 126, 255), Color(24, 195, 64, 255)
        };
        REAL glowPositions[3] = { 0.0f, 0.58f, 1.0f };
        glowBrush.SetInterpolationColors(glowColors, glowPositions, 3);

        if (glowPath.GetPointCount() > 0) g.FillPath(&glowBrush, &glowPath);
        if (spectrumPath.GetPointCount() > 0) g.FillPath(&spectrum, &spectrumPath);
    });
}

static void UpdateOverlayVisibility() {
    if (!g_main) return;
    PollCursor();
    ULONGLONG now = GetTickCount64();
    bool active = g_loaded && !IsIconic(g_main) && IsWindowVisible(g_main);

    // El panel de Info es transparente al ratón para no bloquear el vídeo.
    // Por ello, cuando está abierto, detectamos aquí un clic fuera de su recuadro
    // (pero no sobre el propio botón Info) y lo cerramos.
    if (g_showInfo && active && (GetAsyncKeyState(VK_LBUTTON) & 1)) {
        POINT cp{}; GetCursorPos(&cp);
        RECT ir{}; if (g_info && IsWindowVisible(g_info)) GetWindowRect(g_info, &ir);
        bool insideInfo = PtInRect(&ir, cp) != FALSE;
        bool insideInfoButton = false;
        if (g_ovl && IsWindowVisible(g_ovl)) {
            RECT orc{}; GetWindowRect(g_ovl, &orc);
            POINT lp = cp; ScreenToClient(g_ovl, &lp);
            RECT cr{}; GetClientRect(g_ovl, &cr);
            Layout ol = GetLayout(cr.right, std::min((int)cr.bottom, S(130)));
            insideInfoButton = In(ol.info, lp.x, lp.y);
        }
        if (!insideInfo && !insideInfoButton) CloseInfo();
    }

    bool wantBar = active && (g_pause || g_eof || g_drag != H_NONE || g_menuOpen || now - g_lastMove < 2500);
    if (wantBar) {
        POINT p; GetCursorPos(&p);
        for (HWND w : { g_ovl, g_prev, g_next, g_closefs }) {
            RECT r; if (w && IsWindowVisible(w) && GetWindowRect(w, &r) && PtInRect(&r, p)) g_lastMove = now;
        }
    }
    bool hasPrev = g_idx > 0, hasNext = g_idx >= 0 && g_idx + 1 < (int)g_list.size();
    if (!hasPrev && g_hoverNav == -1) g_hoverNav = 0;
    if (!hasNext && g_hoverNav == 1) g_hoverNav = 0;
    SetVis(g_audioVis, active && g_audioOnly, RedrawAudioVis);
    SetVis(g_ovl, wantBar, Redraw);
    SetVis(g_prev, wantBar && hasPrev, RedrawNav);
    SetVis(g_next, wantBar && hasNext, RedrawNav);
    SetVis(g_info, active && g_showInfo, RedrawInfo);
    SetVis(g_closefs, active && g_fs, RedrawCloseFs);
    // El visualizador queda debajo de los controles flotantes, pero por encima del vídeo de mpv.
    EnsureTop(g_audioVis);
    for (HWND w : { g_ovl, g_prev, g_next, g_info, g_closefs }) EnsureTop(w);

    // Red de seguridad: si la barra no está donde debe (se perdió algún aviso de mover/redimensionar), se recoloca todo.
    if (g_ovl && IsWindowVisible(g_ovl)) {
        int x, y, w, h; BarGeom(x, y, w, h);
        RECT r; GetWindowRect(g_ovl, &r);
        if (r.left != x || r.top != y || r.right - r.left != w || r.bottom - r.top != h) RedrawAll();
    }
    if (g_info && IsWindowVisible(g_info)) RedrawInfo();   // datos en tiempo real
}

// ---------- ciclo de vida de las superposiciones ----------
static HWND MakeOvl(int role) {
    HWND w = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_ACCEPTFILES,
        L"PikPlayerOverlay", L"", WS_POPUP, 0, 0, 10, 10, g_main, nullptr, g_inst, nullptr);
    if (w) {
        SetWindowLongPtrW(w, GWLP_USERDATA, role);
        EnableDropOn(w);
    }
    return w;
}
static void CreateOverlays() { g_audioVis = MakeOvl(OV_AUDIOVIS); g_ovl = MakeOvl(OV_BAR); g_prev = MakeOvl(OV_PREV); g_next = MakeOvl(OV_NEXT); g_info = MakeOvl(OV_INFO); g_closefs = MakeOvl(OV_CLOSEFS); }
static void DestroyOverlays() {
    for (HWND* w : { &g_audioVis, &g_ovl, &g_prev, &g_next, &g_info, &g_closefs }) { if (*w && IsWindow(*w)) DestroyWindow(*w); *w = nullptr; }
}
// Tras maximizar/restaurar o cambiar de pantalla completa se recrean limpias: así nunca quedan en un estado roto.
static void ResetOverlays() {
    if (!g_main || g_drag != H_NONE || g_menuOpen) return;
    DestroyOverlays(); CreateOverlays();
    g_lastMove = GetTickCount64(); g_hover = H_NONE; g_hoverNav = 0;
    UpdateOverlayVisibility(); RedrawAll();
}

struct CMenuItem { std::wstring text; int cmd = -1; bool checked = false; bool disabled = false; bool category = false; };
static HWND g_cmenu = nullptr;
static int g_cmenuPage = 0, g_cmenuHover = -1, g_cmenuResult = -1;
static POINT g_cmenuOrigin{};
static double g_cmenuSpeed = 1.0;
static bool g_cmenuSpeedDrag = false;

static std::vector<CMenuItem> BuildCustomMenuItems() {
    std::vector<CMenuItem> a;
    if (g_cmenuPage == 0) {
        a.push_back({Tr(L"Audio"), 0, false, false, true});
        a.push_back({Tr(L"Subtitulos"), 1, false, false, true});
        a.push_back({Tr(L"Velocidad"), 2, false, false, true});
        a.push_back({L"", -2, false, true, false});
        a.push_back({Tr(L"Abrir archivo…"), 5001, false, false, false});
    } else if (g_cmenuPage == 3) {
        a.push_back({Tr(L"←  Volver"), -10, false, false, false});
        a.push_back({L"", 40000, false, false, false}); // slider
    } else {
        a.push_back({Tr(L"←  Volver"), -10, false, false, false});
        int n = atoi(Prop("track-list/count").c_str());
        std::string wanted = (g_cmenuPage == 1) ? Prop("aid") : Prop("sid");
        int count = 0;
        for (int i = 0; i < n; ++i) {
            std::string b = "track-list/" + std::to_string(i) + "/";
            std::string type = Prop((b+"type").c_str());
            if ((g_cmenuPage == 1 && type != "audio") || (g_cmenuPage == 2 && type != "sub")) continue;
            std::wstring label = W(Prop((b+"title").c_str()).c_str());
            std::string lang = Prop((b+"lang").c_str()), id = Prop((b+"id").c_str()), codec = Prop((b+"codec").c_str());
            if (!lang.empty()) label = (label.empty()?L"":label+L" - ") + W(lang.c_str());
            if (label.empty()) label = (g_cmenuPage == 1 ? Tr(L"Pista de audio ") : Tr(L"Subtitulo ")) + W(id.c_str());
            if (!codec.empty()) label += L" (" + W(codec.c_str()) + L")";
            a.push_back({label, (g_cmenuPage == 1 ? 1000 : 2000) + i, wanted == id, false, false});
            ++count;
        }
        if (g_cmenuPage == 2) {
            a.insert(a.begin()+1, {Tr(L"Desactivados"), 3000, wanted == "no", false, false});
            if (!count) a.push_back({L"(sin subtítulos)", -1, false, true, false});
        } else if (!count) a.push_back({Tr(L"(sin pistas de audio)"), -1, false, true, false});
    }
    return a;
}

static void DrawCustomMenu() {
    if (!g_cmenu || !IsWindowVisible(g_cmenu)) return;
    auto items = BuildCustomMenuItems();
    int w = S(350), row = S(38), pad = S(10), h = pad*2 + (int)items.size()*row + (g_cmenuPage == 3 ? row : 0);
    Present(g_cmenu, g_cmenuOrigin.x, g_cmenuOrigin.y, w, h, [&](Graphics& g) {
        GraphicsPath p; RoundRectPath(p, 0.5f, 0.5f, (REAL)w-1, (REAL)h-1, (REAL)S(11));
        SolidBrush bg(Color(220, 18, 18, 21)); g.FillPath(&bg, &p);
        Pen border(Color(65,255,255,255), 1.f); g.DrawPath(&border, &p);
        Color acc = Accent(); SolidBrush white(Color(255,245,245,248)), dim(Color(255,165,165,170)), ab(acc);
        Font f(L"Segoe UI", (REAL)S(12), FontStyleRegular, UnitPixel);
        StringFormat sf; sf.SetLineAlignment(StringAlignmentCenter); sf.SetTrimming(StringTrimmingEllipsisCharacter); sf.SetFormatFlags(StringFormatFlagsNoWrap);
        int y = pad;
        for (size_t i=0;i<items.size();++i) {
            if (g_cmenuPage == 3 && items[i].cmd == 40000) {
                double cur = std::clamp(g_cmenuSpeed, 0.1, 4.0);
                int sy = y + row/2;
                // El valor queda a la izquierda y el carril empieza justo a su derecha.
                // Escala por tramos: 0.1x..1.0x ocupa la mitad izquierda y
                // 1.0x..4.0x la mitad derecha, por lo que 1.0x queda exactamente centrado.
                int lx = S(78), rx = w - S(28);
                Pen rail(Color(255,90,90,96), SR(4.0f)); rail.SetStartCap(LineCapRound); rail.SetEndCap(LineCapRound);
                g.DrawLine(&rail, (REAL)lx, (REAL)sy, (REAL)rx, (REAL)sy);
                double frac = cur <= 1.0
                    ? ((cur - 0.1) / 0.9) * 0.5
                    : 0.5 + ((cur - 1.0) / 3.0) * 0.5;
                frac = std::clamp(frac, 0.0, 1.0);
                int tx = lx + (int)std::lround(frac * (rx-lx));
                Pen active(Accent(), SR(4.0f)); active.SetStartCap(LineCapRound); active.SetEndCap(LineCapRound);
                g.DrawLine(&active, (REAL)lx, (REAL)sy, (REAL)tx, (REAL)sy);
                SolidBrush knob(Accent()); g.FillEllipse(&knob, (REAL)(tx-S(7)), (REAL)(sy-S(7)), (REAL)S(14), (REAL)S(14));
                wchar_t sb[32]; swprintf_s(sb, L"%.1fx", cur);
                Font sfnt(L"Segoe UI", (REAL)S(12), FontStyleRegular, UnitPixel);
                StringFormat ssf; ssf.SetLineAlignment(StringAlignmentCenter); ssf.SetFormatFlags(StringFormatFlagsNoWrap);
                SolidBrush sbw(Color(255,245,245,248));
                g.DrawString(sb, -1, &sfnt, RectF((REAL)S(20), (REAL)(y+S(6)), (REAL)S(52), (REAL)(row-S(4))), &ssf, &sbw);
                y += row*2;
                continue;
            }
            auto &it=items[i]; RECT r{S(7),y,w-S(7),y+row};
            if (it.disabled && it.cmd==-2) { y+=row; continue; }
            if ((int)i==g_cmenuHover && !it.disabled) { SolidBrush hot(Color(42,255,255,255)); GraphicsPath hp; RoundRectPath(hp,(REAL)S(4),(REAL)y,(REAL)(w-S(8)),(REAL)row,(REAL)S(7)); g.FillPath(&hot,&hp); }
            if (it.category) {
                g.DrawString(it.text.c_str(),-1,&f,RectF((REAL)S(20),(REAL)y,(REAL)(w-S(60)),(REAL)row),&sf,&white);
                Pen ar(Color(255,190,190,195),SR(1.7f)); ar.SetStartCap(LineCapRound); ar.SetEndCap(LineCapRound); g.DrawLine(&ar,(REAL)(w-S(28)),(REAL)(y+row/2-S(5)),(REAL)(w-S(23)),(REAL)(y+row/2)); g.DrawLine(&ar,(REAL)(w-S(23)),(REAL)(y+row/2),(REAL)(w-S(28)),(REAL)(y+row/2+S(5)));
            } else {
                Color c=it.disabled?Color(255,110,110,115):Color(255,245,245,248); SolidBrush b(c);
                g.DrawString(it.text.c_str(),-1,&f,RectF((REAL)S(20),(REAL)y,(REAL)(w-S(40)),(REAL)row),&sf,&b);
                if (it.checked) { Pen ck(Color(255,235,235,240),SR(2.0f)); ck.SetStartCap(LineCapRound); ck.SetEndCap(LineCapRound); int x=S(12), cy=y+row/2; g.DrawLine(&ck,(REAL)x,(REAL)cy,(REAL)(x+S(5)),(REAL)(cy+S(5))); g.DrawLine(&ck,(REAL)(x+S(5)),(REAL)(cy+S(5)),(REAL)(x+S(13)),(REAL)(cy-S(6))); }
            }
            y+=row;
        }
    });
}
static int CustomMenuHit(int y) {
    auto items=BuildCustomMenuItems(); int row=S(38), pad=S(10);
    if (g_cmenuPage == 3) {
        if (y >= pad && y < pad + row) return 0;
        if (y >= pad + row && y < pad + row*3) return 1;
        return -1;
    }
    int i=(y-pad)/row; return (i>=0 && i<(int)items.size())?i:-1;
}
static void UpdateMenuSpeedFromX(HWND h, int x) {
    RECT wr; GetClientRect(h, &wr);
    int lx=S(28), rx=wr.right-S(28);
    if (rx <= lx) return;
    double f=std::clamp((double)(x-lx)/(double)(rx-lx),0.0,1.0);
    double v = f <= 0.5
        ? 0.1 + (f / 0.5) * 0.9
        : 1.0 + ((f - 0.5) / 0.5) * 3.0;
    g_cmenuSpeed=std::round(v*10.0)/10.0;
    g_cmenuSpeed=std::clamp(g_cmenuSpeed,0.1,4.0);
    SetSpeed(g_cmenuSpeed);
    DrawCustomMenu();
}
static void CloseCustomMenu() { if(g_cmenu){ DestroyWindow(g_cmenu); g_cmenu=nullptr; } }
static LRESULT CALLBACK CustomMenuProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch(m) {
    case WM_MOUSEMOVE: {
        int i=CustomMenuHit(GET_Y_LPARAM(lp));
        if (g_cmenuSpeedDrag) { UpdateMenuSpeedFromX(h, GET_X_LPARAM(lp)); return 0; }
        if(i!=g_cmenuHover){g_cmenuHover=i;DrawCustomMenu();} return 0;
    }
    case WM_LBUTTONDOWN: {
        if (g_cmenuPage == 3 && CustomMenuHit(GET_Y_LPARAM(lp)) == 1) {
            g_cmenuSpeedDrag = true; SetCapture(h); UpdateMenuSpeedFromX(h, GET_X_LPARAM(lp)); return 0;
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (g_cmenuSpeedDrag) { g_cmenuSpeedDrag=false; ReleaseCapture(); return 0; }
        int i=CustomMenuHit(GET_Y_LPARAM(lp)); auto items=BuildCustomMenuItems(); if(i<0||i>=(int)items.size()) return 0; auto it=items[i]; if(it.disabled) return 0;
        if(it.category){g_cmenuPage=it.cmd+1; g_cmenuHover=-1; if(g_cmenuPage==3){g_cmenuSpeed=1.0; if(g_mpv) mpv_get_property(g_mpv,"speed",MPV_FORMAT_DOUBLE,&g_cmenuSpeed); g_cmenuSpeed=std::clamp(std::round(g_cmenuSpeed*10.0)/10.0,0.1,4.0);} RECT wr; GetWindowRect(h,&wr); int w=S(350), row=S(38), pad=S(10), hh=pad*2+(int)BuildCustomMenuItems().size()*row+(g_cmenuPage==3?row:0); g_cmenuOrigin={wr.left,wr.top}; SetWindowPos(h,HWND_TOP,g_cmenuOrigin.x,g_cmenuOrigin.y,w,hh,SWP_NOACTIVATE); DrawCustomMenu(); return 0;}
        if(it.cmd==-10){g_cmenuPage=0;g_cmenuHover=-1;RECT wr;GetWindowRect(h,&wr);g_cmenuOrigin={wr.left,wr.top};int w=S(350),row=S(38),pad=S(10),hh=pad*2+(int)BuildCustomMenuItems().size()*row;SetWindowPos(h,HWND_TOP,wr.left,wr.top,w,hh,SWP_NOACTIVATE);DrawCustomMenu();return 0;}
        g_cmenuResult=it.cmd; CloseCustomMenu(); return 0; }
    case WM_ACTIVATE: if(LOWORD(wp)==WA_INACTIVE){ if(g_cmenuResult < 0) g_cmenuResult=-1; CloseCustomMenu(); } return 0;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(h,m,wp,lp);
}
static int ShowCustomMenu(int sx,int sy) {
    g_cmenuPage=0;g_cmenuHover=-1;g_cmenuResult=-1;g_menuOpen=true;
    auto items=BuildCustomMenuItems(); int w=S(350),row=S(38),pad=S(10),h=pad*2+(int)items.size()*row;
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA,0,&wa,0); int x=sx-w, y=sy-h; x=(int)std::max((LONG)(wa.left+S(4)),std::min((LONG)x,(LONG)(wa.right-w-S(4)))); y=(int)std::max((LONG)(wa.top+S(4)),std::min((LONG)y,(LONG)(wa.bottom-h-S(4)))); g_cmenuOrigin={x,y};
    g_cmenu=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW,L"PikPlayerMenu",L"",WS_POPUP,x,y,w,h,g_main,nullptr,g_inst,nullptr);
    if(!g_cmenu){g_menuOpen=false;return -1;} SetWindowPos(g_cmenu,HWND_TOP,x,y,w,h,SWP_SHOWWINDOW); SetForegroundWindow(g_cmenu); DrawCustomMenu();
    MSG msg; while(g_cmenu && GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    g_menuOpen=false; g_lastMove=GetTickCount64(); SetForegroundWindow(g_main); return g_cmenuResult;
}
static void ShowTracksMenu(int sx, int sy) {
    int cmd=ShowCustomMenu(sx,sy);
    auto selectTrack = [&](const char* prop, int i) {
        std::string id = Prop(("track-list/"+std::to_string(i)+"/id").c_str());
        if (id.empty() || !g_mpv) return;

        // libmpv's track properties are the authoritative runtime API.  Do not
        // use a command-string as the primary path here: the menu is already
        // running on the UI thread and a failed command can otherwise look like
        // a successful click while the active track remains unchanged.
        int r = mpv_set_property_string(g_mpv, prop, id.c_str());

        // Some older libmpv builds can reject a runtime property write while a
        // track list is being rebuilt. Retry briefly, then use the native `set`
        // command as a final fallback.
        for (int tries = 0; r < 0 && tries < 20; ++tries) {
            Sleep(10);
            r = mpv_set_property_string(g_mpv, prop, id.c_str());
        }
        if (r < 0) {
            const char* a[] = { "set", prop, id.c_str(), nullptr };
            r = mpv_command(g_mpv, a);
        }

        // Subtitle rendering is independent of selecting the sid property.
        // Explicitly turn it back on after every subtitle selection.
        if (!strcmp(prop, "sid")) {
            mpv_set_property_string(g_mpv, "sub-visibility", "yes");
            mpv_set_property_string(g_mpv, "secondary-sid", "no");
        }

        // Force a redraw only after the request has been submitted.
        RedrawAll();
    };
    if(cmd>=1000&&cmd<2000) selectTrack("aid",cmd-1000);
    else if(cmd>=2000&&cmd<3000) selectTrack("sid",cmd-2000);
    else if(cmd==3000) { SetProp("sid","no"); RedrawAll(); }
    else if(cmd==5001) OpenDialog();
}
static void SeekToX(int x, bool commit) {
    RECT cr; GetClientRect(g_main, &cr);
    Layout L = GetLayout(cr.right, std::min((int)cr.bottom, S(130)));
    double f = std::clamp((double)(x - L.seek.left) / std::max(1, (int)(L.seek.right - L.seek.left)), 0.0, 1.0);
    g_pos = f * g_dur;
    // Durante el arrastre solo mueve el thumb visual. Un único seek al soltar.
    // absolute+exact en todos los formatos: el clic va al tiempo elegido, no al keyframe anterior.
    if (commit && g_dur > 0.0) {
        char b[96];
        sprintf_s(b, "seek %.3f absolute+exact", g_pos);
        Cmd(b);
    }
    Redraw();
}

static void VolToX(int x) {
    RECT cr; GetClientRect(g_main, &cr);
    Layout L = GetLayout(cr.right, std::min((int)cr.bottom, S(130)));
    double f = std::clamp((double)(x - L.vol.left) / std::max(1, (int)(L.vol.right - L.vol.left)), 0.0, 1.0);
    char b[64]; sprintf_s(b, "%.0f", f * 100); SetProp("volume", b); SetProp("mute", "no"); g_vol = f * 100; g_mute = false; Redraw();
}

static void CloseCustomMenu();
static void ShowTracksMenu(int sx, int sy);
static LRESULT CALLBACK OvlProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    int role = (int)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (role == OV_PREV || role == OV_NEXT) {          // flechas laterales
        int dir = role == OV_PREV ? -1 : 1;
        switch (m) {
        case WM_MOUSEMOVE: {
            g_lastMove = GetTickCount64();
            TRACKMOUSEEVENT t{ sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent(&t);
            if (g_hoverNav != dir) { g_hoverNav = dir; RedrawNav(); }
            SetCursor(LoadCursor(nullptr, IDC_HAND)); return 0; }
        case WM_MOUSELEAVE: if (g_hoverNav == dir) { g_hoverNav = 0; RedrawNav(); } return 0;
        case WM_LBUTTONDOWN: BringFront(); CloseInfo(); Nav(dir); return 0;
        case WM_MOUSEWHEEL: AddVol(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 5 : -5); return 0;
        case WM_DROPFILES: DoDrop((HDROP)wp); return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        }
        return DefWindowProcW(h, m, wp, lp);
    }
    if (role == OV_AUDIOVIS) {
        switch (m) {
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_DROPFILES: DoDrop((HDROP)wp); return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        }
        return DefWindowProcW(h, m, wp, lp);
    }
    if (role == OV_CLOSEFS) {
        switch (m) {
        case WM_MOUSEMOVE: {
            g_lastMove = GetTickCount64(); SetCursor(LoadCursor(nullptr, IDC_HAND));
            RedrawCloseFs(); return 0; }
        case WM_MOUSELEAVE: RedrawCloseFs(); return 0;
        case WM_LBUTTONDOWN:
            // En pantalla completa la X debe cerrar el reproductor por completo,
            // no limitarse a salir del modo pantalla completa.
            PostMessageW(g_main, WM_CLOSE, 0, 0);
            return 0;
        case WM_DROPFILES: DoDrop((HDROP)wp); return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        }
        return DefWindowProcW(h,m,wp,lp);
    }
    if (role == OV_INFO) {                              // panel de datos: no captura el ratón
        if (m == WM_NCHITTEST) return HTTRANSPARENT;
        if (m == WM_DROPFILES) { DoDrop((HDROP)wp); return 0; }
        if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (m == WM_ERASEBKGND) return 1;
        return DefWindowProcW(h, m, wp, lp);
    }
    RECT cr; GetClientRect(g_main, &cr);
    Layout L = GetLayout(cr.right, std::min((int)cr.bottom, S(130)));
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    switch (m) {
    case WM_MOUSEMOVE: {
        g_lastMove = GetTickCount64();
        TRACKMOUSEEVENT t{ sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent(&t);
        Hit nh = HitTest(L, x, y);
        if (g_drag == H_SEEK) SeekToX(x, false); else if (g_drag == H_VOL) VolToX(x);
        if (nh != g_hover) { g_hover = nh; Redraw(); }
        SetCursor(LoadCursor(nullptr, nh == H_NONE ? IDC_ARROW : IDC_HAND));
        return 0; }
    case WM_MOUSELEAVE: if (g_hover != H_NONE) { g_hover = H_NONE; Redraw(); } return 0;
    case WM_LBUTTONDOWN: {
        Hit k = HitTest(L, x, y);
        BringFront();
        if (k != H_INFO) CloseInfo();   // cualquier clic que no sea el botón "i" cierra el panel
        if (k == H_SEEK) {
            g_drag = H_SEEK; SetCapture(h);
            g_seekWasPaused = g_pause;
            if (!g_seekWasPaused) SetProp("pause", "yes");
            SeekToX(x, false);
        }
        else if (k == H_VOL) { g_drag = H_VOL; SetCapture(h); VolToX(x); }
        else if (k == H_PLAY) TogglePause();
        else if (k == H_BACK) Seek(-10);
        else if (k == H_FWD) Seek(30);
        else if (k == H_MUTE) Cmd("cycle mute");
        else if (k == H_FS) ToggleFS();
        else if (k == H_INFO) { g_showInfo = !g_showInfo; UpdateOverlayVisibility(); RedrawAll(); }
        else if (k == H_TRACKS) {
            if (g_cmenu) { CloseCustomMenu(); }
            else { POINT p{ L.tracks.right, 0 }; ClientToScreen(h, &p); RECT wr; GetWindowRect(h, &wr); ShowTracksMenu(p.x, wr.top - S(4)); }
        }
        return 0; }
    case WM_LBUTTONUP:
        if (g_drag == H_SEEK) {
            SeekToX(x, true);
            if (!g_seekWasPaused) SetProp("pause", "no");
        }
        if (g_drag != H_NONE) { g_drag = H_NONE; ReleaseCapture(); }
        return 0;
    case WM_MOUSEWHEEL: AddVol(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 5 : -5); return 0;
    case WM_DROPFILES: DoDrop((HDROP)wp); return 0;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(h, m, wp, lp);
}


// ---------- ventana de vídeo (hospeda la ventana de mpv) ----------
// ---------- arrastrar y soltar archivos ----------
static void OpenDroppedPath(const std::wstring& p) {
    if (p.empty()) return;
    // Carpetas: abrir el primer medio que contenga
    DWORD attr = GetFileAttributesW(p.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        std::error_code ec;
        std::vector<std::wstring> found;
        for (fsys::directory_iterator it(p, fsys::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (it->is_regular_file(e2) && IsMediaExt(Lower(it->path().extension().wstring())))
                found.push_back(it->path().wstring());
        }
        if (found.empty()) return;
        std::sort(found.begin(), found.end(), [](const std::wstring& a, const std::wstring& b) {
            return StrCmpLogicalW(a.c_str(), b.c_str()) < 0;
        });
        LoadPath(found[0]);
        return;
    }
    LoadPath(p);
}

static void DoDrop(HDROP d) {
    if (!d) return;
    UINT n = DragQueryFileW(d, 0xFFFFFFFF, nullptr, 0);
    if (n > 0) {
        UINT len = DragQueryFileW(d, 0, nullptr, 0);
        if (len > 0) {
            std::wstring p(len + 1, L'\0');
            DragQueryFileW(d, 0, &p[0], len + 1);
            p.resize(wcslen(p.c_str()));
            if (!p.empty()) OpenDroppedPath(p);
        }
    }
    DragFinish(d);
}

// OLE IDropTarget: más fiable que WM_DROPFILES (y funciona con procesos elevados vía mensaje filtrado)
struct FileDropTarget : IDropTarget {
    LONG ref{ 1 };
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropTarget) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref);
        if (r == 0) delete this;
        return (ULONG)r;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject*, DWORD, POINTL, DWORD* effect) override {
        if (effect) *effect = DROPEFFECT_COPY;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override {
        if (effect) *effect = DROPEFFECT_COPY;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        if (effect) *effect = DROPEFFECT_COPY;
        if (!data) return E_INVALIDARG;
        FORMATETC fmt{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM med{};
        if (FAILED(data->GetData(&fmt, &med))) return S_OK;
        HDROP h = (HDROP)GlobalLock(med.hGlobal);
        if (h) {
            UINT len = DragQueryFileW(h, 0, nullptr, 0);
            if (len > 0) {
                std::wstring p(len + 1, L'\0');
                DragQueryFileW(h, 0, &p[0], len + 1);
                p.resize(wcslen(p.c_str()));
                GlobalUnlock(med.hGlobal);
                ReleaseStgMedium(&med);
                if (!p.empty()) OpenDroppedPath(p);
                return S_OK;
            }
            GlobalUnlock(med.hGlobal);
        }
        ReleaseStgMedium(&med);
        return S_OK;
    }
};

static FileDropTarget* g_dropTarget = nullptr;

static void EnableDropOn(HWND w) {
    if (!w) return;
    DragAcceptFiles(w, TRUE);
    // Permitir drop desde Explorer (integridad media) aunque el proceso esté elevado
    ChangeWindowMessageFilterEx(w, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(w, WM_COPYGLOBALDATA, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(w, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    if (g_dropTarget)
        RegisterDragDrop(w, g_dropTarget);
}

// Ventana embebida de libmpv: subclasarla para WM_DROPFILES + OLE
static WNDPROC g_mpvDropOld = nullptr;
static HWND g_mpvDropHwnd = nullptr;
static LRESULT CALLBACK MpvDropProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_DROPFILES) { DoDrop((HDROP)wp); return 0; }
    if (m == WM_NCDESTROY) {
        if (g_mpvDropHwnd == h) {
            RevokeDragDrop(h);
            if (g_mpvDropOld) SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)g_mpvDropOld);
            g_mpvDropOld = nullptr; g_mpvDropHwnd = nullptr;
        }
    }
    return CallWindowProcW(g_mpvDropOld ? g_mpvDropOld : DefWindowProcW, h, m, wp, lp);
}
static void EnsureMpvDropTarget() {
    if (!g_video) return;
    HWND c = GetWindow(g_video, GW_CHILD);
    if (!c) return;
    if (c == g_mpvDropHwnd && g_mpvDropOld) {
        DragAcceptFiles(c, TRUE);
        return;
    }
    if (g_mpvDropHwnd && IsWindow(g_mpvDropHwnd)) {
        RevokeDragDrop(g_mpvDropHwnd);
        if (g_mpvDropOld) SetWindowLongPtrW(g_mpvDropHwnd, GWLP_WNDPROC, (LONG_PTR)g_mpvDropOld);
        g_mpvDropOld = nullptr; g_mpvDropHwnd = nullptr;
    }
    g_mpvDropOld = (WNDPROC)SetWindowLongPtrW(c, GWLP_WNDPROC, (LONG_PTR)MpvDropProc);
    g_mpvDropHwnd = c;
    EnableDropOn(c);
}

static LRESULT CALLBACK VideoProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_DROPFILES: DoDrop((HDROP)wp); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT r; GetClientRect(h, &r);
        FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        if (!g_loaded || !g_status.empty()) {
            std::wstring hint = Tr(L"Arrastra un video aqui o pulsa Ctrl+O"); const wchar_t* txt = g_status.empty() ? hint.c_str() : g_status.c_str();
            HFONT f = CreateFontW(-S(18), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            HGDIOBJ o = SelectObject(dc, f); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(170, 170, 170));
            DrawTextW(dc, txt, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, o); DeleteObject(f);
        }
        EndPaint(h, &ps); return 0; }
    }
    return DefWindowProcW(h, m, wp, lp);
}

// ---------- ventana principal ----------
static void LayoutChildren() {
    RECT r; GetClientRect(g_main, &r);
    SetWindowPos(g_video, nullptr, 0, 0, r.right, r.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
    if (HWND c = GetWindow(g_video, GW_CHILD)) SetWindowPos(c, nullptr, 0, 0, r.right, r.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
    EnsureMpvDropTarget();
    RedrawAll();
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_MPV: HandleMpvEvents(); return 0;
    case WM_USMDONE: OnUsmDone((std::pair<unsigned, UsmResult>*)lp); return 0;
    case WM_OPENARG: { std::wstring* s = (std::wstring*)lp; LoadPath(*s); delete s; return 0; }
    case WM_SIZE:
        LayoutChildren();
        // Al maximizar/restaurar/volver de minimizar se recrean las superposiciones (ver ResetOverlays)
        if (wp != SIZE_MINIMIZED && (int)wp != g_sizeType) PostMessageW(h, WM_RESETOVL, 0, 0);
        g_sizeType = (int)wp;
        return 0;
    case WM_RESETOVL: ResetOverlays(); return 0;
    case WM_EXITSIZEMOVE: case WM_DISPLAYCHANGE: PostMessageW(h, WM_RESETOVL, 0, 0); break;
    case WM_MOVE: case WM_WINDOWPOSCHANGED: RedrawAll(); break;
    case WM_DPICHANGED: {
        g_dpi = HIWORD(wp); RECT* r = (RECT*)lp;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0; }
    case WM_GETMINMAXINFO: { auto* i = (MINMAXINFO*)lp; i->ptMinTrackSize = { S(560), S(300) }; return 0; }
    case WM_DROPFILES: DoDrop((HDROP)wp); return 0;
    case WM_SETFOCUS: return 0;
    case WM_KEYDOWN: {
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        switch (wp) {
        case VK_SPACE: DoAction("pp-toggle", ""); break;
        case VK_LEFT: DoAction("pp-seek", "-5"); break;
        case VK_RIGHT: DoAction("pp-seek", "5"); break;
        case VK_UP: DoAction("pp-vol", "5"); break;
        case VK_DOWN: DoAction("pp-vol", "-5"); break;
        case VK_RETURN: case 'F': DoAction("pp-fs", ""); break;
        case VK_ESCAPE: DoAction("pp-esc", ""); break;
        case 'M': DoAction("pp-mute", ""); break;
        case 'I': DoAction("pp-info", ""); break;
        case 'N': case VK_NEXT: DoAction("pp-next", ""); break;
        case 'P': case VK_PRIOR: DoAction("pp-prev", ""); break;
        case 'O': if (ctrl) DoAction("pp-open", ""); break;
        }
        g_lastMove = GetTickCount64(); return 0; }
    case WM_TIMER:
        if (wp == T_CLICK) { KillTimer(h, T_CLICK); TogglePause(); }
        else if (wp == T_UI) UpdateOverlayVisibility();
        else if (wp == T_VIS && g_audioOnly && g_loaded) {
            if (!g_pause) g_visPhase += 0.18;
            RedrawAudioVis();
        }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: {
        DWORD v = (DWORD)g_vol;
        RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\PikPlayer", L"Volume", REG_DWORD, &v, sizeof v);
        if (g_mpvDropHwnd && IsWindow(g_mpvDropHwnd)) RevokeDragDrop(g_mpvDropHwnd);
        if (g_video) RevokeDragDrop(g_video);
        RevokeDragDrop(h);
        if (g_dropTarget) { g_dropTarget->Release(); g_dropTarget = nullptr; }
        KillTimer(h, T_UI); KillTimer(h, T_VIS); StopAudioCapture(); DestroyOverlays();
        if (g_mpv) { mpv_handle* t = g_mpv; g_mpv = nullptr; mpv_terminate_destroy(t); }
        std::error_code ec; fsys::remove_all(g_tmpRoot, ec);
        PostQuitMessage(0); return 0; }
    }
    return DefWindowProcW(h, m, wp, lp);
}

static HICON MakeBlankIcon() {
    BYTE andMask[32], xorMask[32];
    memset(andMask, 0xFF, sizeof andMask); memset(xorMask, 0, sizeof xorMask);
    return CreateIcon(g_inst, 16, 16, 1, 1, andMask, xorMask);
}


// ---------- desinstalación (PikPlayer.exe /uninstall) ----------
static bool IsAdmin() {
    BOOL a = FALSE; PSID g = nullptr; SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &g)) { CheckTokenMembership(nullptr, g, &a); FreeSid(g); }
    return a != FALSE;
}
static int RunUninstall() {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (!IsAdmin()) { ShellExecuteW(nullptr, L"runas", exe, L"/uninstall", nullptr, SW_SHOW); return 0; }
    if (MessageBoxW(nullptr, L"¿Quieres desinstalar PikPlayer?", L"PikPlayer", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
    std::wstring dir = fsys::path(exe).parent_path().wstring();
    inst::Unregister();
    wchar_t la[MAX_PATH]; std::error_code ec;
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) fsys::remove_all(fsys::path(la) / L"PikPlayer", ec);
    MessageBoxW(nullptr, L"PikPlayer se ha desinstalado.", L"PikPlayer", MB_ICONINFORMATION);
    std::wstring cmd = L"cmd.exe /c ping -n 4 127.0.0.1 >nul & rmdir /s /q \"" + dir + L"\"";
    STARTUPINFOW si{ sizeof si }; PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    return 0;
}

// ---------- modo miniatura headless: PikPlayer.exe /thumb <entrada> <salida.png> [cx] ----------
// Usado por PikPlayerThumbnail.dll. Sin UI, extrae un fotograma con libmpv (y demux USM si aplica).
static int RunThumbMode(int argc, LPWSTR* argv) {
    // argv[0]=exe, argv[1]=/thumb, argv[2]=input, argv[3]=output, argv[4]=cx opcional
    if (argc < 4) return 2;
    const std::wstring inPath = argv[2];
    const std::wstring outPath = argv[3];
    int cx = 256;
    if (argc >= 5) {
        int v = _wtoi(argv[4]);
        if (v >= 16 && v <= 1024) cx = v;
    }

    if (!PrepareMpvDll()) return 3;

    // Carpeta temporal para demux USM y posibles auxiliares
    wchar_t tmpBase[MAX_PATH]; GetTempPathW(MAX_PATH, tmpBase);
    std::wstring work = std::wstring(tmpBase) + L"PikThumb-" + std::to_wstring(GetCurrentProcessId());
    std::error_code ec;
    fsys::create_directories(work, ec);

    std::wstring mediaPath = inPath;
    std::string demuxerFmt, demuxerFps;
    std::string audioFiles;
    bool isUsm = false;
    {
        std::wstring ext = Lower(fsys::path(inPath).extension().wstring());
        if (ext == L".usm") isUsm = true;
        else {
            // Detección por firma @SFV (por si el archivo no tiene extensión)
            FILE* f = _wfopen(inPath.c_str(), L"rb");
            if (f) {
                char mag[4] = {};
                if (fread(mag, 1, 4, f) == 4 && memcmp(mag, "@SFV", 4) == 0) isUsm = true;
                fclose(f);
            }
        }
    }
    if (isUsm) {
        UsmResult u = UsmDemux(inPath, work);
        if (!u.ok || u.videoPath.empty()) {
            fsys::remove_all(work, ec);
            return 4;
        }
        mediaPath = u.videoPath;
        demuxerFmt = u.format;
        demuxerFps = u.fps;
        for (auto& a : u.audioPaths) {
            if (!audioFiles.empty()) audioFiles += ";";
            audioFiles += U8(a);
        }
    }

    mpv_handle* mpv = mpv_create();
    if (!mpv) { fsys::remove_all(work, ec); return 5; }

    auto o = [&](const char* k, const char* v) { mpv_set_option_string(mpv, k, v); };
    o("config", "no");
    o("load-scripts", "no");
    o("ytdl", "no");
    o("vo", "null");
    o("ao", "null");
    o("hwdec", "no");
    o("osc", "no");
    o("osd-level", "0");
    o("idle", "yes");
    o("force-window", "no");
    o("keep-open", "yes");
    o("pause", "yes");
    o("mute", "yes");
    o("audio", "no");
    o("sid", "no");
    o("sub-auto", "no");
    // keyframes = seeks rápidos (exact es mucho más lento)
    o("hr-seek", "no");
    o("hr-seek-framedrop", "yes");
    o("demuxer-readahead-secs", "0.5");
    o("demuxer-max-bytes", isUsm ? "48MiB" : "8MiB");
    o("demuxer-max-back-bytes", "2MiB");
    // JPEG es bastante más rápido de codificar que PNG
    o("screenshot-format", "jpeg");
    o("screenshot-jpeg-quality", "70");
    o("screenshot-tag-colorspace", "no");
    // Decodificar a baja resolución (miniatura, no hace falta 4K)
    int decW = (std::max)(160, cx * 2);
    char scale[96];
    sprintf_s(scale, "lavfi=[scale=%d:-2:flags=fast_bilinear]", decW);
    o("vf", scale);
    o("vd-lavc-threads", "2");
    o("vd-lavc-skiploopfilter", "all");

    if (mpv_initialize(mpv) < 0) {
        mpv_terminate_destroy(mpv);
        fsys::remove_all(work, ec);
        return 5;
    }

    if (!demuxerFmt.empty()) {
        mpv_set_property_string(mpv, "demuxer", "lavf");
        mpv_set_property_string(mpv, "demuxer-lavf-format", demuxerFmt.c_str());
        if (!demuxerFps.empty()) {
            std::string o2 = "framerate=" + demuxerFps;
            mpv_set_property_string(mpv, "demuxer-lavf-o", o2.c_str());
        }
        mpv_set_property_string(mpv, "force-seekable", "yes");
    }
    if (!audioFiles.empty())
        mpv_set_property_string(mpv, "audio-files", audioFiles.c_str());

    std::string pathU8 = U8(mediaPath);
    const char* cmd[] = { "loadfile", pathU8.c_str(), "replace", nullptr };
    if (mpv_command(mpv, cmd) < 0) {
        mpv_terminate_destroy(mpv);
        fsys::remove_all(work, ec);
        return 6;
    }

    // Esperar carga (máx. ~4 s; la mayoría lista en <1 s)
    bool loaded = false;
    double duration = 0;
    ULONGLONG t0 = GetTickCount64();
    while (GetTickCount64() - t0 < 4000) {
        mpv_event* e = mpv_wait_event(mpv, 0.03);
        if (e->event_id == MPV_EVENT_NONE) continue;
        if (e->event_id == MPV_EVENT_FILE_LOADED || e->event_id == MPV_EVENT_PLAYBACK_RESTART)
            loaded = true;
        if (e->event_id == MPV_EVENT_END_FILE) {
            auto* ef = (mpv_event_end_file*)e->data;
            if (ef->reason == MPV_END_FILE_REASON_ERROR) {
                mpv_terminate_destroy(mpv);
                fsys::remove_all(work, ec);
                return 6;
            }
        }
        if (loaded) {
            mpv_get_property(mpv, "duration", MPV_FORMAT_DOUBLE, &duration);
            break;
        }
    }
    if (!loaded) {
        mpv_terminate_destroy(mpv);
        fsys::remove_all(work, ec);
        return 7;
    }

    // Puntuar imagen: evita negros/blancos planos de fundidos.
    auto scoreImg = [](const std::wstring& path) -> double {
        Bitmap bmp(path.c_str(), FALSE);
        if (bmp.GetLastStatus() != Ok) return -1.0;
        const int w = (int)bmp.GetWidth(), h = (int)bmp.GetHeight();
        if (w < 2 || h < 2) return -1.0;
        const int stepX = (std::max)(1, w / 24);
        const int stepY = (std::max)(1, h / 24);
        double sum = 0, sum2 = 0;
        int n = 0, nearBlack = 0, nearWhite = 0;
        for (int y = 0; y < h; y += stepY) {
            for (int x = 0; x < w; x += stepX) {
                Color c;
                if (bmp.GetPixel(x, y, &c) != Ok) continue;
                double Y = 0.299 * c.GetR() + 0.587 * c.GetG() + 0.114 * c.GetB();
                sum += Y; sum2 += Y * Y; n++;
                if (Y < 18.0) nearBlack++;
                else if (Y > 237.0) nearWhite++;
            }
        }
        if (n < 8) return -1.0;
        double mean = sum / n;
        double var = sum2 / n - mean * mean;
        if (var < 0) var = 0;
        double flatFrac = (double)(nearBlack + nearWhite) / n;
        double mid = 1.0 - std::fabs(mean - 128.0) / 128.0;
        double score = var * (0.25 + 0.75 * mid) * (1.0 - 0.85 * flatFrac);
        if (flatFrac > 0.92) score *= 0.05;
        return score;
    };

    // Estrategia rápida: 1–2 seeks prioritarios; solo si salen planos, 1–2 más.
    // keyframes + poca espera = mucho más rápido que exact×N.
    std::vector<double> primary, secondary;
    if (duration <= 0.8) {
        primary = { 0.0 };
    } else if (duration <= 4.0) {
        primary = { duration * 0.40 };
        secondary = { duration * 0.70 };
    } else if (duration <= 20.0) {
        primary = { duration * 0.25, duration * 0.55 };
        secondary = { duration * 0.12, duration * 0.75 };
    } else {
        // Intros largas: 5 s y 30 % suelen tener escena real
        double t5 = (std::min)(5.0, duration * 0.2);
        primary = { t5, duration * 0.35 };
        secondary = { duration * 0.15, duration * 0.55 };
    }

    auto clampT = [&](double t) {
        if (t < 0) t = 0;
        if (duration > 0.3 && t > duration - 0.2) t = duration * 0.5;
        return t;
    };

    ULONG_PTR gdipTok = 0;
    GdiplusStartupInput gsi;
    GdiplusStartup(&gdipTok, &gsi, nullptr);

    std::wstring bestPath = work + L"\\best.jpg";
    double bestScore = -1.0;
    bool anyShot = false;

    auto trySeeks = [&](const std::vector<double>& list, double goodEnough) {
        for (double seekTo : list) {
            seekTo = clampT(seekTo);
            char seekCmd[96];
            sprintf_s(seekCmd, "seek %.3f absolute+keyframes", seekTo);
            mpv_command_string(mpv, seekCmd);

            // Espera corta: con keyframes el frame llega rápido
            ULONGLONG tw = GetTickCount64();
            const DWORD maxWait = isUsm ? 700u : 350u;
            while (GetTickCount64() - tw < maxWait) {
                mpv_event* e = mpv_wait_event(mpv, 0.02);
                if (e->event_id == MPV_EVENT_PLAYBACK_RESTART || e->event_id == MPV_EVENT_VIDEO_RECONFIG)
                    break;
                if (e->event_id == MPV_EVENT_NONE && GetTickCount64() - tw > 120)
                    break;
            }

            std::wstring cand = work + L"\\cand.jpg";
            std::string candU8 = U8(cand);
            const char* shot[] = { "screenshot-to-file", candU8.c_str(), "video", nullptr };
            if (mpv_command(mpv, shot) < 0) continue;

            WIN32_FILE_ATTRIBUTE_DATA fad{};
            if (!GetFileAttributesExW(cand.c_str(), GetFileExInfoStandard, &fad) || fad.nFileSizeLow < 32)
                continue;
            anyShot = true;

            double sc = scoreImg(cand);
            if (sc > bestScore) {
                bestScore = sc;
                CopyFileW(cand.c_str(), bestPath.c_str(), FALSE);
                if (sc >= goodEnough) {
                    DeleteFileW(cand.c_str());
                    return true; // suficiente, salir
                }
            }
            DeleteFileW(cand.c_str());
        }
        return false;
    };

    // Umbral "bastante bueno": no hace falta buscar más
    const double kGood = 180.0;
    if (!trySeeks(primary, kGood) && bestScore < 40.0 && !secondary.empty())
        trySeeks(secondary, kGood);

    // Fallback mínimo
    if (!anyShot || bestScore < 0) {
        if (duration > 0.5) {
            char seekCmd[96];
            sprintf_s(seekCmd, "seek %.3f absolute+keyframes", duration * 0.3);
            mpv_command_string(mpv, seekCmd);
            mpv_wait_event(mpv, 0.25);
        }
        std::string outU8 = U8(outPath);
        const char* shot[] = { "screenshot-to-file", outU8.c_str(), "video", nullptr };
        int shotOk = mpv_command(mpv, shot);
        mpv_terminate_destroy(mpv);
        if (gdipTok) GdiplusShutdown(gdipTok);
        fsys::remove_all(work, ec);
        if (shotOk < 0) return 8;
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (!GetFileAttributesExW(outPath.c_str(), GetFileExInfoStandard, &fad) || fad.nFileSizeLow < 32)
            return 8;
        return 0;
    }

    mpv_terminate_destroy(mpv);
    if (gdipTok) GdiplusShutdown(gdipTok);

    DeleteFileW(outPath.c_str());
    if (!MoveFileExW(bestPath.c_str(), outPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        if (!CopyFileW(bestPath.c_str(), outPath.c_str(), FALSE)) {
            fsys::remove_all(work, ec);
            return 8;
        }
    }
    fsys::remove_all(work, ec);

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(outPath.c_str(), GetFileExInfoStandard, &fad) || fad.nFileSizeLow < 32)
        return 8;
    return 0;
}


int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    g_inst = hi;
    // Modos headless antes de inicializar UI
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; i++) {
                if (_wcsicmp(argv[i], L"/uninstall") == 0) {
                    LocalFree(argv);
                    return RunUninstall();
                }
                if (_wcsicmp(argv[i], L"/thumb") == 0) {
                    int rc = RunThumbMode(argc, argv);
                    LocalFree(argv);
                    return rc;
                }
            }
            LocalFree(argv);
        }
    }
    if (wcsstr(GetCommandLineW(), L"/uninstall")) return RunUninstall();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    GdiplusStartupInput gi; GdiplusStartup(&g_gdipToken, &gi, nullptr);

    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    g_tmpRoot = std::wstring(tmp) + L"PikPlayer-" + std::to_wstring(GetCurrentProcessId());

    DWORD vol = 100, sz = sizeof vol;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\PikPlayer", L"Volume", RRF_RT_REG_DWORD, nullptr, &vol, &sz) == ERROR_SUCCESS) g_vol = (double)std::min<DWORD>(vol, 130);

    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    WNDCLASSEXW wc{ sizeof wc };
    wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = black;
    wc.lpszClassName = L"PikPlayerMain"; wc.lpfnWndProc = MainProc; wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1)); wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);
    wc.lpszClassName = L"PikPlayerVideo"; wc.lpfnWndProc = VideoProc; wc.hIcon = nullptr; wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);
    wc.lpszClassName = L"PikPlayerOverlay"; wc.lpfnWndProc = OvlProc; wc.hbrBackground = nullptr; wc.style = 0;
    RegisterClassExW(&wc);
    wc.lpszClassName = L"PikPlayerMenu"; wc.lpfnWndProc = CustomMenuProc; wc.hbrBackground = nullptr; wc.style = 0;
    RegisterClassExW(&wc);

    OleInitialize(nullptr);
    g_dropTarget = new FileDropTarget();

    g_main = CreateWindowExW(WS_EX_ACCEPTFILES, L"PikPlayerMain", L"PikPlayer", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1100, 660, nullptr, nullptr, hi, nullptr);
    g_dpi = GetDpiForWindow(g_main);
    EnableDropOn(g_main);
    // Barra de título solo con el nombre del vídeo: sin icono, en modo oscuro.
    g_blankIcon = MakeBlankIcon();
    SendMessageW(g_main, WM_SETICON, ICON_SMALL, (LPARAM)g_blankIcon);
    BOOL dark = TRUE; DwmSetWindowAttribute(g_main, 20, &dark, sizeof dark); DwmSetWindowAttribute(g_main, 19, &dark, sizeof dark);

    RECT cr; GetClientRect(g_main, &cr);
    g_video = CreateWindowExW(WS_EX_ACCEPTFILES, L"PikPlayerVideo", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                              0, 0, cr.right, cr.bottom, g_main, nullptr, hi, nullptr);
    EnableDropOn(g_video);
    CreateOverlays();

    if (!PrepareMpvDll() || !InitMpv()) {
        MessageBoxW(g_main, L"No se pudo iniciar el motor de reproducción (libmpv).", L"PikPlayer", MB_ICONERROR);
        return 1;
    }
    SetProp("volume", std::to_string((int)g_vol));
    SetWindowTextW(g_main, L"PikPlayer");
    ShowWindow(g_main, SW_SHOWNORMAL); UpdateWindow(g_main);
    SetTimer(g_main, T_UI, 150, nullptr);
    SetTimer(g_main, T_VIS, 16, nullptr);

    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) PostMessageW(g_main, WM_OPENARG, 0, (LPARAM)new std::wstring(argv[1]));
    if (argv) LocalFree(argv);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    GdiplusShutdown(g_gdipToken);
    return (int)msg.wParam;
}
