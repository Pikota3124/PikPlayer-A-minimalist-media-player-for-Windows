// encrypt-payload.cpp - cifra binarios para embeber en el instalador (PKPE)
// Uso: encrypt-payload.exe <entrada> <salida> <seed_hex>
#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>

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

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "uso: %s <in> <out> <seed_hex>\n", argv[0]);
        return 1;
    }
    uint32_t seed = 0;
    if (sscanf(argv[3], "%x", &seed) != 1 || seed == 0) {
        fprintf(stderr, "seed invalida\n");
        return 1;
    }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 1; }
    std::vector<uint8_t> buf((size_t)sz);
    if (fread(buf.data(), 1, buf.size(), f) != buf.size()) { fclose(f); return 1; }
    fclose(f);

    uint32_t crc = Crc32(buf.data(), buf.size());
    Crypt(buf.data(), buf.size(), seed);

    FILE* o = fopen(argv[2], "wb");
    if (!o) { perror(argv[2]); return 1; }
    const char mag[4] = { 'P', 'K', 'P', 'E' };
    uint32_t ver = 1, usize = (uint32_t)sz;
    fwrite(mag, 1, 4, o);
    fwrite(&ver, 4, 1, o);
    fwrite(&usize, 4, 1, o);
    fwrite(&crc, 4, 1, o);
    fwrite(buf.data(), 1, buf.size(), o);
    fclose(o);
    printf("PKPE %s -> %s (%u bytes, crc=%08X)\n", argv[1], argv[2], usize, crc);
    return 0;
}
