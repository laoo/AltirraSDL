/* mcel_span.h — interfejs rasteryzator → cel (D23) i writer spanów.
 *
 * Rasteryzator emituje (y, x0, x1, teksel) i NIE WIE, dokąd. Podstawiasz albo
 * bufor pasa (granice Y0..Y1, adresowanie y mod 24), albo płaski bufor
 * 320×240 z granicami (0, 240). To jest ta sama ścieżka kodu — i tylko dlatego
 * diff "pasami vs. cała klatka" cokolwiek dowodzi.
 *
 * Konwencja: span to przedział PÓŁOTWARTY [x0, x1), a y jest współrzędną
 * EKRANOWĄ. Reguła CEL-a "domknięte w x" (§12, QUAD) należy do ścieżki, która
 * span buduje — writer dostaje zawsze [x0, x1).
 *
 * Kolejność stopni w writerze jest kontraktem, nie szczegółem:
 *
 *     przycięcie → LICZNIK POKRYCIA → WE_rle → przezroczystość → maska → zapis
 *                    (D14, przed maską)                              (D42, ostatni)
 *
 * `WE_rle` to byte-enable z dekodera pakietów: pakiet `TRANSPARENT` niesie
 * BRAK ZAPISU, a nie wartość (§10). Jest to człon NIEZALEŻNY od detektora
 * przezroczystości — §9 zapisuje `WE` jako iloczyn. Niezależność kupuje
 * przepustowość: byte-enable pozwala pominąć CAŁE SŁOWA bez zapisu do BRAM
 * i to jest powód, dla którego `TRANSPARENT` jest szybszy od `REPEAT($00)`.
 *
 * Licznik przed maską, bo inaczej sprajt z PAT != %1111 wygląda jak dziury
 * w pokryciu i niezmiennik "dokładnie 1" przestaje cokolwiek dowodzić.
 * Maska jest ostatnia właśnie po to, żeby dało się ją odjąć przełącznikiem.
 */
#ifndef MCEL_SPAN_H
#define MCEL_SPAN_H

#include <cstdint>
#include <vector>

#include "mcel_regs.h"

namespace mcel {

/* Cel spanów. y_lo/y_hi to granice pionowe, w których cel w ogóle przyjmuje
 * zapisy — dla bufora pasa (Y0, Y1), dla całej klatki (0, 240). */
class SpanTarget {
public:
    virtual ~SpanTarget() {}
    virtual int  y_lo() const = 0;
    virtual int  y_hi() const = 0;
    virtual void put(int y, int x, uint8_t v) = 0;
};

/* Płaski bufor 320×240 — tryb referencyjny (E0.4). */
class FrameTarget : public SpanTarget {
public:
    explicit FrameTarget(uint8_t *buf) : buf_(buf) {}
    int  y_lo() const override { return 0; }
    int  y_hi() const override { return SCREEN_H; }
    void put(int y, int x, uint8_t v) override { buf_[y * SCREEN_W + x] = v; }
private:
    uint8_t *buf_;
};

struct CoverViolation {
    int      x, y;
    int      sprite;
    unsigned count;
};

struct SpanStats {
    uint64_t spans          = 0;
    uint64_t pixels_covered = 0;   /* po przycięciu, przed przezroczystością  */
    uint64_t pixels_transp  = 0;   /* odrzucone przez detektor przezroczystości */
    uint64_t pixels_rle_skip = 0;  /* odrzucone przez byte-enable dekodera RLE  */
    uint64_t pixels_masked  = 0;   /* odrzucone przez maskę siatki (D42)      */
    uint64_t pixels_written = 0;
    unsigned cover_max      = 0;   /* maks. pokrycie jednego piksela przez JEDEN sprajt */
    std::vector<CoverViolation> violations;
};

class SpanWriter {
public:
    SpanWriter();

    void set_target(SpanTarget *t) { target_ = t; }

    /* Zerowanie map weryfikacyjnych — raz na klatkę. */
    void begin_frame();

    /* Kontekst sprajta. Ustawiany raz na (sprajt, pas); mapa pokrycia
     * rozpoznaje sprajta po indeksie, więc akumuluje się przez pasy.
     *
     * `transp_ref` to Derived::transp_ref() — indeks 0 w palecie efektywnej
     * (D72). Sprajt nieprzezroczysty to sprajt, którego tekstura nie używa
     * tego indeksu. */
    void begin_sprite(int idx, uint8_t transp_ref, uint8_t pat);

    /* Jedyne wejście do bufora obrazu w całym modelu.
     * `rle_skip` = byte-enable z dekodera pakietów: span jest geometrycznie
     * pokryty (więc liczy się do D14), ale nie wolno go zapisać. */
    void emit(int y, int x0, int x1, uint8_t texel, bool rle_skip = false);

    /* Przełączniki weryfikacyjne (D14, D42). */
    bool mask_enabled   = true;   /* false → maska siatki odjęta            */
    bool count_coverage = true;

    const SpanStats &stats() const { return st_; }
    const std::vector<uint32_t> &write_map() const { return write_map_; }
    /* Ile pikseli pokrył sprajt i w tej klatce (suma po pasach). */
    uint32_t sprite_pixels(int i) const { return spr_pixels_[i]; }

private:
    SpanTarget *target_ = nullptr;

    int      spr_        = -1;
    uint8_t  transp_ref_ = 0;
    uint8_t  pat_        = 0x0F;

    std::vector<int32_t>  cov_owner_;   /* który sprajt ostatnio pokrył piksel */
    std::vector<uint16_t> cov_count_;   /* ile razy — w obrębie TEGO sprajta   */
    std::vector<uint32_t> write_map_;   /* rzeczywiste zapisy, wszystkie sprajty */
    std::vector<uint32_t> spr_pixels_;

    SpanStats st_;
};

} /* namespace mcel */

#endif /* MCEL_SPAN_H */
