// setup.cpp - Instalador de PikPlayer. Lleva PikPlayer.exe dentro (recurso PAYLOAD) y lo instala en Archivos de programa.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>
#include "install_common.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

enum { ID_INSTALL = 101, ID_CANCEL, ID_OPEN, ID_CLOSE, ID_DESK, ID_UNINSTALL, ID_DEFAULT };
enum { WM_PROG = WM_APP + 1, WM_DONE = WM_APP + 2 };

static HWND hMain, hTitle, hText, hBar, hInstall, hCancel, hDesk, hDefault, hOpen, hClose, hUninst;
static HFONT fText, fTitle;
static int g_dpi = 96;
static std::wstring g_dir, g_err;
static bool g_wantDesk = true, g_wantDefault = true, g_busy;
static bool g_installed;            // ya hay una instalación (completa o dañada)
static bool g_damaged;              // instalación incompleta: falta el .exe o el registro
static std::wstring g_oldVer;

static std::wstring STr(const wchar_t* es) {
    LANGID l = PRIMARYLANGID(GetUserDefaultUILanguage());
    if (l == LANG_SPANISH) return es;
    struct T { const wchar_t* es; const wchar_t* en; const wchar_t* fr; const wchar_t* de; const wchar_t* it; const wchar_t* pt; const wchar_t* nl; const wchar_t* pl; const wchar_t* ru; const wchar_t* ja; const wchar_t* ko; const wchar_t* zh; const wchar_t* zht; const wchar_t* tr; };
    static const T a[] = {
      {L"Instalador de PikPlayer",L"PikPlayer Installer",L"Installateur PikPlayer",L"PikPlayer-Installer",L"Installazione di PikPlayer",L"Instalador do PikPlayer",L"PikPlayer-installatieprogramma",L"Instalator PikPlayer",L"Установщик PikPlayer",L"PikPlayer インストーラー",L"PikPlayer 설치 프로그램",L"PikPlayer 安装程序",L"PikPlayer 安裝程式",L"PikPlayer Yükleyici"},
      {L"Crear acceso directo en el escritorio",L"Create desktop shortcut",L"Créer un raccourci sur le bureau",L"Desktop-Verknüpfung erstellen",L"Crea collegamento sul desktop",L"Criar atalho no ambiente de trabalho",L"Snelkoppeling op bureaublad maken",L"Utwórz skrót na pulpicie",L"Создать ярлык на рабочем столе",L"デスクトップショートカットを作成",L"바탕 화면 바로 가기 만들기",L"创建桌面快捷方式",L"建立桌面捷徑",L"Masaüstü kısayolu oluştur"},
      {L"Hacer PikPlayer el reproductor predeterminado",L"Make PikPlayer the default player",L"Définir PikPlayer comme lecteur par défaut",L"PikPlayer als Standardplayer festlegen",L"Imposta PikPlayer come lettore predefinito",L"Tornar o PikPlayer o leitor predefinido",L"PikPlayer als standaardspeler instellen",L"Ustaw PikPlayer jako domyślny odtwarzacz",L"Сделать PikPlayer проигрывателем по умолчанию",L"PikPlayerを既定のプレーヤーにする",L"PikPlayer를 기본 플레이어로 설정",L"将 PikPlayer 设为默认播放器",L"將 PikPlayer 設為預設播放器",L"PikPlayer'ı varsayılan oynatıcı yap"},
      {L"Reparar / reinstalar",L"Repair / reinstall",L"Réparer / réinstaller",L"Reparieren / neu installieren",L"Ripara / reinstalla",L"Reparar / reinstalar",L"Herstellen / opnieuw installeren",L"Napraw / zainstaluj ponownie",L"Восстановить / переустановить",L"修復 / 再インストール",L"복구 / 재설치",L"修复 / 重新安装",L"修復 / 重新安裝",L"Onar / yeniden yükle"},
      {L"Desinstalar",L"Uninstall",L"Désinstaller",L"Deinstallieren",L"Disinstalla",L"Desinstalar",L"Verwijderen",L"Odinstaluj",L"Удалить",L"アンインストール",L"제거",L"卸载",L"解除安裝",L"Kaldır"},
      {L"Instalar",L"Install",L"Installer",L"Installieren",L"Installa",L"Instalar",L"Installeren",L"Zainstaluj",L"Установить",L"インストール",L"설치",L"安装",L"安裝",L"Yükle"},
      {L"Cancelar",L"Cancel",L"Annuler",L"Abbrechen",L"Annulla",L"Cancelar",L"Annuleren",L"Anuluj",L"Отмена",L"キャンセル",L"취소",L"取消",L"取消",L"İptal"},
      {L"Abrir PikPlayer",L"Open PikPlayer",L"Ouvrir PikPlayer",L"PikPlayer öffnen",L"Apri PikPlayer",L"Abrir o PikPlayer",L"PikPlayer openen",L"Otwórz PikPlayer",L"Открыть PikPlayer",L"PikPlayerを開く",L"PikPlayer 열기",L"打开 PikPlayer",L"開啟 PikPlayer",L"PikPlayer'ı aç"},
      {L"Cerrar",L"Close",L"Fermer",L"Schließen",L"Chiudi",L"Fechar",L"Sluiten",L"Zamknij",L"Закрыть",L"閉じる",L"닫기",L"关闭",L"關閉",L"Kapat"},
      {L"¿Seguro que quieres desinstalar PikPlayer?",L"Are you sure you want to uninstall PikPlayer?",L"Voulez-vous vraiment désinstaller PikPlayer ?",L"PikPlayer wirklich deinstallieren?",L"Sei sicuro di voler disinstallare PikPlayer?",L"Tem a certeza de que pretende desinstalar o PikPlayer?",L"Weet je zeker dat je PikPlayer wilt verwijderen?",L"Czy na pewno chcesz odinstalować PikPlayer?",L"Вы уверены, что хотите удалить PikPlayer?",L"PikPlayerをアンインストールしますか？",L"PikPlayer를 제거하시겠습니까?",L"确定要卸载 PikPlayer 吗？",L"確定要解除安裝 PikPlayer 嗎？",L"PikPlayer'ı kaldırmak istediğinizden emin misiniz?"}
    };
    for (auto& x:a) if (!wcscmp(es,x.es)) { switch(l) {
      case LANG_ENGLISH:return x.en; case LANG_FRENCH:return x.fr; case LANG_GERMAN:return x.de; case LANG_ITALIAN:return x.it; case LANG_PORTUGUESE:return x.pt; case LANG_DUTCH:return x.nl; case LANG_POLISH:return x.pl; case LANG_RUSSIAN:return x.ru; case LANG_JAPANESE:return x.ja; case LANG_KOREAN:return x.ko; case LANG_CHINESE:return x.zh; case LANG_TURKISH:return x.tr; default:return es; } }
    return es;
}

static int S(int v) { return MulDiv(v, g_dpi, 96); }

static bool MakeLink(const std::wstring& lnk, const std::wstring& target, const std::wstring& dir) {
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return false;
    sl->SetPath(target.c_str()); sl->SetWorkingDirectory(dir.c_str()); sl->SetIconLocation(target.c_str(), 0); sl->SetDescription(L"PikPlayer");
    IPersistFile* pf = nullptr; bool ok = false;
    if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf)))) { ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE)); pf->Release(); }
    sl->Release(); return ok;
}

// ---------- detección de una instalación previa ----------
static bool RegStr(const wchar_t* name, std::wstring& out) {
    wchar_t b[MAX_PATH * 2]; DWORD n = sizeof b;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, inst::kUninstKey, name, RRF_RT_REG_SZ, nullptr, b, &n) != ERROR_SUCCESS) return false;
    out = b; return true;
}
static void DetectInstall() {
    std::wstring loc;
    bool reg = RegStr(L"DisplayName", loc);
    std::wstring where; if (RegStr(L"InstallLocation", where) && !where.empty()) g_dir = where;   // respeta una ruta anterior distinta
    RegStr(L"DisplayVersion", g_oldVer);
    std::error_code ec;
    bool exe = std::filesystem::exists(g_dir + L"\\PikPlayer.exe", ec);
    g_installed = reg || exe;
    g_damaged = g_installed && !(reg && exe);
    if (g_installed) g_wantDesk = std::filesystem::exists(inst::DesktopLink(), ec);   // conserva la elección anterior del acceso directo
}

static void KillRunning() {
    wchar_t cmd[] = L"taskkill.exe /f /im PikPlayer.exe";
    STARTUPINFOW si{ sizeof si }; PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
}
static void ClearUserData() {   // libmpv extraída y temporales de PikPlayer (se regeneran solas al abrir el programa)
    wchar_t la[MAX_PATH]; std::error_code ec;
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) std::filesystem::remove_all(std::filesystem::path(la) / L"PikPlayer" / L"bin", ec);
}

static void UnregisterThumbnailProvider();

static bool DoUninstall() {
    KillRunning();
    UnregisterThumbnailProvider();
    inst::Unregister();
    wchar_t la[MAX_PATH]; std::error_code ec;
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) std::filesystem::remove_all(std::filesystem::path(la) / L"PikPlayer", ec);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    PostMessageW(hMain, WM_PROG, 60, 0);
    // Solo se borra la carpeta entera si se llama PikPlayer (por seguridad); si no, solo el ejecutable.
    if (_wcsicmp(std::filesystem::path(g_dir).filename().c_str(), L"PikPlayer") == 0) std::filesystem::remove_all(g_dir, ec);
    else {
        std::filesystem::remove(g_dir + L"\\PikPlayer.exe", ec);
        std::filesystem::remove(g_dir + L"\\PikPlayerThumbnail.dll", ec);
        std::filesystem::remove(g_dir + L"\\Uninstall.exe", ec);
    }
    if (ec && std::filesystem::exists(g_dir + L"\\PikPlayer.exe")) { g_err = L"No se pudo borrar la carpeta " + g_dir + L".\nCierra PikPlayer y vuelve a intentarlo."; return false; }
    PostMessageW(hMain, WM_PROG, 100, 0);
    return true;
}

static void OpenDefaultApps() {
    // Windows 10/11 protege las asociaciones por usuario y no permite que un
    // instalador las cambie silenciosamente. Al aceptar, abrimos directamente
    // la ficha de PikPlayer en Aplicaciones predeterminadas para que el usuario
    // confirme allí la elección.
    std::wstring uri = L"ms-settings:defaultapps?registeredAppMachine=PikPlayer";
    HINSTANCE r = ShellExecuteW(nullptr, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL);
}

static const wchar_t* kThumbClsid = L"{6D2D3E8A-8F34-4E21-9C47-725A0E1B9364}";
static const wchar_t* kThumbShellEx = L"{E357FCCD-A995-4576-B01F-234630154E96}";

static bool SetRegSz(HKEY root, const std::wstring& sub, const wchar_t* valueName, const std::wstring& value) {
    HKEY h = nullptr; DWORD disp = 0;
    LONG e = RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, &disp);
    if (e != ERROR_SUCCESS) return false;
    e = RegSetValueExW(h, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));
    RegCloseKey(h); return e == ERROR_SUCCESS;
}

// Solo elimina el Thumbnail Provider *propio* de versiones antiguas de PikPlayer.
// No toca los manejadores nativos de Windows ({9DBD2C50-...}) que Register reinstala.
static void UnregisterThumbnailProvider() {
    const std::wstring clsid = L"Software\\Classes\\CLSID\\" + std::wstring(kThumbClsid);
    auto deleteIfOldPik = [](const std::wstring& parent, const wchar_t* shellExGuid) {
        std::wstring path = parent + L"\\ShellEx\\" + shellExGuid;
        HKEY h = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_QUERY_VALUE, &h) != ERROR_SUCCESS)
            return;
        wchar_t buf[128] = {};
        DWORD type = 0, cb = sizeof(buf);
        LONG r = RegQueryValueExW(h, nullptr, nullptr, &type, (LPBYTE)buf, &cb);
        RegCloseKey(h);
        if (r == ERROR_SUCCESS && type == REG_SZ && _wcsicmp(buf, kThumbClsid) == 0)
            RegDeleteTreeW(HKEY_LOCAL_MACHINE, path.c_str());
    };
    for (const wchar_t* ext : inst::kExts) {
        std::wstring prog = inst::ProgFor(ext);
        std::wstring extKey = std::wstring(L"Software\\Classes\\") + ext;
        std::wstring sfaKey = std::wstring(L"Software\\Classes\\SystemFileAssociations\\") + ext;
        std::wstring progKey = std::wstring(L"Software\\Classes\\") + prog;
        deleteIfOldPik(extKey,  kThumbShellEx);
        deleteIfOldPik(sfaKey,  kThumbShellEx);
        deleteIfOldPik(progKey, kThumbShellEx);
        HKEY hProg = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, progKey.c_str(), 0, KEY_SET_VALUE, &hProg) == ERROR_SUCCESS) {
            RegDeleteValueW(hProg, L"ThumbnailHandler");
            RegCloseKey(hProg);
        }
    }
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsid.c_str());
}

// --- Paquete único cifrado embebido (formato PKPK, recurso #101) ---
// Un solo blob con todos los PE: el Setup.exe es independiente y portable.
static uint32_t PayloadSeed() {
    volatile uint32_t a = 0xA7C30000u;
    volatile uint32_t b = 0x0000E91Fu;
    volatile uint32_t c = 0x13579BDFu;
    return (a | b) ^ (c & 0x0F0F0F0Fu);
}

static uint32_t Crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static void CryptInPlace(uint8_t* data, size_t n, uint32_t seed) {
    uint32_t s = seed;
    for (size_t i = 0; i < n; i++) {
        s = s * 1664525u + 1013904223u;
        uint8_t k = (uint8_t)((s >> 16) ^ (uint8_t)(i * 0x9Eu) ^ (uint8_t)(seed >> ((i & 3) * 8)));
        data[i] ^= k;
    }
}

static bool WriteBytesToFile(const std::wstring& dest, const uint8_t* data, size_t sz) {
    // Si el destino está bloqueado (p.ej. DLL cargada por Explorer), renombrar y reintentar
    DeleteFileW(dest.c_str());
    HANDLE f = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        std::wstring bak = dest + L".old";
        DeleteFileW(bak.c_str());
        MoveFileW(dest.c_str(), bak.c_str());
        f = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        MoveFileExW(bak.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }
    DWORD done = 0; const DWORD chunk = 1 << 20; bool ok = true;
    while (done < sz) {
        DWORD n = (DWORD)(std::min)((size_t)chunk, sz - done), w = 0;
        if (!WriteFile(f, data + done, n, &w, nullptr) || w != n) { ok = false; break; }
        done += n;
    }
    CloseHandle(f);
    if (!ok) DeleteFileW(dest.c_str());
    return ok;
}

// Extrae el paquete PKPK (#101 RCDATA) y escribe los ficheros en g_dir.
// Nombres esperados dentro del pack: PikPlayer.exe, PikPlayerThumbnail.dll, Uninstall.exe
static bool ExtractPack(const std::wstring& dir, std::wstring& err) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
    HGLOBAL hg = r ? LoadResource(nullptr, r) : nullptr;
    const BYTE* raw = hg ? (const BYTE*)LockResource(hg) : nullptr;
    DWORD rawSz = r ? SizeofResource(nullptr, r) : 0;
    if (!raw || rawSz < 12) {
        err = L"El instalador está dañado (falta el paquete de instalación).";
        return false;
    }
    if (raw[0] != 'P' || raw[1] != 'K' || raw[2] != 'P' || raw[3] != 'K') {
        err = L"El instalador está dañado (paquete no válido).";
        return false;
    }
    uint32_t ver = 0, count = 0;
    memcpy(&ver, raw + 4, 4);
    memcpy(&count, raw + 8, 4);
    if (ver != 1 || count == 0 || count > 16) {
        err = L"El instalador está dañado (versión de paquete desconocida).";
        return false;
    }

    size_t off = 12;
    const uint32_t seed = PayloadSeed();
    int written = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (off + 2 > rawSz) { err = L"El instalador está dañado (paquete truncado)."; return false; }
        uint16_t nlen = 0;
        memcpy(&nlen, raw + off, 2); off += 2;
        if (nlen == 0 || nlen > 200 || off + nlen + 8 > rawSz) {
            err = L"El instalador está dañado (entrada inválida).";
            return false;
        }
        std::string name((const char*)raw + off, (const char*)raw + off + nlen);
        off += nlen;
        uint32_t sz = 0, expectCrc = 0;
        memcpy(&sz, raw + off, 4); off += 4;
        memcpy(&expectCrc, raw + off, 4); off += 4;
        if (sz == 0 || sz > 256u * 1024u * 1024u || off + sz > rawSz) {
            err = L"El instalador está dañado (tamaño de archivo inválido).";
            return false;
        }
        std::vector<uint8_t> buf(raw + off, raw + off + sz);
        off += sz;
        CryptInPlace(buf.data(), buf.size(), seed);
        if (Crc32(buf.data(), buf.size()) != expectCrc) {
            err = L"El instalador está dañado (integridad: " + std::wstring(name.begin(), name.end()) + L").";
            return false;
        }
        std::wstring dest = dir + L"\\" + std::wstring(name.begin(), name.end());
        if (!WriteBytesToFile(dest, buf.data(), buf.size())) {
            err = L"No se pudo escribir " + dest + L".\nCierra PikPlayer y Explorer si están usando esos archivos.";
            SecureZeroMemory(buf.data(), buf.size());
            return false;
        }
        SecureZeroMemory(buf.data(), buf.size());
        written++;
        PostMessageW(hMain, WM_PROG, (WPARAM)(20 + (written * 60 / (int)count)), 0);
    }
    if (written < 3) {
        err = L"El instalador está incompleto (faltan componentes).";
        return false;
    }
    return true;
}

static bool DoInstall() {
    KillRunning();   // cerrar PikPlayer si está abierto
    UnregisterThumbnailProvider();
    ClearUserData(); // fuerza a extraer de nuevo libmpv (reparación completa)
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);

    if (!ExtractPack(g_dir, g_err)) return false;
    PostMessageW(hMain, WM_PROG, 85, 0);

    std::wstring exe = g_dir + L"\\PikPlayer.exe";
    std::wstring dll = g_dir + L"\\PikPlayerThumbnail.dll";
    std::wstring uni = g_dir + L"\\Uninstall.exe";

    WIN32_FILE_ATTRIBUTE_DATA fa{};
    DWORD sizeKB = 0;
    auto addSize = [&](const std::wstring& p) {
        if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa))
            sizeKB += fa.nFileSizeLow / 1024;
    };
    addSize(exe); addSize(dll); addSize(uni);

    inst::Register(g_dir, sizeKB);
    PostMessageW(hMain, WM_PROG, 95, 0);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MakeLink(inst::StartMenuLink(), exe, g_dir);
    if (g_wantDesk) MakeLink(inst::DesktopLink(), exe, g_dir); else DeleteFileW(inst::DesktopLink().c_str());
    CoUninitialize();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSH, nullptr, nullptr);
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_IDLIST | SHCNF_FLUSH, nullptr, nullptr);
    if (g_wantDefault) OpenDefaultApps();
    PostMessageW(hMain, WM_PROG, 100, 0);
    return true;
}


static HWND Mk(const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int w, int h, int id, HFONT font) {
    HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), hMain, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE); return c;
}

static void PaintInstaller(HWND h, HDC dc) {
    RECT r{}; GetClientRect(h, &r);
    // Clean modern dark header with a small equalizer motif matching the audio visualizer.
    HBRUSH bg = CreateSolidBrush(RGB(245, 247, 250));
    FillRect(dc, &r, bg); DeleteObject(bg);
    RECT head = r; head.bottom = S(178);
    HBRUSH hb = CreateSolidBrush(RGB(18, 24, 38));
    FillRect(dc, &head, hb); DeleteObject(hb);

    // Accent line.
    RECT accent{0, S(174), r.right, S(178)};
    HBRUSH ab = CreateSolidBrush(RGB(74, 190, 255));
    FillRect(dc, &accent, ab); DeleteObject(ab);

    // Decorative equalizer bars on the right.
    const int base = S(158), bw = S(5), gap = S(4);
    const int heights[] = {18, 30, 13, 38, 23, 46, 28, 17, 35, 25};
    HBRUSH eb = CreateSolidBrush(RGB(74, 190, 255));
    for (int i = 0; i < (int)(sizeof(heights)/sizeof(heights[0])); ++i) {
        RECT b{S(405) + i * (bw + gap), base - S(heights[i]), S(405) + i * (bw + gap) + bw, base};
        FillRect(dc, &b, eb);
    }
    DeleteObject(eb);
}

static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: {
        hMain = h; g_dpi = GetDpiForWindow(h);
        fText = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        fTitle = CreateFontW(-S(24), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        hTitle = Mk(L"STATIC", L"PikPlayer", 0, 24, 20, 452, 36, 0, fTitle);
        std::wstring t;
        if (!g_installed) t = L"Se instalará PikPlayer en:\r\n" + g_dir + L"\r\n\r\nIncluye todo lo necesario para funcionar: no hace falta descargar nada más.";
        else if (g_damaged) t = L"Se ha detectado una instalación incompleta o dañada de PikPlayer en:\r\n" + g_dir + L"\r\n\r\n¿Qué quieres hacer?";
        else t = L"PikPlayer ya está instalado" + (g_oldVer.empty() ? std::wstring() : L" (versión " + g_oldVer + L")") + L" en:\r\n" + g_dir + L"\r\n\r\n¿Qué quieres hacer?";
        hText = Mk(L"STATIC", t.c_str(), 0, 24, 72, 452, 100, 0, fText);
        hDesk = Mk(L"BUTTON", STr(L"Crear acceso directo en el escritorio").c_str(), BS_AUTOCHECKBOX | WS_TABSTOP, 24, 192, 452, 24, ID_DESK, fText);
        SendMessageW(hDesk, BM_SETCHECK, g_wantDesk ? BST_CHECKED : BST_UNCHECKED, 0);
        hDefault = Mk(L"BUTTON", STr(L"Hacer PikPlayer el reproductor predeterminado").c_str(), BS_AUTOCHECKBOX | WS_TABSTOP, 24, 218, 452, 24, ID_DEFAULT, fText);
        SendMessageW(hDefault, BM_SETCHECK, g_wantDefault ? BST_CHECKED : BST_UNCHECKED, 0);
        hBar = Mk(PROGRESS_CLASSW, L"", PBS_SMOOTH, 24, 252, 452, 16, 0, fText); ShowWindow(hBar, SW_HIDE);
        SendMessageW(hBar, PBM_SETRANGE32, 0, 100);
        if (g_installed) {   // Reparar/reinstalar | Desinstalar | Cancelar
            hInstall = Mk(L"BUTTON", STr(L"Reparar / reinstalar").c_str(), BS_DEFPUSHBUTTON | WS_TABSTOP, 140, 294, 140, 32, ID_INSTALL, fText);
            hUninst  = Mk(L"BUTTON", STr(L"Desinstalar").c_str(), BS_PUSHBUTTON | WS_TABSTOP, 288, 294, 92, 32, ID_UNINSTALL, fText);
        } else {
            hInstall = Mk(L"BUTTON", STr(L"Instalar").c_str(), BS_DEFPUSHBUTTON | WS_TABSTOP, 280, 294, 96, 32, ID_INSTALL, fText);
            hUninst = nullptr;
        }
        hCancel = Mk(L"BUTTON", STr(L"Cancelar").c_str(), BS_PUSHBUTTON | WS_TABSTOP, 384, 294, 92, 32, ID_CANCEL, fText);
        // Misma fila, con hueco claro entre ambos (antes se solapaban 276+124 y 384)
        hOpen = Mk(L"BUTTON", STr(L"Abrir PikPlayer").c_str(), BS_DEFPUSHBUTTON | WS_TABSTOP, 232, 294, 140, 32, ID_OPEN, fText); ShowWindow(hOpen, SW_HIDE);
        hClose = Mk(L"BUTTON", STr(L"Cerrar").c_str(), BS_PUSHBUTTON | WS_TABSTOP, 384, 294, 92, 32, ID_CLOSE, fText); ShowWindow(hClose, SW_HIDE);
        return 0; }
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(h, &ps);
        PaintInstaller(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        HWND ctl = (HWND)lp;
        SetBkMode(dc, TRANSPARENT);
        if (ctl == hTitle || ctl == hText) {
            SetTextColor(dc, RGB(242, 246, 252));
            static HBRUSH headerBrush = CreateSolidBrush(RGB(18, 24, 38));
            return (LRESULT)headerBrush;
        }
        SetTextColor(dc, RGB(35, 42, 55));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_INSTALL:
            g_busy = true;
            g_wantDesk = SendMessageW(hDesk, BM_GETCHECK, 0, 0) == BST_CHECKED;
            g_wantDefault = SendMessageW(hDefault, BM_GETCHECK, 0, 0) == BST_CHECKED;
            EnableWindow(hInstall, FALSE); EnableWindow(hCancel, FALSE); EnableWindow(hDesk, FALSE); EnableWindow(hDefault, FALSE); if (hUninst) EnableWindow(hUninst, FALSE);
            SetWindowTextW(hText, g_installed ? L"Reparando…" : L"Instalando…"); ShowWindow(hBar, SW_SHOW);
            std::thread([] { bool ok = DoInstall(); PostMessageW(hMain, WM_DONE, ok, 0); }).detach();
            break;
        case ID_UNINSTALL:
            if (MessageBoxW(h, STr(L"¿Seguro que quieres desinstalar PikPlayer?").c_str(), L"PikPlayer", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            g_busy = true;
            EnableWindow(hInstall, FALSE); EnableWindow(hCancel, FALSE); EnableWindow(hDesk, FALSE); EnableWindow(hDefault, FALSE); EnableWindow(hUninst, FALSE);
            SetWindowTextW(hText, L"Desinstalando…"); ShowWindow(hBar, SW_SHOW);
            std::thread([] { bool ok = DoUninstall(); PostMessageW(hMain, WM_DONE, ok, 1); }).detach();
            break;
        case ID_CANCEL: case ID_CLOSE: DestroyWindow(h); break;
        case ID_OPEN: {
            // Sin elevación: si se lanza elevado desde el instalador, Explorer no puede hacer drag&drop
            std::wstring exe = g_dir + L"\\PikPlayer.exe";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", exe.c_str(), g_dir.c_str(), SW_SHOWNORMAL);
            DestroyWindow(h);
            break;
        }
        }
        return 0;
    case WM_PROG: SendMessageW(hBar, PBM_SETPOS, wp, 0); return 0;
    case WM_DONE:
        g_busy = false; ShowWindow(hBar, SW_HIDE); ShowWindow(hInstall, SW_HIDE); ShowWindow(hCancel, SW_HIDE); ShowWindow(hDesk, SW_HIDE); ShowWindow(hDefault, SW_HIDE);
        if (hUninst) ShowWindow(hUninst, SW_HIDE);
        ShowWindow(hClose, SW_SHOW);
        if (lp == 1) {   // desinstalación
            SetWindowTextW(hText, wp ? L"PikPlayer se ha desinstalado correctamente." : (L"No se pudo desinstalar.\r\n\r\n" + g_err).c_str());
        } else if (wp) {
            SetWindowTextW(hText, g_installed ? L"PikPlayer se ha reparado correctamente.\r\n\r\nLo encontrarás en el menú Inicio y en \"Abrir con\" para tus vídeos."
                                              : L"PikPlayer se ha instalado correctamente.\r\n\r\nLo encontrarás en el menú Inicio y en \"Abrir con\" para tus vídeos.");
            ShowWindow(hOpen, SW_SHOW);
        } else SetWindowTextW(hText, (L"No se pudo instalar.\r\n\r\n" + g_err).c_str());
        return 0;
    case WM_CLOSE: if (g_busy) return 0; DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX ic{ sizeof ic, ICC_PROGRESS_CLASS }; InitCommonControlsEx(&ic);
    g_dir = inst::KnownFolder(FOLDERID_ProgramFilesX64) + L"\\PikPlayer";
    DetectInstall();
    WNDCLASSEXW wc{ sizeof wc }; wc.hInstance = hi; wc.lpfnWndProc = Proc; wc.lpszClassName = L"PikPlayerSetup";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    int dpi = (int)GetDpiForSystem(); RECT r{ 0, 0, MulDiv(500, dpi, 96), MulDiv(398, dpi, 96) };
    DWORD st = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectExForDpi(&r, st, FALSE, 0, dpi);
    int w = r.right - r.left, h = r.bottom - r.top;
    HWND win = CreateWindowExW(0, wc.lpszClassName, STr(L"Instalador de PikPlayer").c_str(), st, (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
                               (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h, nullptr, nullptr, hi, nullptr);
    ShowWindow(win, SW_SHOW); UpdateWindow(win);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { if (!IsDialogMessageW(win, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    return 0;
}
