/* mcel_video.h — dwa bufory pasa 320×24 w "BRAM", tor wideo, zerowanie.
 *
 * Framebuffera nie ma (D28). Obraz istnieje wyłącznie tutaj i jest czytany
 * wprost przez tor wideo w takt wyświetlania.
 *
 * Adresowanie jest odtworzone literalnie za 02-architecture §4, bo to
 * kawałek, który w RTL trzeba trafić co do bitu:
 *
 *     y_local    = y mod 24
 *     grupa[1:0] = y_local / 8                 // 0,1,2 — 8 wierszy × 80 słów
 *     offset[9:0]= (y_local mod 8) * 80 + (x >> 2)
 *     lane[1:0]  = x[1:0]
 *
 * Bufor wybiera PARZYSTOŚĆ PASA: buf = (s & 1) ? B : A (§9).
 */
#ifndef MCEL_VIDEO_H
#define MCEL_VIDEO_H

#include <cstdint>

#include "mcel_regs.h"
#include "mcel_span.h"

/* ALTIRRA: device wrapper granted friend access for save states. */
class ATDeviceMaria;

namespace mcel {

constexpr int SB_GROUPS      = 3;
constexpr int SB_WORDS_GROUP = 640;    /* 8 wierszy × 80 słów, potęga dwójki */

class StripBuffers {
    friend class ::ATDeviceMaria;   /* ALTIRRA: save state */
public:
    StripBuffers();

    void clear_all(uint8_t col = 0);

    /* Zapis pojedynczego bajta — odpowiednik jednego byte-enable portu B. */
    void put(int buf, int y_local, int x, uint8_t v);
    uint8_t get(int buf, int y_local, int x) const;

    /* Tor wideo: podaje współrzędną, dostaje piksel. Kombinacyjnie, bez
     * skutków ubocznych — czyszczenie idzie drugim portem (patrz scanout). */
    uint8_t vid_pixel(unsigned x, unsigned y) const;

    /* Odczyt całego pasa + ZEROWANIE w tym samym takcie. Tor wideo czyta
     * każde słowo dokładnie raz, więc drugim portem wpisujemy zero pod ten
     * sam adres: zero taktów, zero pasma SDRAM (§9).
     *
     * Wpisywane jest ZERO, i nie da się tego zmienić (D57): pod warstwą
     * sprajtów jest warstwa tła, a 0 w buforze znaczy "tu nic nie narysowano". */
    void scanout(int s, uint8_t *out /* 320*24 */);

    /* Dekompozycja adresu bufora pasa (docs/02-architecture.md §4) — PUBLICZNA,
     * bo to jest kontrakt z RTL-em, a nie szczegół implementacji. Test
     * wyprowadza tę samą formułę niezależnie z dokumentu i porównuje. */
    static void addr_decode(int y_local, int x, int &group, int &offset, int &lane);

private:
    static void decode(int y_local, int x, int &group, int &offset, int &lane) {
        addr_decode(y_local, x, group, offset, lane);
    }
    uint32_t w_[NBUF][SB_GROUPS][SB_WORDS_GROUP];
};

/* Cel spanów: bufor pasa. Granice pionowe to okno pasa Y0..Y1. */
class StripTarget : public SpanTarget {
public:
    StripTarget(StripBuffers &sb, int strip)
        : sb_(sb), s_(strip), y0_(strip * STRIP_H), y1_(y0_ + STRIP_H) {}
    int  y_lo() const override { return y0_; }
    int  y_hi() const override { return y1_; }
    void put(int y, int x, uint8_t v) override {
        sb_.put(s_ & 1, y % STRIP_H, x, v);
    }
private:
    StripBuffers &sb_;
    int s_, y0_, y1_;
};

} /* namespace mcel */

#endif /* MCEL_VIDEO_H */
