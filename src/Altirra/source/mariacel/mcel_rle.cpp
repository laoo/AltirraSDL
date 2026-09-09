#include "mcel_rle.h"

#include <cstring>

namespace mcel {

RleDecodeResult rle_decode_row(const uint8_t *packets, size_t avail,
                               uint8_t *out, uint8_t *skip, int max_out) {
    RleDecodeResult r;
    size_t p = 0;

    while (r.produced < max_out) {
        if (p >= avail) { r.malformed = true; break; }
        const uint8_t hdr = packets[p++];
        const uint8_t type = uint8_t(hdr & RLE_TYPE_MASK);
        const int units = int(hdr & RLE_COUNT_MASK) + 1;

        if (type == RLE_EOL) { r.hit_eol = true; break; }

        const int take = (units < max_out - r.produced) ? units : (max_out - r.produced);

        switch (type) {
        case RLE_LITERAL:
            if (p + size_t(units) > avail) { r.malformed = true; p = avail; break; }
            std::memcpy(out + r.produced, packets + p, size_t(take));
            std::memset(skip + r.produced, 0, size_t(take));
            p += size_t(units);              /* wskaźnik idzie po CAŁYM pakiecie */
            r.produced += take;
            break;

        case RLE_REPEAT:
            if (p >= avail) { r.malformed = true; break; }
            std::memset(out + r.produced, packets[p], size_t(take));
            std::memset(skip + r.produced, 0, size_t(take));
            p += 1;                          /* REPEAT to zawsze jeden bajt danych */
            r.produced += take;
            break;

        case RLE_TRANSPARENT:
            /* Brak danych w strumieniu i brak zapisu na wyjściu. */
            std::memset(out + r.produced, 0, size_t(take));
            std::memset(skip + r.produced, 1, size_t(take));
            r.produced += take;
            break;

        default: break;
        }
        if (r.malformed) break;
    }

    r.consumed = p;
    return r;
}

size_t rle_row_length(const uint8_t *packets, size_t avail, bool *malformed) {
    size_t p = 0;
    if (malformed) *malformed = false;
    for (;;) {
        if (p >= avail) { if (malformed) *malformed = true; return p; }
        const uint8_t hdr = packets[p++];
        const uint8_t type = uint8_t(hdr & RLE_TYPE_MASK);
        const int units = int(hdr & RLE_COUNT_MASK) + 1;
        if (type == RLE_EOL) return p;
        if (type == RLE_LITERAL) p += size_t(units);
        else if (type == RLE_REPEAT) p += 1;
        if (p > avail) { if (malformed) *malformed = true; return avail; }
    }
}

} /* namespace mcel */
