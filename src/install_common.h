// install_common.h - registro de la app en Windows (compartido por el instalador y por PikPlayer.exe /uninstall)
#pragma once
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <cwctype>
#include <cstring>

namespace inst {
static const wchar_t* kUninstKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PikPlayer";
static const wchar_t* kAppKey    = L"Software\\Classes\\Applications\\PikPlayer.exe";
static const wchar_t* kPathKey   = L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\PikPlayer.exe";
static const wchar_t* kProgId    = L"PikPlayer.Video";
static const wchar_t* kExts[] = {
    // Video / containers
    L".mp4", L".mkv", L".avi", L".mov", L".wmv", L".webm", L".flv", L".f4v", L".m4v", L".mpg", L".mpeg",
    L".mpe", L".mp2v", L".ts", L".m2ts", L".mts", L".mxf", L".3gp", L".3g2", L".ogv", L".ogm", L".vob",
    L".asf", L".divx", L".rm", L".rmvb", L".qt", L".nut", L".ivf", L".y4m", L".h264", L".h265", L".hevc", L".mjpg", L".mjpeg", L".usm",
    // Audio / containers
    L".mp3", L".flac", L".m4a", L".m4b", L".aac", L".ogg", L".oga", L".opus", L".wav", L".aif", L".aiff",
    L".ape", L".ac3", L".dts", L".amr", L".mka", L".mp2", L".mpa", L".wma", L".alac", L".caf", L".tta" };

// Shell extension GUIDs
static const wchar_t* kShellExThumb    = L"{E357FCCD-A995-4576-B01F-234630154E96}"; // IThumbnailProvider
static const wchar_t* kShellExExtract  = L"{BB2E617C-0920-11D1-9A0B-00C04FC2D6C1}"; // IExtractImage
static const wchar_t* kShellExPreview  = L"{8895b1c6-b41f-4c1c-a562-0d564250836f}"; // IPreviewHandler
static const wchar_t* kSysPreviewClsid = L"{031EE060-67BC-460d-8847-E4A7C5E45A27}"; // vista previa nativa
// CLSID de PikPlayerThumbnail.dll
static const wchar_t* kPikThumbClsid   = L"{6D2D3E8A-8F34-4E21-9C47-725A0E1B9364}";

inline void SetStr(const std::wstring& sub, const wchar_t* name, const std::wstring& v) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, sub.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
}
inline void SetDword(const std::wstring& sub, const wchar_t* name, DWORD v) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, sub.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof v); RegCloseKey(k);
    }
}
inline std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr; std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = p;
    CoTaskMemFree(p); return r;
}
inline std::wstring StartMenuLink() { return KnownFolder(FOLDERID_CommonPrograms) + L"\\PikPlayer.lnk"; }
inline std::wstring DesktopLink()   { return KnownFolder(FOLDERID_PublicDesktop) + L"\\PikPlayer.lnk"; }

inline bool IsAudioExt(const wchar_t* e) {
    static const wchar_t* audio[] = {
        L".mp3", L".flac", L".m4a", L".m4b", L".aac", L".ogg", L".oga", L".opus", L".wav",
        L".aif", L".aiff", L".ape", L".ac3", L".dts", L".amr", L".mka", L".mp2", L".mpa",
        L".wma", L".alac", L".caf", L".tta"
    };
    for (auto a : audio) if (_wcsicmp(e, a) == 0) return true;
    return false;
}
inline bool IsUsmExt(const wchar_t* e) { return _wcsicmp(e, L".usm") == 0; }

inline std::wstring ProgFor(const wchar_t* ext) {
    std::wstring p = L"PikPlayer.";
    for (const wchar_t* q = ext + 1; *q; ++q) p += (wchar_t)towlower(*q);
    return p;
}

inline void SetShellExHandler(const std::wstring& parentKey, const wchar_t* shellExGuid, const wchar_t* handlerClsid) {
    SetStr(parentKey + L"\\ShellEx\\" + shellExGuid, nullptr, handlerClsid);
}

// Registra PikPlayerThumbnail.dll como IThumbnailProvider para la extensión
// (ProgID + .ext + SystemFileAssociations). Incluye .usm y formatos raros.
inline void RegisterPikThumbnails(const wchar_t* ext, const std::wstring& prog) {
    const std::wstring keys[] = {
        std::wstring(L"Software\\Classes\\") + prog,
        std::wstring(L"Software\\Classes\\") + ext,
        std::wstring(L"Software\\Classes\\SystemFileAssociations\\") + ext,
    };
    for (const auto& k : keys) {
        SetShellExHandler(k, kShellExThumb, kPikThumbClsid);
        // Vista previa del panel lateral: nativa de Windows cuando exista
        if (!IsUsmExt(ext))
            SetShellExHandler(k, kShellExPreview, kSysPreviewClsid);
    }
}

// CLSID + InprocServer32 de la DLL de miniaturas
inline void RegisterThumbClsid(const std::wstring& dllPath) {
    std::wstring base = std::wstring(L"Software\\Classes\\CLSID\\") + kPikThumbClsid;
    SetStr(base, nullptr, L"PikPlayer Thumbnail Provider");
    SetStr(base + L"\\InprocServer32", nullptr, dllPath);
    SetStr(base + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
    // Necesario para IInitializeWithFile (sin stream)
    SetDword(base, L"DisableProcessIsolation", 1);
}

inline void UnregisterThumbClsid() {
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, (std::wstring(L"Software\\Classes\\CLSID\\") + kPikThumbClsid).c_str());
}

// Borra ShellEx de miniaturas solo si apunta a nuestro CLSID
inline void DeleteShellExIfPik(const std::wstring& parentKey, const wchar_t* shellExGuid) {
    std::wstring path = parentKey + L"\\ShellEx\\" + shellExGuid;
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &h) != ERROR_SUCCESS)
        return;
    wchar_t buf[128] = {};
    DWORD type = 0, cb = sizeof(buf);
    LONG r = RegQueryValueExW(h, nullptr, nullptr, &type, (LPBYTE)buf, &cb);
    RegCloseKey(h);
    if (r == ERROR_SUCCESS && type == REG_SZ && _wcsicmp(buf, kPikThumbClsid) == 0)
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, path.c_str());
}

inline void Register(const std::wstring& dir, DWORD sizeKB) {
    std::wstring exe = dir + L"\\PikPlayer.exe";
    SetStr(kPathKey, nullptr, exe);

    // Un ProgID único por extensión evita que Explorer muestre "PikPlayer"
    // en la columna Tipo. PikPlayer sigue siendo la aplicación asociada.
    auto friendlyFor = [](const wchar_t* ext) {
        std::wstring e = ext + 1;
        for (auto& c : e) c = (wchar_t)towupper(c);
        return e + L" file";
    };
    for (auto e : kExts) {
        std::wstring prog = ProgFor(e);
        std::wstring friendly = friendlyFor(e);
        std::wstring base = std::wstring(L"Software\\Classes\\") + prog;
        SetStr(base, nullptr, friendly);
        SetStr(base, L"FriendlyTypeName", friendly);
        // Prevent Explorer from adding the legacy video-sprocket adornment.
        SetDword(base, L"Treatment", 0);
        SetStr(base + L"\\DefaultIcon", nullptr, exe + L",0");
        SetStr(base + L"\\shell\\open\\command", nullptr, L"\"" + exe + L"\" \"%1\"");
        // Ensure Explorer resolves this extension through its own ProgID, so the Type column
        // is the format name instead of the application name.
        SetStr(std::wstring(L"Software\\Classes\\") + e, nullptr, prog);
        SetStr(std::wstring(L"Software\\Classes\\") + e + L"\\OpenWithProgids", prog.c_str(), L"");
        SetStr(std::wstring(L"Software\\Classes\\") + e, L"PerceivedType",
               IsAudioExt(e) ? L"audio" : L"video");
        SetStr(L"Software\\PikPlayer\\Capabilities\\FileAssociations", e, prog);

        // Miniaturas propias (libmpv / USM) para todas las extensiones, incluido .usm.
        RegisterPikThumbnails(e, prog);
    }
    // CLSID de la DLL de miniaturas (debe existir en dir)
    RegisterThumbClsid(dir + L"\\PikPlayerThumbnail.dll");

    SetStr(L"Software\\PikPlayer\\Capabilities", L"ApplicationName", L"PikPlayer");
    SetStr(L"Software\\PikPlayer\\Capabilities", L"ApplicationDescription", L"Reproductor de vídeo PikPlayer");
    SetStr(L"Software\\RegisteredApplications", L"PikPlayer", L"Software\\PikPlayer\\Capabilities");

    SetStr(kAppKey, L"FriendlyAppName", L"PikPlayer");
    SetStr(std::wstring(kAppKey) + L"\\shell\\open\\command", nullptr, L"\"" + exe + L"\" \"%1\"");
    for (auto e : kExts) SetStr(std::wstring(kAppKey) + L"\\SupportedTypes", e, L"");

    std::wstring uninst = dir + L"\\Uninstall.exe";
    SetStr(kUninstKey, L"DisplayName", L"PikPlayer");
    SetStr(kUninstKey, L"DisplayVersion", L"1.0");
    SetStr(kUninstKey, L"Publisher", L"PikPlayer");
    SetStr(kUninstKey, L"InstallLocation", dir);
    SetStr(kUninstKey, L"DisplayIcon", exe);
    SetStr(kUninstKey, L"UninstallString", L"\"" + uninst + L"\"");
    SetDword(kUninstKey, L"NoModify", 1); SetDword(kUninstKey, L"NoRepair", 1); SetDword(kUninstKey, L"EstimatedSize", sizeKB);
}

inline void Unregister() {
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kPathKey);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kAppKey);
    for (auto e : kExts) {
        std::wstring prog = ProgFor(e);
        // Borra solo nuestro ProgID completo (incluye cualquier ShellEx bajo él).
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, (std::wstring(L"Software\\Classes\\") + prog).c_str());

        // En .ext y SystemFileAssociations: solo quitar el handler si era el DLL
        // antiguo de PikPlayer. Nunca borrar el manejador nativo de Windows.
        std::wstring extKey = std::wstring(L"Software\\Classes\\") + e;
        std::wstring sfaKey = std::wstring(L"Software\\Classes\\SystemFileAssociations\\") + e;
        DeleteShellExIfPik(extKey, kShellExThumb);
        DeleteShellExIfPik(extKey, kShellExExtract);
        DeleteShellExIfPik(extKey, kShellExPreview);
        DeleteShellExIfPik(sfaKey, kShellExThumb);
        DeleteShellExIfPik(sfaKey, kShellExExtract);
        DeleteShellExIfPik(sfaKey, kShellExPreview);

        // Si el valor por defecto de .ext apuntaba a nuestro ProgID, quitarlo
        // para no dejar una asociación rota tras desinstalar.
        {
            HKEY h = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, extKey.c_str(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &h) == ERROR_SUCCESS) {
                wchar_t buf[128] = {};
                DWORD type = 0, cb = sizeof(buf);
                if (RegQueryValueExW(h, nullptr, nullptr, &type, (LPBYTE)buf, &cb) == ERROR_SUCCESS
                    && type == REG_SZ && _wcsicmp(buf, prog.c_str()) == 0)
                    RegDeleteValueW(h, nullptr);
                RegCloseKey(h);
            }
        }
        // OpenWithProgids de nuestro ProgID
        HKEY h = nullptr;
        std::wstring owp = extKey + L"\\OpenWithProgids";
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, owp.c_str(), 0, KEY_SET_VALUE, &h) == ERROR_SUCCESS) {
            RegDeleteValueW(h, prog.c_str());
            RegCloseKey(h);
        }
    }
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, (std::wstring(L"Software\\Classes\\") + kProgId).c_str());
    UnregisterThumbClsid();
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"Software\\PikPlayer");
    {
        HKEY k = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\RegisteredApplications", 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
            RegDeleteValueW(k, L"PikPlayer");
            RegCloseKey(k);
        }
    }
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kUninstKey);
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\PikPlayer");
    DeleteFileW(StartMenuLink().c_str());
    DeleteFileW(DesktopLink().c_str());
}
} // namespace inst
