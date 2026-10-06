// pack-payload.cpp - empaqueta varios PE cifrados en un único blob PKPK para el instalador
// Uso: pack-payload.exe <seed_hex> <salida.pkpk> <nombre1> <archivo1> [nombre2 archivo2 ...]
#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

static uint32_t Crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static void Crypt(uint8_t* data, size_t n, uint32_t seed) {
    uint32_t s = seed;
    for (size_t i = 0; i < n; i++) {
        s = s * 1664525u + 1013904223u;
        uint8_t k = (uint8_t)((s >> 16) ^ (uint8_t)(i * 0x9Eu) ^ (uint8_t)(seed >> ((i & 3) * 8)));
        data[i] ^= k;
    }
}

static bool ReadFileAll(const char* path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path, "rb");
    if (!f) { perror(path); return false; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return false; }
    out.resize((size_t)sz);
    bool ok = fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

int main(int argc, char** argv) {
    // pack-payload seed out name1 file1 name2 file2 ...
    if (argc < 5 || ((argc - 3) % 2) != 0) {
        fprintf(stderr, "uso: %s <seed_hex> <out.pkpk> <name> <file> [...]\n", argv[0]);
        return 1;
    }
    uint32_t seed = 0;
    if (sscanf(argv[1], "%x", &seed) != 1 || !seed) {
        fprintf(stderr, "seed invalida\n");
        return 1;
    }
    const char* outPath = argv[2];
    int nFiles = (argc - 3) / 2;

    struct Entry {
        std::string name;
        std::vector<uint8_t> data;
        uint32_t crc = 0;
    };
    std::vector<Entry> entries;
    for (int i = 0; i < nFiles; i++) {
        Entry e;
        e.name = argv[3 + i * 2];
        if (!ReadFileAll(argv[3 + i * 2 + 1], e.data)) return 1;
        e.crc = Crc32(e.data.data(), e.data.size());
        Crypt(e.data.data(), e.data.size(), seed);
        entries.push_back(std::move(e));
        printf("  + %s (%u bytes)\n", entries.back().name.c_str(), (unsigned)entries.back().data.size());
    }

    FILE* o = fopen(outPath, "wb");
    if (!o) { perror(outPath); return 1; }

    // Header: PKPK | ver=1 | count
    const char mag[4] = { 'P', 'K', 'P', 'K' };
    uint32_t ver = 1, count = (uint32_t)entries.size();
    fwrite(mag, 1, 4, o);
    fwrite(&ver, 4, 1, o);
    fwrite(&count, 4, 1, o);

    for (auto& e : entries) {
        uint16_t nlen = (uint16_t)e.name.size();
        uint32_t sz = (uint32_t)e.data.size();
        fwrite(&nlen, 2, 1, o);
        fwrite(e.name.data(), 1, nlen, o);
        fwrite(&sz, 4, 1, o);
        fwrite(&e.crc, 4, 1, o);
        fwrite(e.data.data(), 1, e.data.size(), o);
    }
    fclose(o);
    printf("PKPK -> %s (%d archivos)\n", outPath, nFiles);
    return 0;
}
