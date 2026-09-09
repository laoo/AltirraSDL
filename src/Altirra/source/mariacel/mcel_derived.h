/* mcel_derived.h — rekord DERIVED, pakowany BAJT PO BAJCIE (D37).
 *
 * Po przesunięciu pól SRC_STRIDE, SRC_W i SRC_H zaczynają się pod
 * NIEPARZYSTYMI offsetami, a SRC_BASE ma 24 bity. W RTL to wycinanie bitów
 * z bufora i nic nie kosztuje; w C++ nałożenie na ten rekord struktury
 * z polami 16-bitowymi daje ciche śmieci — i przez wyrównanie, i przez
 * kolejność bajtów, którą model i tak musi mieć jawną, żeby trace.bin
 * odtworzył się w testbenchu.
 *
 * Dlatego struktura poniżej NIGDY nie jest nakładana na pamięć. Jest tylko
 * postacią roboczą; do SDRAM i z SDRAM idzie przez pack/unpack.
 */
#ifndef MCEL_DERIVED_H
#define MCEL_DERIVED_H

#include <cstdint>

#include "mcel_fixed.h"
#include "mcel_regs.h"

namespace mcel {

struct Derived {
    fixed_t  xpos = 0, ypos = 0;
    fixed_t  hdx = 0, hdy = 0, vdx = 0, vdy = 0;
    fixed_t  ia = 0, ib = 0, ic = 0, id = 0;
    /* QUAD nie ma macierzy odwrotnej — odwrotność odwzorowania biliniowego
     * wymagałaby pierwiastka i nie da się jej akumulować sumatorami (§12).
     * Te same cztery słowa rekordu (+24..+39) niosą wtedy HDD. Struktura
     * trzyma to w OSOBNYCH polach, żeby nazwy nie kłamały; aliasowanie żyje
     * wyłącznie w derived_pack/unpack, czyli tam, gdzie i tak mieszka prawda
     * bajtowa (D37). */
    fixed_t  hddx = 0, hddy = 0;
    int32_t  rcp_hdy = 0, rcp_vdy = 0, rcp_hdx = 0;
    uint32_t src_base = 0;        /* A[24:1] — adres bajtowy to src_base << 1 */
    uint16_t src_stride = 0;
    uint16_t src_w = 0, src_h = 0;
    uint8_t  format = 0;          /* { PAL[3:0], rezerwa, RLEW[1:0], BPP } (D44, D72) */
    uint8_t  mode = MODE_LINE;

    /* pola wyprowadzone z FORMAT — wygoda, nie zawartość rekordu */
    uint8_t  bpp_code()  const { return uint8_t(format & FMT_BPP_MASK); }
    bool     rle()       const { return rlew_bytes(format) != 0; }
    int      rle_width() const { return rlew_bytes(format); }   /* bajtów na wpis, D44 */
    uint8_t  pal()      const { return uint8_t(format >> 4); }

    /* Wartość, z którą detektor porównuje TEKSEL — indeks 0 w palecie
     * efektywnej (D72). Przy 4 bpp rozpakowywacz składa {PAL, nibbel}, więc
     * nibbel zerowy wychodzi jako {PAL, 0000}; przy 8 bpp paleta w teksel nie
     * wchodzi, więc odniesieniem jest gołe $00.
     *
     * Wyprowadzone z FORMAT tak samo jak pozostałe pola tej sekcji — w RTL
     * cztery bramki AND na PAL, bramkowane bitem BPP. */
    uint8_t  transp_ref() const {
        return bpp_code() == FMT_BPP_4 ? uint8_t(pal() << 4) : uint8_t(0);
    }
};

void derived_pack  (const Derived &d, uint8_t out[DER_REC_SZ]);
void derived_unpack(const uint8_t in[DER_REC_SZ], Derived &d);

} /* namespace mcel */

#endif /* MCEL_DERIVED_H */
