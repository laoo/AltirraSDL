#include "mcel_span.h"

#include <algorithm>

namespace mcel {

SpanWriter::SpanWriter()
    : cov_owner_(SCREEN_W * SCREEN_H, -1),
      cov_count_(SCREEN_W * SCREEN_H, 0),
      write_map_(SCREEN_W * SCREEN_H, 0),
      spr_pixels_(SPR_COUNT, 0) {}

void SpanWriter::begin_frame() {
    std::fill(cov_owner_.begin(), cov_owner_.end(), -1);
    std::fill(cov_count_.begin(), cov_count_.end(), 0);
    std::fill(write_map_.begin(), write_map_.end(), 0);
    std::fill(spr_pixels_.begin(), spr_pixels_.end(), 0);
    st_ = SpanStats();
}

void SpanWriter::begin_sprite(int idx, uint8_t transp_ref, uint8_t pat) {
    spr_        = idx;
    transp_ref_ = transp_ref;
    pat_        = uint8_t(pat & 0x0F);
}

void SpanWriter::emit(int y, int x0, int x1, uint8_t texel, bool rle_skip) {
    if (!target_) return;

    /* --- stopień 1: przycięcie ------------------------------------------
     * Pionowo do okna celu (pas albo cała klatka), poziomo do ekranu.
     * Nadmiarowe zakresy z solvera §4 są bezpieczne — kosztują takty,
     * nie poprawność — więc przycięcie jest tu, a nie w rasteryzatorze. */
    if (y < target_->y_lo() || y >= target_->y_hi()) return;
    if (y < 0 || y >= SCREEN_H) return;

    x0 = std::max(x0, 0);
    x1 = std::min(x1, SCREEN_W);
    if (x0 >= x1) return;

    st_.spans++;

    /* Maska siatki 2×2 (D42). Bity w kolejności czytania, MSB od lewego
     * górnego rogu kwadratu:
     *
     *                 x parzyste   x nieparzyste
     *   y parzyste        b3            b2
     *   y nieparzyste     b1            b0
     *
     * y jest LOGICZNYM wierszem bufora pasa, ale STRIP_H = 24 jest parzyste,
     * więc (y mod 24) & 1 == y & 1 — wzór jest ten sam w obu trybach i to
     * właśnie dlatego strip ≡ cała klatka zachodzi także z siatką. */
    const int row_bit_base = 2 * (y & 1);

    for (int x = x0; x < x1; ++x) {
        /* --- stopień 2: licznik pokrycia (D14) — PRZED przezroczystością
         * i PRZED maską. Mapa jest "per sprajt": piksel przypisany innemu
         * sprajtowi startuje od nowa, więc niezmiennik "dokładnie 1"
         * dotyczy jednego sprajta, tak jak w D14, a nie sumy warstw. */
        if (count_coverage) {
            const int p = y * SCREEN_W + x;
            if (cov_owner_[p] != spr_) { cov_owner_[p] = spr_; cov_count_[p] = 1; }
            else                       { cov_count_[p]++; }
            const unsigned c = cov_count_[p];
            if (c > st_.cover_max) st_.cover_max = c;
            if (c > 1 && st_.violations.size() < 64)
                st_.violations.push_back(CoverViolation{ x, y, spr_, c });
            if (c == 1 && spr_ >= 0 && spr_ < SPR_COUNT) spr_pixels_[spr_]++;
            st_.pixels_covered++;
        }

        /* --- stopień 3: byte-enable z dekodera RLE ------------------------
         * Pakiet TRANSPARENT niesie BRAK ZAPISU, nie wartość. Człon jest
         * niezależny od detektora (§10) i liczony osobno, żeby statystyka
         * mówiła, KTÓRY człon odrzucił piksel. */
        if (rle_skip) { st_.pixels_rle_skip++; continue; }

        /* --- stopień 4: detektor przezroczystości -------------------------
         * W RTL to komparator ośmiobitowy generujący byte-enable: bez odczytu
         * tła i bez porównywania w pętli. Odniesieniem jest indeks 0 w palecie
         * efektywnej — przy 8 bpp $00, przy 4 bpp {PAL, 0000} (D72). */
        if (texel == transp_ref_) {
            st_.pixels_transp++;
            continue;
        }

        /* --- stopień 5: maska siatki (D42), ostatni przed zapisem -------- */
        if (mask_enabled) {
            const int bit = 3 - (row_bit_base + (x & 1));
            if (!((pat_ >> bit) & 1)) { st_.pixels_masked++; continue; }
        }

        /* --- stopień 6: zapis ------------------------------------------- */
        target_->put(y, x, texel);
        write_map_[y * SCREEN_W + x]++;
        st_.pixels_written++;
    }
}

} /* namespace mcel */
