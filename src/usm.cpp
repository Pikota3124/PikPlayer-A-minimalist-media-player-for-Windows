#define _CRT_SECURE_NO_WARNINGS
// usm.cpp - Extrae vídeo y audio de un .usm sin cifrar.
// Estructura de cada chunk: firma(4) tamaño BE(4) | cabecera 0x18 bytes | datos | relleno
#include "usm.h"
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

static FILE* OpenW(const std::wstring& p, const wchar_t* modeW, const char* modeA) {
#ifdef _WIN32
    (void)modeA; return _wfopen(p.c_str(), modeW);
#else
    (void)modeW; return fopen(fs::path(p).string().c_str(), modeA);
#endif
}

// Lee los campos numéricos de la primera fila de una tabla @UTF.
static void ParseUtf(const uint8_t* d, size_t n, std::map<std::string, int64_t>& out) {
    if (n < 0x20 || memcmp(d, "@UTF", 4) != 0) return;
    size_t rowsOff = (size_t)be16(d + 0x0a) + 8, strOff = (size_t)be32(d + 0x0c) + 8;
    unsigned cols = be16(d + 0x18);
    size_t p = 0x20, rowPos = 0;
    auto tsize = [](int t) -> size_t {
        switch (t) { case 0: case 1: return 1; case 2: case 3: return 2;
            case 4: case 5: case 8: case 0xA: return 4;
            case 6: case 7: case 9: case 0xB: return 8; }
        return 0; };
    for (unsigned c = 0; c < cols; c++) {
        if (p + 5 > n) return;
        int flag = d[p++]; int type = flag & 0x0f; int st = flag & 0xE0;
        uint32_t nameOff = be32(d + p); p += 4;
        size_t s = tsize(type);
        const uint8_t* src = nullptr;
        if (st & 0x40) { if (rowsOff + rowPos + s > n) return; src = d + rowsOff + rowPos; rowPos += s; }
        else if (st & 0x20) { if (p + s > n) return; src = d + p; p += s; }
        if (!src || type > 7 || strOff + nameOff >= n) continue;
        const char* name = (const char*)d + strOff + nameOff;
        uint64_t v = 0;
        for (size_t i = 0; i < s; i++) v = (v << 8) | src[i];
        int64_t sv = (int64_t)v;
        if (type == 1 && s == 1) sv = (int8_t)v; else if (type == 3) sv = (int16_t)v; else if (type == 5) sv = (int32_t)v;
        out[name] = sv;
    }
}

static void Put16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(v & 255); b.push_back(v >> 8); }
static void Put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 255); }


// ---------- VP9 (IVF crudo) -> WebM con índice (Cues) para que se pueda buscar con la barra de tiempo ----------
static int Seek64(FILE* f, int64_t off, int whence) {
#ifdef _WIN32
    return _fseeki64(f, off, whence);
#else
    return fseeko(f, (off_t)off, whence);
#endif
}
static void PutBE(std::vector<uint8_t>& b, uint64_t v, int n) { for (int i = n - 1; i >= 0; i--) b.push_back((uint8_t)((v >> (8 * i)) & 255)); }
// Todos los elementos usan tamaño de 8 bytes y enteros de 8 bytes: así el tamaño total se conoce de antemano.
static void Elem(std::vector<uint8_t>& o, std::initializer_list<uint8_t> id, const std::vector<uint8_t>& p) {
    o.insert(o.end(), id.begin(), id.end()); o.push_back(0x01); PutBE(o, p.size(), 7); o.insert(o.end(), p.begin(), p.end());
}
static void ElemU(std::vector<uint8_t>& o, std::initializer_list<uint8_t> id, uint64_t v) { std::vector<uint8_t> p; PutBE(p, v, 8); Elem(o, id, p); }
static void ElemS(std::vector<uint8_t>& o, std::initializer_list<uint8_t> id, const char* s) { std::vector<uint8_t> p(s, s + strlen(s)); Elem(o, id, p); }

static bool Vp9Key(const uint8_t* d, size_t n) {
    if (n < 1) return false;
    size_t bit = 0;
    auto rd = [&](int c) { unsigned v = 0; for (int i = 0; i < c; i++, bit++) v = (v << 1) | ((d[bit >> 3] >> (7 - (bit & 7))) & 1); return v; };
    if (rd(2) != 2) return false;
    unsigned lo = rd(1), hi = rd(1); unsigned profile = (hi << 1) | lo;
    if (profile == 3) rd(1);
    if (rd(1)) return false;          // show_existing_frame
    return rd(1) == 0;                // frame_type 0 = fotograma clave
}

namespace { struct Fr { uint64_t off; uint32_t len; bool key; int64_t ms; }; struct Cl { size_t a, b; bool cue; int64_t ms; uint64_t size; }; }

static bool Ivf2Webm(const std::wstring& ivfPath, const std::wstring& outPath, int64_t w, int64_t h, int64_t fn, int64_t fd) {
    FILE* in = OpenW(ivfPath, L"rb", "rb"); if (!in) return false;
    std::vector<Fr> fr;
    uint8_t hd[32], fh[12];
    if (fread(hd, 1, 32, in) != 32) { fclose(in); return false; }
    uint64_t pos = 32;
    while (fread(fh, 1, 12, in) == 12) {
        uint32_t len = (uint32_t)fh[0] | (uint32_t)fh[1] << 8 | (uint32_t)fh[2] << 16 | (uint32_t)fh[3] << 24;
        uint8_t first[8] = { 0 }; size_t rd = std::min<size_t>(len, 8);
        if (fread(first, 1, rd, in) != rd) break;
        Fr f{ pos + 12, len, Vp9Key(first, rd), (int64_t)((double)fr.size() * 1000.0 * (double)fd / (double)fn + 0.5) };
        fr.push_back(f);
        if (len > rd && Seek64(in, (int64_t)(len - rd), SEEK_CUR) != 0) break;
        pos += 12 + len;
    }
    if (fr.empty()) { fclose(in); return false; }
    fr[0].key = true;

    // clústeres: uno por cada fotograma clave (o cada 30 s / 300 fotogramas)
    std::vector<Cl> cl;
    for (size_t i = 0; i < fr.size(); i++) {
        if (cl.empty() || fr[i].key || i - cl.back().a >= 300 || fr[i].ms - cl.back().ms > 30000) cl.push_back({ i, i + 1, fr[i].key, fr[i].ms, 0 });
        else cl.back().b = i + 1;
    }
    uint64_t clustersTotal = 0; size_t cues = 0;
    for (auto& c : cl) {
        uint64_t payload = 17;                                         // Timecode
        for (size_t i = c.a; i < c.b; i++) payload += 13 + fr[i].len;   // SimpleBlock: id(1)+tamaño(8)+cabecera(4)+datos
        c.size = 12 + payload; clustersTotal += c.size; if (c.cue) cues++;
    }
    int64_t durMs = fr.back().ms + (int64_t)(1000.0 * fd / fn + 0.5);

    std::vector<uint8_t> info, tracks, tr, vid;
    ElemU(info, { 0x2A, 0xD7, 0xB1 }, 1000000);
    { double d = (double)durMs; uint64_t bits; memcpy(&bits, &d, 8); ElemU(info, { 0x44, 0x89 }, bits); }
    ElemS(info, { 0x4D, 0x80 }, "PikPlayer"); ElemS(info, { 0x57, 0x41 }, "PikPlayer");
    { std::vector<uint8_t> p = info; info.clear(); Elem(info, { 0x15, 0x49, 0xA9, 0x66 }, p); }
    ElemU(tr, { 0xD7 }, 1); ElemU(tr, { 0x73, 0xC5 }, 1); ElemU(tr, { 0x83 }, 1); ElemS(tr, { 0x86 }, "V_VP9");
    ElemU(tr, { 0x23, 0xE3, 0x83 }, (uint64_t)(1e9 * (double)fd / (double)fn));
    ElemU(vid, { 0xB0 }, (uint64_t)w); ElemU(vid, { 0xBA }, (uint64_t)h); Elem(tr, { 0xE0 }, vid);
    Elem(tracks, { 0xAE }, tr);
    { std::vector<uint8_t> p = tracks; tracks.clear(); Elem(tracks, { 0x16, 0x54, 0xAE, 0x6B }, p); }

    const uint64_t seekHeadSize = 12 + 3 * 42, cuesSize = 12 + (uint64_t)cues * 69;
    uint64_t posInfo = seekHeadSize, posTracks = posInfo + info.size(), posCues = posTracks + tracks.size(), posClusters = posCues + cuesSize;

    std::vector<uint8_t> seekHead, sp;
    auto seek = [&](std::initializer_list<uint8_t> id, uint64_t at) {
        std::vector<uint8_t> s1; std::vector<uint8_t> idv(id.begin(), id.end()); Elem(s1, { 0x53, 0xAB }, idv); ElemU(s1, { 0x53, 0xAC }, at); Elem(sp, { 0x4D, 0xBB }, s1);
    };
    seek({ 0x15, 0x49, 0xA9, 0x66 }, posInfo); seek({ 0x16, 0x54, 0xAE, 0x6B }, posTracks); seek({ 0x1C, 0x53, 0xBB, 0x6B }, posCues);
    Elem(seekHead, { 0x11, 0x4D, 0x9B, 0x74 }, sp);

    std::vector<uint8_t> cuev, cp;
    { uint64_t at = posClusters;
      for (auto& c : cl) {
          if (c.cue) {
              std::vector<uint8_t> pt, tp, one;
              ElemU(pt, { 0xB3 }, (uint64_t)c.ms);
              ElemU(tp, { 0xF7 }, 1); ElemU(tp, { 0xF1 }, at); Elem(pt, { 0xB7 }, tp);
              Elem(cp, { 0xBB }, pt);
          }
          at += c.size;
      }
      Elem(cuev, { 0x1C, 0x53, 0xBB, 0x6B }, cp); }
    if (seekHead.size() != seekHeadSize || cuev.size() != cuesSize) { fclose(in); return false; }

    std::vector<uint8_t> ebml, ep;
    ElemU(ep, { 0x42, 0x86 }, 1); ElemU(ep, { 0x42, 0xF7 }, 1); ElemU(ep, { 0x42, 0xF2 }, 4); ElemU(ep, { 0x42, 0xF3 }, 8);
    ElemS(ep, { 0x42, 0x82 }, "webm"); ElemU(ep, { 0x42, 0x87 }, 4); ElemU(ep, { 0x42, 0x85 }, 2);
    Elem(ebml, { 0x1A, 0x45, 0xDF, 0xA3 }, ep);

    FILE* out = OpenW(outPath, L"wb", "wb");
    if (!out) { fclose(in); return false; }
    bool ok = true;
    auto W = [&](const std::vector<uint8_t>& v) { if (!v.empty() && fwrite(v.data(), 1, v.size(), out) != v.size()) ok = false; };
    W(ebml);
    { std::vector<uint8_t> sg = { 0x18, 0x53, 0x80, 0x67, 0x01 }; PutBE(sg, posClusters + clustersTotal, 7); W(sg); }
    W(seekHead); W(info); W(tracks); W(cuev);
    std::vector<uint8_t> data;
    for (auto& c : cl) {
        std::vector<uint8_t> hdr = { 0x1F, 0x43, 0xB6, 0x75, 0x01 }; PutBE(hdr, c.size - 12, 7);
        ElemU(hdr, { 0xE7 }, (uint64_t)c.ms); W(hdr);
        for (size_t i = c.a; i < c.b && ok; i++) {
            std::vector<uint8_t> bh = { 0xA3, 0x01 }; PutBE(bh, 4 + (uint64_t)fr[i].len, 7);
            bh.push_back(0x81); int16_t rel = (int16_t)(fr[i].ms - c.ms); bh.push_back((uint8_t)((rel >> 8) & 255)); bh.push_back((uint8_t)(rel & 255));
            bh.push_back(fr[i].key ? 0x80 : 0x00); W(bh);
            data.resize(fr[i].len);
            if (Seek64(in, (int64_t)fr[i].off, SEEK_SET) != 0 || (fr[i].len && fread(data.data(), 1, fr[i].len, in) != fr[i].len)) { ok = false; break; }
            W(data);
        }
        if (!ok) break;
    }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    return ok;
}

namespace {
struct Out { FILE* fp = nullptr; std::wstring path; uint32_t frames = 0; std::vector<Fr> fr; };
}


static std::vector<std::pair<size_t,size_t>> H264Nals(const uint8_t* d, size_t n) {
    std::vector<std::pair<size_t,size_t>> r;
    auto start = [&](size_t p, size_t& sc)->size_t {
        for (size_t i=p; i+3<n; ++i) {
            if (d[i]==0 && d[i+1]==0 && d[i+2]==1) { sc=3; return i; }
            if (i+4<=n && d[i]==0 && d[i+1]==0 && d[i+2]==0 && d[i+3]==1) { sc=4; return i; }
        }
        return n;
    };
    size_t p=0;
    while (p<n) {
        size_t sc=0, a=start(p,sc); if (a>=n) break;
        size_t qsc=0, b=start(a+sc,qsc); if (b>=n) b=n;
        if (b>a+sc) r.push_back({a+sc,b-(a+sc)});
        p=b;
    }
    return r;
}
static bool H264Key(const uint8_t* d, size_t n) {
    auto ns=H264Nals(d,n); for(auto [o,l]:ns) if(l && (d[o]&0x1f)==5) return true; return false;
}
static std::vector<uint8_t> H264AvccFrame(const uint8_t* d, size_t n) {
    auto ns=H264Nals(d,n); std::vector<uint8_t> out;
    if(ns.empty()) { if(n>=4) { uint32_t l=(uint32_t)n; out.push_back(l>>24);out.push_back(l>>16);out.push_back(l>>8);out.push_back(l);out.insert(out.end(),d,d+n); } return out; }
    for(auto [o,l]:ns) { uint32_t z=(uint32_t)l; out.push_back(z>>24);out.push_back(z>>16);out.push_back(z>>8);out.push_back(z);out.insert(out.end(),d+o,d+o+l); }
    return out;
}
static bool GetH264ParamSets(const std::wstring& raw, const std::vector<Fr>& fr, std::vector<uint8_t>& sps, std::vector<uint8_t>& pps) {
    FILE* in=OpenW(raw,L"rb","rb"); if(!in) return false; std::vector<uint8_t> b;
    for(auto &f:fr) {
        b.resize(f.len); if(Seek64(in,(int64_t)f.off,SEEK_SET)!=0 || (f.len && fread(b.data(),1,f.len,in)!=f.len)) { fclose(in); return false; }
        for(auto [o,l]:H264Nals(b.data(),b.size())) if(l) { int t=b[o]&0x1f; if(t==7&&sps.empty()) sps.assign(b.begin()+o,b.begin()+o+l); if(t==8&&pps.empty()) pps.assign(b.begin()+o,b.begin()+o+l); }
        if(!sps.empty()&&!pps.empty()) break;
    }
    fclose(in); return !sps.empty()&&!pps.empty();
}
static bool H2642Mkv(const std::wstring& raw, const std::wstring& outPath, int64_t w, int64_t h, int64_t fn, int64_t fd, const std::vector<Fr>& fr) {
    if(fr.empty()) return false; std::vector<uint8_t> sps,pps; if(!GetH264ParamSets(raw,fr,sps,pps)) return false;
    FILE* in=OpenW(raw,L"rb","rb"); if(!in) return false;
    std::vector<std::vector<uint8_t>> frames(fr.size());
    for(size_t i=0;i<fr.size();++i){ std::vector<uint8_t> b(fr[i].len); if(Seek64(in,(int64_t)fr[i].off,SEEK_SET)!=0 || (fr[i].len&&fread(b.data(),1,fr[i].len,in)!=fr[i].len)){fclose(in);return false;} frames[i]=H264AvccFrame(b.data(),b.size()); }
    fclose(in);
    std::vector<Cl> cl; for(size_t i=0;i<fr.size();++i) if(cl.empty()||fr[i].key||i-cl.back().a>=300||fr[i].ms-cl.back().ms>30000) cl.push_back({i,i+1,fr[i].key,fr[i].ms,0}); else cl.back().b=i+1;
    uint64_t clustersTotal=0; size_t cues=0; for(auto& c:cl){uint64_t payload=17;for(size_t i=c.a;i<c.b;++i)payload+=13+frames[i].size();c.size=12+payload;clustersTotal+=c.size;if(c.cue)cues++;}
    int64_t durMs=fr.back().ms+(int64_t)(1000.0*fd/fn+0.5);
    std::vector<uint8_t> info,tracks,tr,vid;
    ElemU(info,{0x2A,0xD7,0xB1},1000000); double dd=(double)durMs; uint64_t db; memcpy(&db,&dd,8); ElemU(info,{0x44,0x89},db); ElemS(info,{0x4D,0x80},"PikPlayer"); ElemS(info,{0x57,0x41},"PikPlayer"); {auto p=info;info.clear();Elem(info,{0x15,0x49,0xA9,0x66},p);}
    ElemU(tr,{0xD7},1);ElemU(tr,{0x73,0xC5},1);ElemU(tr,{0x83},1);ElemS(tr,{0x86},"V_MPEG4/ISO/AVC");ElemU(tr,{0x23,0xE3,0x83},(uint64_t)(1e9*(double)fd/fn));
    std::vector<uint8_t> cp; cp.push_back(1); cp.push_back(sps.size()>1?sps[1]:0); cp.push_back(sps.size()>2?sps[2]:0); cp.push_back(sps.size()>3?sps[3]:0); cp.push_back(0xFF); cp.push_back(0xE1); cp.push_back((sps.size()>>8)&255);cp.push_back(sps.size()&255);cp.insert(cp.end(),sps.begin(),sps.end());cp.push_back(1);cp.push_back((pps.size()>>8)&255);cp.push_back(pps.size()&255);cp.insert(cp.end(),pps.begin(),pps.end()); Elem(tr,{0x63,0xA2},cp);
    ElemU(vid,{0xB0},(uint64_t)w);ElemU(vid,{0xBA},(uint64_t)h);Elem(tr,{0xE0},vid);Elem(tracks,{0xAE},tr);{auto p=tracks;tracks.clear();Elem(tracks,{0x16,0x54,0xAE,0x6B},p);}
    const uint64_t seekHeadSize=12+3*42,cuesSize=12+(uint64_t)cues*69;uint64_t posInfo=seekHeadSize,posTracks=posInfo+info.size(),posCues=posTracks+tracks.size(),posClusters=posCues+cuesSize;
    std::vector<uint8_t> seekHead,sp; auto seek=[&](std::initializer_list<uint8_t> id,uint64_t at){std::vector<uint8_t>s1,idv(id.begin(),id.end());Elem(s1,{0x53,0xAB},idv);ElemU(s1,{0x53,0xAC},at);Elem(sp,{0x4D,0xBB},s1);}; seek({0x15,0x49,0xA9,0x66},posInfo);seek({0x16,0x54,0xAE,0x6B},posTracks);seek({0x1C,0x53,0xBB,0x6B},posCues);Elem(seekHead,{0x11,0x4D,0x9B,0x74},sp);
    std::vector<uint8_t> cuev,cp2;uint64_t at=posClusters;for(auto&c:cl){if(c.cue){std::vector<uint8_t>pt,tp;ElemU(pt,{0xB3},(uint64_t)c.ms);ElemU(tp,{0xF7},1);ElemU(tp,{0xF1},at);Elem(pt,{0xB7},tp);Elem(cp2,{0xBB},pt);}at+=c.size;}Elem(cuev,{0x1C,0x53,0xBB,0x6B},cp2);if(seekHead.size()!=seekHeadSize||cuev.size()!=cuesSize)return false;
    std::vector<uint8_t> ebml,ep;ElemU(ep,{0x42,0x86},1);ElemU(ep,{0x42,0xF7},1);ElemU(ep,{0x42,0xF2},4);ElemU(ep,{0x42,0xF3},8);ElemS(ep,{0x42,0x82},"matroska");ElemU(ep,{0x42,0x87},4);ElemU(ep,{0x42,0x85},2);Elem(ebml,{0x1A,0x45,0xDF,0xA3},ep);
    FILE*out=OpenW(outPath,L"wb","wb");if(!out)return false;bool ok=true;auto W=[&](const std::vector<uint8_t>&v){if(!v.empty()&&fwrite(v.data(),1,v.size(),out)!=v.size())ok=false;};W(ebml);std::vector<uint8_t>sg={0x18,0x53,0x80,0x67,0x01};PutBE(sg,posClusters+clustersTotal,7);W(sg);W(seekHead);W(info);W(tracks);W(cuev);
    for(auto&c:cl){std::vector<uint8_t>hh={0x1F,0x43,0xB6,0x75,0x01};PutBE(hh,c.size-12,7);ElemU(hh,{0xE7},(uint64_t)c.ms);W(hh);for(size_t i=c.a;i<c.b&&ok;++i){std::vector<uint8_t>bh={0xA3,0x01};PutBE(bh,4+(uint64_t)frames[i].size(),7);bh.push_back(0x81);int16_t rel=(int16_t)(fr[i].ms-c.ms);bh.push_back((uint8_t)(rel>>8));bh.push_back((uint8_t)rel);bh.push_back(fr[i].key?0x80:0);W(bh);W(frames[i]);}}
    ok&=fclose(out)==0;return ok;
}

UsmResult UsmDemux(const std::wstring& usmPath, const std::wstring& outDir) {
    UsmResult R;
    FILE* f = OpenW(usmPath, L"rb", "rb");
    if (!f) { R.error = L"No se puede abrir el archivo USM."; return R; }
    std::error_code ec; fs::create_directories(fs::path(outDir), ec);

    Out video; Out audio[8];
    std::string vkind;   // "vp9" | "h264" | "mpeg"
    int64_t vw = 1920, vh = 1080, fn = 30000, fd = 1000;
    bool audioSkipped = false, audioHca = false;
    std::vector<uint8_t> buf;
    uint8_t h[8];

    while (fread(h, 1, 8, f) == 8) {
        uint32_t size = be32(h + 4);
        if (size < 0x18 || size > (256u << 20)) break;
        buf.resize(size);
        if (fread(buf.data(), 1, size, f) != size) break;
        size_t off = buf[1]; size_t pad = be16(&buf[2]); int chno = buf[4]; int type = buf[7] & 3;
        if (off + pad > size) continue;
        const uint8_t* data = buf.data() + off; size_t len = size - off - pad;

        if (!memcmp(h, "@SFV", 4)) {
            if (type == 1) {
                std::map<std::string, int64_t> m; ParseUtf(data, len, m);
                if (m.count("width")) vw = m["width"];
                if (m.count("height")) vh = m["height"];
                if (m.count("framerate_n") && m.count("framerate_d") && m["framerate_n"] > 0 && m["framerate_d"] > 0) { fn = m["framerate_n"]; fd = m["framerate_d"]; }
            } else if (type == 0 && len > 4) {
                if (!video.fp) {
                    if (data[0] == 0 && data[1] == 0 && data[2] == 1 && data[3] == 0xB3) vkind = "mpeg";
                    else if ((data[0] == 0 && data[1] == 0 && data[2] == 1) || (data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1)) vkind = "h264";
                    else if ((data[0] & 0xC0) == 0x80) vkind = "vp9";
                    else { R.error = L"El vídeo del USM está cifrado o usa un códec no reconocido."; fclose(f); return R; }
                    const wchar_t* ext = vkind == "vp9" ? L"video.ivf" : vkind == "h264" ? L"video.h264" : L"video.m1v";
                    video.path = (fs::path(outDir) / ext).wstring();
                    video.fp = OpenW(video.path, L"wb", "wb");
                    if (!video.fp) { R.error = L"No se puede escribir en la carpeta temporal."; fclose(f); return R; }
                    if (vkind == "vp9") {
                        std::vector<uint8_t> hd = { 'D','K','I','F' };
                        Put16(hd, 0); Put16(hd, 32); hd.push_back('V'); hd.push_back('P'); hd.push_back('9'); hd.push_back('0');
                        Put16(hd, (uint16_t)vw); Put16(hd, (uint16_t)vh); Put32(hd, (uint32_t)fn); Put32(hd, (uint32_t)fd); Put32(hd, 0); Put32(hd, 0);
                        fwrite(hd.data(), 1, hd.size(), video.fp);
                    }
                }
                if (vkind == "vp9") {
                    std::vector<uint8_t> fh; Put32(fh, (uint32_t)len); Put32(fh, video.frames); Put32(fh, 0);
                    fwrite(fh.data(), 1, fh.size(), video.fp);
                }
                if (vkind == "h264") {
                    int64_t ms = (int64_t)((double)video.frames * 1000.0 * (double)fd / (double)fn + 0.5);
                    int64_t off = _ftelli64(video.fp);
                    video.fr.push_back(Fr{ (uint64_t)off, (uint32_t)len, H264Key(data,len), ms });
                }
                fwrite(data, 1, len, video.fp); video.frames++;
            }
        } else if (!memcmp(h, "@SFA", 4) && type == 0 && chno < 8 && len > 4) {
            Out& a = audio[chno];
            if (!a.fp && a.path.empty()) {
                const wchar_t* ext = nullptr;
                if (data[0] == 0x80 && data[1] == 0x00) ext = L".adx";
                else if (!memcmp(data, "HCA", 4) || (data[0] == 0xC8 && data[1] == 0xC3 && data[2] == 0xC1)) { ext = L".hca"; audioHca = true; }
                if (!ext) { audioSkipped = true; a.path = L"-"; continue; }
                a.path = (fs::path(outDir) / (L"audio" + std::to_wstring(chno) + ext)).wstring();
                a.fp = OpenW(a.path, L"wb", "wb");
            }
            if (a.fp) fwrite(data, 1, len, a.fp);
        }
    }
    fclose(f);

    if (video.fp) {
        if (vkind == "vp9") { fflush(video.fp); fseek(video.fp, 24, SEEK_SET); std::vector<uint8_t> c; Put32(c, video.frames); fwrite(c.data(), 1, 4, video.fp); }
        fclose(video.fp);
    }
    for (auto& a : audio) { if (a.fp) { fclose(a.fp); R.audioPaths.push_back(a.path); } }

    if (!video.fp && video.path.empty()) { R.error = L"No se encontró ninguna pista de vídeo en el USM."; return R; }
    if (video.frames > 0 && fn > 0) R.duration = (double)video.frames * (double)fd / (double)fn;
    R.videoPath = video.path;
    if (vkind == "vp9") {   // contenedor con índice: la barra de tiempo funciona
        std::wstring webm = (fs::path(outDir) / L"video.webm").wstring();
        if (Ivf2Webm(video.path, webm, vw, vh, fn, fd)) { std::error_code e2; fs::remove(video.path, e2); R.videoPath = webm; }
        else { std::error_code e2; fs::remove(webm, e2); }
    }
    if (vkind == "h264") {
        std::wstring mkv = (fs::path(outDir) / L"video.mkv").wstring();
        if (H2642Mkv(video.path, mkv, vw, vh, fn, fd, video.fr)) { std::error_code e2; fs::remove(video.path, e2); R.videoPath = mkv; R.format.clear(); }
        else R.format = "h264";
    } else if (vkind == "mpeg") R.format = "mpegvideo";
    if (!R.format.empty()) R.fps = std::to_string(fn) + "/" + std::to_string(fd);
    if (audioSkipped) R.note = L"Audio cifrado: no soportado en esta versión.";
    else if (audioHca) R.note = L"Audio HCA: solo suena si tu libmpv incluye decodificador HCA.";
    R.ok = true;
    return R;
}
