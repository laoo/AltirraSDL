#include "mcel_derived.h"

#include <cstring>

namespace mcel {
namespace {

/* Little endian, jawnie. Nie std::memcpy struktury, nie reinterpret_cast. */
inline void put32(uint8_t *p, int32_t v) {
    const uint32_t u = (uint32_t)v;
    p[0] = uint8_t(u); p[1] = uint8_t(u >> 8);
    p[2] = uint8_t(u >> 16); p[3] = uint8_t(u >> 24);
}
inline int32_t get32(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}
inline void put24(uint8_t *p, uint32_t v) {
    p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16);
}
inline uint32_t get24(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}
inline void put16(uint8_t *p, uint16_t v) {
    p[0] = uint8_t(v); p[1] = uint8_t(v >> 8);
}
inline uint16_t get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

} /* anon */

void derived_pack(const Derived &d, uint8_t out[DER_REC_SZ]) {
    std::memset(out, 0, DER_REC_SZ);
    put32(out + DER_XPOS,       d.xpos);
    put32(out + DER_YPOS,       d.ypos);
    put32(out + DER_HDX,        d.hdx);
    put32(out + DER_HDY,        d.hdy);
    put32(out + DER_VDX,        d.vdx);
    put32(out + DER_VDY,        d.vdy);
    if ((d.mode & 7) == MODE_QUAD) {
        /* §12: przy QUAD te cztery słowa niosą HDDX, HDDY i akumulatory
         * startowe. Model nie prekomputuje akumulatorów — liczy je w pętli
         * z XPOS/YPOS — więc dwa ostatnie słowa zostają zerem. */
        put32(out + DER_IA, d.hddx);
        put32(out + DER_IB, d.hddy);
        put32(out + DER_IC, 0);
        put32(out + DER_ID, 0);
    } else {
        put32(out + DER_IA,     d.ia);
        put32(out + DER_IB,     d.ib);
        put32(out + DER_IC,     d.ic);
        put32(out + DER_ID,     d.id);
    }
    put32(out + DER_RCP_HDY,    d.rcp_hdy);
    put32(out + DER_RCP_VDY,    d.rcp_vdy);
    put32(out + DER_RCP_HDX,    d.rcp_hdx);
    put24(out + DER_SRC_BASE,   d.src_base & 0xFFFFFF);
    put16(out + DER_SRC_STRIDE, d.src_stride);   /* offset 55 — nieparzysty */
    put16(out + DER_SRC_W,      d.src_w);        /* offset 57 — nieparzysty */
    put16(out + DER_SRC_H,      d.src_h);        /* offset 59 — nieparzysty */
    out[DER_RSV_61]     = 0;      /* rezerwa — jawnie zero (D72) */
    out[DER_FORMAT]     = d.format;
    out[DER_MODE]       = d.mode;
}

void derived_unpack(const uint8_t in[DER_REC_SZ], Derived &d) {
    d.xpos       = get32(in + DER_XPOS);
    d.ypos       = get32(in + DER_YPOS);
    d.hdx        = get32(in + DER_HDX);
    d.hdy        = get32(in + DER_HDY);
    d.vdx        = get32(in + DER_VDX);
    d.vdy        = get32(in + DER_VDY);
    d.mode       = in[DER_MODE];          /* czytane najpierw — decyduje o +24..+39 */
    if ((d.mode & 7) == MODE_QUAD) {
        d.hddx = get32(in + DER_IA);
        d.hddy = get32(in + DER_IB);
        d.ia = d.ib = d.ic = d.id = 0;
    } else {
        d.ia     = get32(in + DER_IA);
        d.ib     = get32(in + DER_IB);
        d.ic     = get32(in + DER_IC);
        d.id     = get32(in + DER_ID);
        d.hddx = d.hddy = 0;
    }
    d.rcp_hdy    = get32(in + DER_RCP_HDY);
    d.rcp_vdy    = get32(in + DER_RCP_VDY);
    d.rcp_hdx    = get32(in + DER_RCP_HDX);
    d.src_base   = get24(in + DER_SRC_BASE);
    d.src_stride = get16(in + DER_SRC_STRIDE);
    d.src_w      = get16(in + DER_SRC_W);
    d.src_h      = get16(in + DER_SRC_H);
    d.format     = in[DER_FORMAT];
    d.mode       = in[DER_MODE];
}

} /* namespace mcel */
