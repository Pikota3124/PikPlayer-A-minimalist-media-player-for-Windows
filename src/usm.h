// usm.h - Demuxer de contenedores CRI USM (Sofdec2) -> flujos elementales que libmpv/FFmpeg sí entiende.
#pragma once
#include <string>
#include <vector>

struct UsmResult {
    bool ok = false;
    std::wstring error;                 // mensaje si ok == false
    std::wstring videoPath;             // .ivf (VP9), .h264 o .m1v
    std::vector<std::wstring> audioPaths; // .adx / .hca
    std::string format;                 // "" (autodetectar) | "h264" | "mpegvideo"
    std::string fps;                    // p.ej. "30000/1001" (solo para flujos crudos)
    std::wstring note;                  // aviso no fatal (p.ej. audio cifrado)
    double duration = 0.0;               // duración calculada para flujos USM crudos
};

UsmResult UsmDemux(const std::wstring& usmPath, const std::wstring& outDir);
