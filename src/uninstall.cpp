// uninstall.cpp - Uninstall.exe (desinstalador independiente de PikPlayer)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <filesystem>
#include "install_common.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

namespace fsys = std::filesystem;

static bool IsAdmin() {
    BOOL a = FALSE; PSID g = nullptr; SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &g)) {
        CheckTokenMembership(nullptr, g, &a); FreeSid(g);
    }
    return a != FALSE;
}

static void KillPikPlayer() {
    wchar_t cmd[] = L"taskkill.exe /f /im PikPlayer.exe";
    STARTUPINFOW si{ sizeof si }; PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
}

// Quita solo nuestro Thumbnail Provider (mismo CLSID que install_common)
static void UnregisterThumb() {
    const wchar_t* clsid = inst::kPikThumbClsid;
    const wchar_t* shellEx = inst::kShellExThumb;
    for (const wchar_t* ext : inst::kExts) {
        std::wstring prog = inst::ProgFor(ext);
        std::wstring keys[] = {
            std::wstring(L"Software\\Classes\\") + ext,
            std::wstring(L"Software\\Classes\\SystemFileAssociations\\") + ext,
            std::wstring(L"Software\\Classes\\") + prog,
        };
        for (const auto& k : keys) {
            std::wstring path = k + L"\\ShellEx\\" + shellEx;
            HKEY h = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_QUERY_VALUE, &h) != ERROR_SUCCESS)
                continue;
            wchar_t buf[128] = {};
            DWORD type = 0, cb = sizeof(buf);
            LONG r = RegQueryValueExW(h, nullptr, nullptr, &type, (LPBYTE)buf, &cb);
            RegCloseKey(h);
            if (r == ERROR_SUCCESS && type == REG_SZ && _wcsicmp(buf, clsid) == 0)
                RegDeleteTreeW(HKEY_LOCAL_MACHINE, path.c_str());
        }
    }
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, (std::wstring(L"Software\\Classes\\CLSID\\") + clsid).c_str());
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = fsys::path(exe).parent_path().wstring();

    if (!IsAdmin()) {
        ShellExecuteW(nullptr, L"runas", exe, nullptr, nullptr, SW_SHOW);
        return 0;
    }

    if (MessageBoxW(nullptr, L"¿Quieres desinstalar PikPlayer?", L"PikPlayer",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
        return 0;

    KillPikPlayer();
    UnregisterThumb();
    inst::Unregister();

    wchar_t la[MAX_PATH];
    std::error_code ec;
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH))
        fsys::remove_all(fsys::path(la) / L"PikPlayer", ec);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSH, nullptr, nullptr);

    MessageBoxW(nullptr, L"PikPlayer se ha desinstalado.", L"PikPlayer", MB_ICONINFORMATION);

    // Borrar la carpeta de instalación tras salir (incluye este Uninstall.exe)
    std::wstring cmd = L"cmd.exe /c ping -n 3 127.0.0.1 >nul & rmdir /s /q \"" + dir + L"\"";
    STARTUPINFOW si{ sizeof si }; PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
    return 0;
}
