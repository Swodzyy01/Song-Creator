#include "LevelString.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>

namespace ais {

std::string fmtNum(double v) {
    bool neg = v < 0;
    if (neg) v = -v;
    long long scaled = std::llround(v * 100.0);
    long long ip = scaled / 100, fp = scaled % 100;
    std::string s;
    if (neg && scaled != 0) s += '-';
    s += std::to_string(ip);
    if (fp) {
        s += '.';
        s += char('0' + fp / 10);
        if (fp % 10) s += char('0' + fp % 10);
    }
    return s;
}

std::string buildLevelString(Level const& lv, Palette const& pal) {
    std::string s;
    s.reserve(lv.objs.size() * 26 + 4096);
    s += "kS38,";
    auto chan = [&](Col c, int id, float alpha, bool blend) {
        s += "1_" + std::to_string(c.r) + "_2_" + std::to_string(c.g) + "_3_" + std::to_string(c.b);
        s += "_11_255_12_255_13_255_4_-1_6_" + std::to_string(id) + "_";
        if (blend) s += "5_1_";
        s += "7_" + fmtNum(double(alpha)) + "_15_1_18_0_8_1|";
    };
    chan(pal.bg, 1000, 1.f, false);
    chan(pal.ground, 1001, 1.f, false);
    chan(pal.ground2, 1009, 1.f, false);
    chan(pal.line, 1002, 1.f, true);
    chan(pal.obj, 1004, 1.f, false);
    chan(Col{255, 255, 255}, 1003, 1.f, false);
    chan(pal.d1, 1, pal.d1a, true);
    chan(pal.d2, 2, pal.d2a, true);
    chan(pal.d3, 3, pal.d3a, true);
    chan(pal.d4, 4, pal.d4a, true);
    s += ",kA13,0,kA15,0,kA16,0,kA14,,kA6,0,kA7,0,kA25,0,kA17,0,kA18,0,kS39,0,kA2,0,kA3,0,kA8,0,kA4,0,kA9,0,kA10,0,kA22,0,kA23,0,kA24,0,kA27,1,kA40,1,kA41,1,kA42,1,kA28,0,kA29,0,"
         "kA31,1,kA32,1,kA36,0,kA43,0,kA44,0,kA45,1,kA46,0,kA33,1,kA34,1,kA35,0,kA37,1,kA38,1,kA39,1,kA19,0,kA26,0,kA20,0,kA21,0,kA11,0;";

    std::vector<size_t> idx(lv.objs.size());
    std::iota(idx.begin(), idx.end(), size_t(0));
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return lv.objs[a].x < lv.objs[b].x; });
    for (size_t i : idx) {
        Obj const& o = lv.objs[i];
        s += "1,";
        s += std::to_string(o.id);
        s += ",2,";
        s += fmtNum(double(o.x));
        s += ",3,";
        s += fmtNum(double(o.y));
        s += o.kv;
        s += ';';
    }
    return s;
}

// ---------------------------------------------------------------------------------------------
// gzip + base64url
// ---------------------------------------------------------------------------------------------

namespace {

uint32_t crc32(std::string const& d) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : d) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

struct BitWriter {
    std::string out;
    uint32_t acc = 0;
    int nb = 0;
    void bits(uint32_t v, int n) {
        acc |= v << nb;
        nb += n;
        while (nb >= 8) {
            out.push_back(char(acc & 0xFF));
            acc >>= 8;
            nb -= 8;
        }
    }
    void huff(uint32_t code, int len) {  // Huffman codes are sent most-significant bit first
        uint32_t r = 0;
        for (int i = 0; i < len; ++i) r |= ((code >> i) & 1u) << (len - 1 - i);
        bits(r, len);
    }
    void flush() {
        if (nb > 0) {
            out.push_back(char(acc & 0xFF));
            acc = 0;
            nb = 0;
        }
    }
};

void putLiteral(BitWriter& w, int sym) {
    if (sym < 144) w.huff(0x30u + uint32_t(sym), 8);
    else w.huff(0x190u + uint32_t(sym - 144), 9);
}

void putLengthSym(BitWriter& w, int sym) {  // 256..287
    if (sym < 280) w.huff(uint32_t(sym - 256), 7);
    else w.huff(0xC0u + uint32_t(sym - 280), 8);
}

const int kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const int kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void putMatch(BitWriter& w, int len, int dist) {
    int li = 28;
    while (li > 0 && kLenBase[li] > len) --li;
    putLengthSym(w, 257 + li);
    if (kLenExtra[li]) w.bits(uint32_t(len - kLenBase[li]), kLenExtra[li]);
    int di = 29;
    while (di > 0 && kDistBase[di] > dist) --di;
    w.huff(uint32_t(di), 5);
    if (kDistExtra[di]) w.bits(uint32_t(dist - kDistBase[di]), kDistExtra[di]);
}

std::string deflateFixed(std::string const& in) {
    BitWriter w;
    w.bits(1, 1);  // final block
    w.bits(1, 2);  // fixed Huffman
    size_t n = in.size();
    auto const* d = reinterpret_cast<unsigned char const*>(in.data());
    const int HB = 15;
    std::vector<int32_t> head(size_t(1) << HB, -1), prev(n, -1);
    auto hashAt = [&](size_t i) { return (uint32_t(((uint32_t(d[i]) << 16) | (uint32_t(d[i + 1]) << 8) | d[i + 2]) * 2654435761u)) >> (32 - HB); };
    size_t i = 0;
    while (i < n) {
        int bestLen = 0, bestDist = 0;
        if (i + 3 <= n) {
            uint32_t h = hashAt(i);
            int32_t cand = head[h];
            int chain = 32;
            while (cand >= 0 && chain-- > 0 && i - size_t(cand) <= 32768) {
                size_t maxl = std::min<size_t>(258, n - i);
                size_t l = 0;
                while (l < maxl && d[size_t(cand) + l] == d[i + l]) ++l;
                if (int(l) > bestLen) {
                    bestLen = int(l);
                    bestDist = int(i - size_t(cand));
                    if (l == maxl) break;
                }
                cand = prev[size_t(cand)];
            }
        }
        size_t adv = 1;
        if (bestLen >= 3) {
            putMatch(w, bestLen, bestDist);
            adv = size_t(bestLen);
        } else {
            putLiteral(w, d[i]);
        }
        for (size_t k = 0; k < adv; ++k) {
            size_t p = i + k;
            if (p + 3 <= n) {
                uint32_t h = hashAt(p);
                prev[p] = head[h];
                head[h] = int32_t(p);
            }
        }
        i += adv;
    }
    putLengthSym(w, 256);  // end of block
    w.flush();
    return w.out;
}

void put32le(std::string& s, uint32_t v) {
    for (int k = 0; k < 4; ++k) s.push_back(char((v >> (8 * k)) & 0xFF));
}

std::string base64url(std::string const& in) {
    static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    while (i + 3 <= in.size()) {
        uint32_t v = (uint32_t(uint8_t(in[i])) << 16) | (uint32_t(uint8_t(in[i + 1])) << 8) | uint32_t(uint8_t(in[i + 2]));
        out += A[(v >> 18) & 63];
        out += A[(v >> 12) & 63];
        out += A[(v >> 6) & 63];
        out += A[v & 63];
        i += 3;
    }
    if (i + 1 == in.size()) {
        uint32_t v = uint32_t(uint8_t(in[i])) << 16;
        out += A[(v >> 18) & 63];
        out += A[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = (uint32_t(uint8_t(in[i])) << 16) | (uint32_t(uint8_t(in[i + 1])) << 8);
        out += A[(v >> 18) & 63];
        out += A[(v >> 12) & 63];
        out += A[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

}  // namespace

std::string gzipBase64(std::string const& raw) {
    std::string gz;
    static const unsigned char header[10] = {0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b};
    gz.assign(reinterpret_cast<char const*>(header), 10);
    gz += deflateFixed(raw);
    put32le(gz, crc32(raw));
    put32le(gz, uint32_t(raw.size()));
    return base64url(gz);
}

}  // namespace ais
