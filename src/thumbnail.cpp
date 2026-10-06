// PikPlayerThumbnail.dll - IThumbnailProvider para Explorer
// Extrae fotogramas invocando PikPlayer.exe /thumb (libmpv + demux USM).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <thumbcache.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <new>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "windowscodecs.lib")

// Mismo CLSID que usaba la versión antigua (setup.cpp / install_common.h)
static const CLSID CLSID_PikThumb =
{ 0x6D2D3E8A, 0x8F34, 0x4E21, { 0x9C, 0x47, 0x72, 0x5A, 0x0E, 0x1B, 0x93, 0x64 } };

static HINSTANCE g_hInst = nullptr;
static LONG g_locks = 0;

// ---------- utilidades ----------
static std::wstring GetModuleDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_hInst, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return path;
}

static std::wstring FindPikPlayerExe() {
    // 1) Junto a la DLL
    std::wstring dir = GetModuleDir();
    std::wstring exe = dir + L"\\PikPlayer.exe";
    if (GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES) return exe;

    // 2) InstallLocation del registro
    wchar_t buf[MAX_PATH] = {};
    DWORD cb = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PikPlayer",
            L"InstallLocation", RRF_RT_REG_SZ, nullptr, buf, &cb) == ERROR_SUCCESS) {
        exe = std::wstring(buf) + L"\\PikPlayer.exe";
        if (GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES) return exe;
    }
    return {};
}

static std::wstring TempImagePath() {
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    wchar_t name[MAX_PATH];
    GetTempFileNameW(tmp, L"pkt", 0, name);
    // GetTempFileName crea un .tmp vacío; el helper genera JPEG
    std::wstring img = name;
    DeleteFileW(name);
    size_t dot = img.rfind(L'.');
    if (dot != std::wstring::npos) img = img.substr(0, dot);
    img += L".jpg";
    return img;
}

// Lanza PikPlayer.exe /thumb y espera (timeout ms). Devuelve true si generó el PNG.
static bool RunThumbHelper(const std::wstring& input, const std::wstring& output, int cx, DWORD timeoutMs) {
    std::wstring exe = FindPikPlayerExe();
    if (exe.empty()) return false;

    // Comilla rutas por si tienen espacios
    auto q = [](const std::wstring& s) {
        return L"\"" + s + L"\"";
    };
    std::wstring cmd = q(exe) + L" /thumb " + q(input) + L" " + q(output) + L" " + std::to_wstring(cx);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);

    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_DEFAULT_ERROR_MODE,
                        nullptr, nullptr, &si, &pi))
        return false;

    DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD code = 1;
    if (wait == WAIT_OBJECT_0)
        GetExitCodeProcess(pi.hProcess, &code);
    else
        TerminateProcess(pi.hProcess, 9);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (code != 0) {
        DeleteFileW(output.c_str());
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(output.c_str(), GetFileExInfoStandard, &fad) || fad.nFileSizeLow < 32) {
        DeleteFileW(output.c_str());
        return false;
    }
    return true;
}

// JPEG/PNG -> HBITMAP 16:9 (letterbox/pillarbox). Todas las miniaturas
// comparten el mismo marco que un vídeo 16:9, con fondo oscuro si el
// contenido tiene otra proporción (p.ej. carátula cuadrada de FLAC).
static HBITMAP ImageToHBitmap16x9(const std::wstring& path, UINT maxEdge) {
    if (maxEdge < 16) maxEdge = 16;
    if (maxEdge > 1024) maxEdge = 1024;

    // Lienzo 16:9: el lado mayor es maxEdge
    UINT outW = maxEdge;
    UINT outH = (maxEdge * 9 + 8) / 16;
    if (outH < 1) outH = 1;

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))))
        return nullptr;

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                    WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) { factory->Release(); return nullptr; }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) { decoder->Release(); factory->Release(); return nullptr; }

    IWICFormatConverter* conv = nullptr;
    hr = factory->CreateFormatConverter(&conv);
    if (FAILED(hr)) { frame->Release(); decoder->Release(); factory->Release(); return nullptr; }

    hr = conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                          nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        conv->Release(); frame->Release(); decoder->Release(); factory->Release();
        return nullptr;
    }

    UINT srcW = 0, srcH = 0;
    conv->GetSize(&srcW, &srcH);
    if (srcW == 0 || srcH == 0) {
        conv->Release(); frame->Release(); decoder->Release(); factory->Release();
        return nullptr;
    }

    // Escalar manteniendo aspect ratio para caber dentro del 16:9
    double scale = (std::min)((double)outW / (double)srcW, (double)outH / (double)srcH);
    UINT fitW = (UINT)(std::max)(1.0, srcW * scale + 0.5);
    UINT fitH = (UINT)(std::max)(1.0, srcH * scale + 0.5);
    if (fitW > outW) fitW = outW;
    if (fitH > outH) fitH = outH;

    IWICBitmapScaler* scaler = nullptr;
    hr = factory->CreateBitmapScaler(&scaler);
    if (FAILED(hr)) {
        conv->Release(); frame->Release(); decoder->Release(); factory->Release();
        return nullptr;
    }
    hr = scaler->Initialize(conv, fitW, fitH, WICBitmapInterpolationModeFant);
    if (FAILED(hr)) {
        scaler->Release(); conv->Release(); frame->Release(); decoder->Release(); factory->Release();
        return nullptr;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)outW;
    bmi.bmiHeader.biHeight = -(LONG)outH; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP hbmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);

    if (!hbmp || !bits) {
        if (hbmp) DeleteObject(hbmp);
        scaler->Release(); conv->Release(); frame->Release(); decoder->Release(); factory->Release();
        return nullptr;
    }

    // Fondo oscuro (como Explorer con vídeos)
    const UINT outStride = outW * 4;
    const UINT outBytes = outStride * outH;
    memset(bits, 0x18, outBytes); // gris muy oscuro opaco aproximadamente
    // Poner alpha a 255
    BYTE* p = (BYTE*)bits;
    for (UINT i = 0; i < outW * outH; i++)
        p[i * 4 + 3] = 255;

    // Centrar la imagen escalada
    UINT ox = (outW - fitW) / 2;
    UINT oy = (outH - fitH) / 2;
    std::vector<BYTE> row(fitW * 4);
    for (UINT y = 0; y < fitH; y++) {
        WICRect rc{ 0, (INT)y, (INT)fitW, 1 };
        if (FAILED(scaler->CopyPixels(&rc, fitW * 4, fitW * 4, row.data())))
            continue;
        BYTE* dst = (BYTE*)bits + ((oy + y) * outStride) + (ox * 4);
        memcpy(dst, row.data(), fitW * 4);
    }

    scaler->Release();
    conv->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return hbmp;
}

// ---------- COM: Thumbnail Provider ----------
class ThumbProvider : public IThumbnailProvider, public IInitializeWithFile {
    LONG m_ref = 1;
    std::wstring m_path;
public:
    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IThumbnailProvider)
            *ppv = static_cast<IThumbnailProvider*>(this);
        else if (riid == IID_IInitializeWithFile)
            *ppv = static_cast<IInitializeWithFile*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&m_ref);
        if (n == 0) delete this;
        return (ULONG)n;
    }

    // IInitializeWithFile
    HRESULT STDMETHODCALLTYPE Initialize(LPCWSTR pszFilePath, DWORD /*grfMode*/) override {
        if (!pszFilePath || !*pszFilePath) return E_INVALIDARG;
        if (!m_path.empty()) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        m_path = pszFilePath;
        return S_OK;
    }

    // IThumbnailProvider
    HRESULT STDMETHODCALLTYPE GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override {
        if (!phbmp) return E_POINTER;
        *phbmp = nullptr;
        if (pdwAlpha) *pdwAlpha = WTSAT_ARGB;
        if (m_path.empty()) return E_UNEXPECTED;

        UINT size = cx;
        if (size < 16) size = 16;
        if (size > 1024) size = 1024;

        std::wstring img = TempImagePath();
        // Primer arranque puede extraer libmpv; luego debería ser <3–5 s
        DWORD timeout = 12000;
        if (!RunThumbHelper(m_path, img, (int)size, timeout)) {
            DeleteFileW(img.c_str());
            return E_FAIL;
        }

        HBITMAP hbmp = ImageToHBitmap16x9(img, size);
        DeleteFileW(img.c_str());
        if (!hbmp) return E_FAIL;

        *phbmp = hbmp;
        return S_OK;
    }
};

// ---------- Class Factory ----------
class ClassFactory : public IClassFactory {
    LONG m_ref = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IClassFactory)
            *ppv = static_cast<IClassFactory*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&m_ref);
        if (n == 0) delete this;
        return (ULONG)n;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        ThumbProvider* p = new (std::nothrow) ThumbProvider();
        if (!p) return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&g_locks);
        else InterlockedDecrement(&g_locks);
        return S_OK;
    }
};

// ---------- exports COM ----------
BOOL APIENTRY DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = h;
        DisableThreadLibraryCalls(h);
    }
    return TRUE;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (rclsid != CLSID_PikThumb) return CLASS_E_CLASSNOTAVAILABLE;
    ClassFactory* f = new (std::nothrow) ClassFactory();
    if (!f) return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

extern "C" HRESULT __stdcall DllCanUnloadNow() {
    return g_locks == 0 ? S_OK : S_FALSE;
}

// Registro manual desde el instalador; stubs por si se usa regsvr32
extern "C" HRESULT __stdcall DllRegisterServer() { return S_OK; }
extern "C" HRESULT __stdcall DllUnregisterServer() { return S_OK; }
