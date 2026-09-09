#include "mcel_video.h"

#include <cstring>

namespace mcel {

StripBuffers::StripBuffers() { std::memset(w_, 0, sizeof(w_)); }

void StripBuffers::addr_decode(int y_local, int x, int &group, int &offset, int &lane) {
    group  = y_local / 8;
    offset = (y_local % 8) * 80 + (x >> 2);
    lane   = x & 3;
}

void StripBuffers::clear_all(uint8_t col) {
    const uint32_t word = uint32_t(col) * 0x01010101u;
    for (int b = 0; b < NBUF; ++b)
        for (int g = 0; g < SB_GROUPS; ++g)
            for (int o = 0; o < SB_WORDS_GROUP; ++o) w_[b][g][o] = word;
}

void StripBuffers::put(int buf, int y_local, int x, uint8_t v) {
    int g, o, l;
    decode(y_local, x, g, o, l);
    uint32_t &word = w_[buf & 1][g][o];
    const int sh = 8 * l;
    word = (word & ~(uint32_t(0xFF) << sh)) | (uint32_t(v) << sh);
}

uint8_t StripBuffers::get(int buf, int y_local, int x) const {
    int g, o, l;
    decode(y_local, x, g, o, l);
    return uint8_t(w_[buf & 1][g][o] >> (8 * l));
}

uint8_t StripBuffers::vid_pixel(unsigned x, unsigned y) const {
    if (x >= (unsigned)SCREEN_W || y >= (unsigned)SCREEN_H) return 0;
    const int s = strip_of((int)y);
    return get(s & 1, (int)y % STRIP_H, (int)x);
}

void StripBuffers::scanout(int s, uint8_t *out) {
    const int b = s & 1;
    /* Zawsze zero (D57). Indeks 0 znaczy "tu nic nie narysowano" i tor obrazu
     * pokazuje w tym miejscu warstwe tla — wiec nie ma czego wybierac. */
    const uint32_t fill = 0;
    for (int y_local = 0; y_local < STRIP_H; ++y_local) {
        const int g = y_local / 8;
        const int row = (y_local % 8) * 80;
        for (int wx = 0; wx < 80; ++wx) {
            uint32_t word = w_[b][g][row + wx];      /* port A: odczyt        */
            w_[b][g][row + wx] = fill;               /* port B: ten sam adres */
            if (out) {
                uint8_t *dst = out + y_local * SCREEN_W + wx * 4;
                dst[0] = uint8_t(word);
                dst[1] = uint8_t(word >> 8);
                dst[2] = uint8_t(word >> 16);
                dst[3] = uint8_t(word >> 24);
            }
        }
    }
}

} /* namespace mcel */
