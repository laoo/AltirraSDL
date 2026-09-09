/* mcel_collide.cpp — strona $14 okna SPRLIST: viewport (D84) i akcelerator
 * kolizji (D85).
 *
 * Siedem bajtów tuż za rekordami sprajtów. Rekordy kończą się na stronie $13,
 * więc strona $14 była dotąd poza mapą (D76) — te siedem bajtów wyjmuje się
 * z niej i definiuje, reszta zostaje poza mapą jak dotąd.
 *
 * DLACZEGO KOLIZJE LICZĄ SIĘ NA ŻĄDANIE, a nie w prologue:
 *
 *   - `DERIVED` jest pełny (jeden wolny bajt na +61, i on musi być $00)
 *     i porównywany BAJT W BAJT ze wzorcem, więc doklejenie hit-boxa
 *     unieważniłoby `expected.bin` i cały 10-appendix-vectors.md;
 *   - nic się nie prelicza, więc nie ma czego trzymać i nie ma formatu
 *     zapisu — porównanie idzie w pełnej szerokości 12.4, bez nasycania;
 *   - silnik czyta listę ŻYWĄ, więc odpowiedź dotyczy tego, co 6502 właśnie
 *     wpisał, a nie ostatniego przebiegu prologue. Można przesunąć sprajta
 *     i zapytać od razu, bez kasowania `SNAP`;
 *   - sprajt z `DRAW = 0` ma poprawny hit-box, bo `DRAW` jest tu IGNOROWANY
 *     (D85). To jest jedyny powód, dla którego niewidzialne proxy obróconego
 *     sprajta w ogóle działa.
 *
 * Zasoby biorą się z ograniczenia „tylko poza prologue": poza wygaszaniem
 * port B `SPRLIST`, port B `TEXTAB` i dzielarka prologue stoją bezczynnie.
 * Silnik kolizji nie ma własnego niczego — bierze cudze wtedy, gdy właściciel
 * śpi. Dla 6502 reguła jest ta sama, co dla listy: PYTAJ PRZY `SNAP = 1`,
 * bo `SNAP = 1` znaczy dokładnie „prologue nie chodzi".
 */
#include "mcel.h"

namespace mcel {
namespace {

inline uint16_t rd16u_at(const uint8_t *rec, int lo, int hi) {
    return uint16_t((uint16_t)rec[lo] | ((uint16_t)rec[hi] << 8));
}

} /* anon */

/* ------------------------------------------------------------- hit-box --- */

/* Prostokąt kolizji sprajta `i` w przestrzeni ŚWIATA, 12.4.
 *
 * Świata, nie ekranu: viewport (D84) jest translacją, a nakładanie się dwóch
 * prostokątów jest na translację niezmiennicze — więc odjęcie go tutaj nic by
 * nie zmieniło poza dołożeniem dwóch odejmowań i ryzyka przekroczenia zakresu.
 *
 * `$0C`-`$13` to PRZESUNIĘCIA WŁASNYCH ROGÓW sprajta, w tekselach ze znakiem,
 * a nie współrzędne. Wyzerowany rekord daje więc hit-box RÓWNY sprajtowi —
 * ta sama zasada co `DRAW = 0` (D40) i wyzerowana BRAM (D78): zera znaczą
 * rzecz sensowną, a nie brak.
 *
 * Zwraca false, gdy sprajt hit-boxa nie ma — czyli przy opcodzie innym niż
 * BLIT i SCALE. Nie jest to arbitralne: przy ROTSCALE leżą pod tymi stronami
 * `PIVX`/`PIVY`, przy AFFINE `EV`, przy QUAD `EV` i `EW`.
 */
bool Mcel::hitbox_of(int i, Hitbox &hb) const {
    const auto pg = [&](int page) -> uint8_t {
        return sprlist_[(page << 8) | (i & 0xFF)];
    };
    const auto s16 = [&](int lo, int hi) -> int64_t {
        return (int16_t)((uint16_t)pg(lo) | ((uint16_t)pg(hi) << 8));
    };
    const auto u16 = [&](int lo, int hi) -> int64_t {
        return (uint16_t)((uint16_t)pg(lo) | ((uint16_t)pg(hi) << 8));
    };

    const uint8_t opcode = uint8_t(pg(SPR_OPCODE) & 0x0F);   /* D39: maska! */
    if (opcode != OPC_BLIT && opcode != OPC_SCALE) return false;

    const int64_t dstx = s16(SPR_DSTX_LO, SPR_DSTX_HI);
    const int64_t dsty = s16(SPR_DSTY_LO, SPR_DSTY_HI);
    const int64_t hbx0 = s16(SPR_HBX0_LO, SPR_HBX0_HI);
    const int64_t hby0 = s16(SPR_HBY0_LO, SPR_HBY0_HI);
    const int64_t hbx1 = s16(SPR_HBX1_LO, SPR_HBX1_HI);
    const int64_t hby1 = s16(SPR_HBY1_LO, SPR_HBY1_HI);

    /* TEXTAB jest potrzebny TAKŻE przy BLIT, bo prawy dolny róg to róg
     * tekstury: W i H są tylko w deskryptorze. Ścieżki BLIT i SCALE różnią
     * się przez to skalowaniem, a nie zestawem dostępów. */
    const uint8_t *tex = tex_rec(pg(SPR_TEXID));
    const int64_t W = int64_t(rd16u_at(tex, TEX_WIDTH_LO,  TEX_WIDTH_HI))  + 1;
    const int64_t H = int64_t(rd16u_at(tex, TEX_HEIGHT_LO, TEX_HEIGHT_HI)) + 1;

    if (opcode == OPC_BLIT) {
        /* Skala 1:1, więc przesunięcia wchodzą wprost — teksel to piksel.
         * `W << FIELD_FRAC` wynosi rozmiar tekstury z tekseli do 12.4. */
        hb.x0 = dstx + hbx0;
        hb.y0 = dsty + hby0;
        hb.x1 = dstx + (W << FIELD_FRAC) + hbx1;
        hb.y1 = dsty + (H << FIELD_FRAC) + hby1;
        return true;
    }

    /* SCALE: przesunięcia są PRZED skalowaniem, więc trzeba je przeskalować
     * tym samym współczynnikiem, co sprajta. `sx` to dokładnie `HDX`, które
     * liczy prologue (§4.2) — ten sam kształt i to samo `div_d33` (D33), żeby
     * wynik był bit w bit ten sam po obu stronach.
     *
     * `sx` jest w 16.16, `hbx0` w 12.4, a wynik ma być w 12.4 — stąd
     * `>> FRAC_BITS`, a nie `>> FIELD_SH`.
     *
     * Prawy dolny róg to `DSTX + DSTW`, bo `DSTW` jest już w jednostkach
     * DOCELOWYCH (12.4 pikseli ekranu) — skalowaniu podlega tylko poprawka. */
    const int64_t dstw = u16(SPR_DSTW_LO, SPR_DSTW_HI);
    const int64_t dsth = u16(SPR_DSTH_LO, SPR_DSTH_HI);
    const int64_t sx = div_d33(dstw << FIELD_SH, W);
    const int64_t sy = div_d33(dsth << FIELD_SH, H);

    hb.x0 = dstx + ((hbx0 * sx) >> FRAC_BITS);
    hb.y0 = dsty + ((hby0 * sy) >> FRAC_BITS);
    hb.x1 = dstx + dstw + ((hbx1 * sx) >> FRAC_BITS);
    hb.y1 = dsty + dsth + ((hby1 * sy) >> FRAC_BITS);
    return true;
}

/* Test półotwarty: stykające się bokiem prostokąty NIE kolidują.
 *
 * Prostokąt pusty (`x1 <= x0` albo `y1 <= y0`) nie koliduje nigdy i musi być
 * sprawdzony OSOBNO: bez tego zdegenerowany prostokąt leżący wewnątrz drugiego
 * zgłaszałby trafienie, bo `a.x0 < b.x1 && b.x0 < a.x1` jest wtedy prawdą. */
bool Mcel::hb_overlap(const Hitbox &a, const Hitbox &b) {
    if (a.x1 <= a.x0 || a.y1 <= a.y0) return false;
    if (b.x1 <= b.x0 || b.y1 <= b.y0) return false;
    return a.x0 < b.x1 && b.x0 < a.x1 &&
           a.y0 < b.y1 && b.y0 < a.y1;
}

/* --------------------------------------------------------------- skan ---- */

/* Automat z D85. Para to skan o zerowej długości (`COLBMAX <= COLB`), więc
 * trybów nie ma dwóch — jest jeden.
 *
 *     testuj A kontra B
 *     trafienie      -> stop, FOUND = 1, COLB niesie indeks trafionego
 *     B <  COLBMAX   -> B := B + 1, dalej
 *     B >= COLBMAX   -> stop, FOUND = 0
 *
 * `B == A` jest POMIJANE, a nie zgłaszane jako trafienie: bez tego pętla
 * „gracz kontra wszyscy" musiałaby mieć `cpx`/`beq` w środku, czyli ~1 000
 * cykli na klatkę za jeden komparator w sprzęcie.
 *
 * Warunek `B < COLBMAX` sam wyklucza zawinięcie przy `B = 255` — licznik nie
 * ma jak przekroczyć bajta.
 */
void Mcel::col_scan() {
    col_status_ = 0;

    Hitbox a;
    if (!hitbox_of(col_a_, a) || a.x1 <= a.x0 || a.y1 <= a.y0) {
        /* A nie bierze udziału w kolizjach — nic nie może go trafić, więc
         * skan kończy się od razu. NOBOX leży przy NOTREADY = 0 CELOWO: to
         * jest odpowiedź POPRAWNA („nie koliduje"), tylko z podaną przyczyną.
         * Gdyby zgłaszała się jako błąd w b7, pętla `bmi` wisiałaby
         * w nieskończoność na sprajcie obróconym. */
        col_status_ = COL_NOBOX;
        return;
    }

    int candidates = 0;
    for (;;) {
        ++candidates;
        if (col_b_ != col_a_) {
            Hitbox b;
            if (hitbox_of(col_b_, b) && hb_overlap(a, b)) {
                col_status_ = COL_FOUND;
                break;
            }
        }
        if (col_b_ < col_bmax_) ++col_b_;
        else break;
    }

    /* [model] NOTREADY nigdy nie jest tu widziane jako 1: model liczy skan
     * natychmiast i atomowo, tak samo jak nie pokazuje BUSY (D56). Program,
     * który pominie pętlę `bmi`, przejdzie więc w modelu, a na sprzęcie
     * przeczyta odpowiedź z poprzedniego zapytania. Model tego nie odtworzy —
     * może za to powiedzieć, i mówi wtedy, gdy skan nie zmieściłby się
     * w luce między `sta COLB` a najwcześniejszym odczytem. */
    if (candidates * COL_CAND_CYCLES > COL_READY_BUDGET)
        warn("skan kolizji obejmuje wiecej kandydatow, niz zdazy sie policzyc "
             "miedzy 'sta COLB' a najwczesniejszym odczytem — na sprzecie "
             "odpowiedzi NIE BYLOBY jeszcze gotowej. Petla 'bit COLA / bmi' "
             "jest OBOWIAZKOWA (D85). [tekst bez liczby kandydatow, zeby "
             "powtorzenia sie sklejaly]");
}

/* ------------------------------------------- strona $14: zapis i odczyt --- */

/* COLA i COLBMAX są ZATRZASKAMI; skan startuje WYŁĄCZNIE zapis COLB. Gdyby
 * startował też zapis COLA, ustawienie sprajta odniesienia ruszałoby przemiat
 * po nieaktualnym B. Kolejność jest przez to ustalona: COLA i COLBMAX przed,
 * COLB na końcu — i tylko on w pętli.
 *
 * Zapis COLB PRZERYWA skan w toku i startuje nowy. Dzięki temu podwójny zapis
 * NMOS-owego `inc COLB` jest tylko marnotrawstwem, a nie stanem nieokreślonym
 * — ale `inc` i tak zostaje zakazane, bo spod COLA czyta się status, a nie to,
 * co się tam wpisało. */
void Mcel::sprwin_wr(uint16_t off, uint8_t v) {
    const auto lo = [](int16_t o, uint8_t b) {
        return int16_t((uint16_t(o) & 0xFF00) | uint16_t(b));
    };
    const auto hi = [](int16_t o, uint8_t b) {
        return int16_t((uint16_t(o) & 0x00FF) | (uint16_t(b) << 8));
    };
    switch (off) {
    case SPRWIN_VPX_LO:  vpx_ = lo(vpx_, v); return;
    case SPRWIN_VPX_HI:  vpx_ = hi(vpx_, v); return;
    case SPRWIN_VPY_LO:  vpy_ = lo(vpy_, v); return;
    case SPRWIN_VPY_HI:  vpy_ = hi(vpy_, v); return;
    case SPRWIN_COLA:    col_a_    = v; return;   /* zatrzask */
    case SPRWIN_COLBMAX: col_bmax_ = v; return;   /* zatrzask */
    case SPRWIN_COLB:    col_b_    = v; col_scan(); return;
    default: return;
    }
}

/* Odczyt. Viewport i COLBMAX oddają to, co się w nie wpisało; COLB oddaje
 * indeks BIEŻĄCY (po trafieniu — indeks trafionego), a COLA — status.
 * Tylko COLA jest przez to rejestrem asymetrycznym, tak samo jak CTL/STATUS
 * (D60), i z tego samego powodu obowiązuje na nim zakaz RMW. */
uint8_t Mcel::sprwin_rd(uint16_t off) const {
    switch (off) {
    case SPRWIN_VPX_LO:  return uint8_t(uint16_t(vpx_));
    case SPRWIN_VPX_HI:  return uint8_t(uint16_t(vpx_) >> 8);
    case SPRWIN_VPY_LO:  return uint8_t(uint16_t(vpy_));
    case SPRWIN_VPY_HI:  return uint8_t(uint16_t(vpy_) >> 8);
    case SPRWIN_COLA:    return col_status_;
    case SPRWIN_COLB:    return col_b_;
    case SPRWIN_COLBMAX: return col_bmax_;
    default: return 0xFF;
    }
}

} /* namespace mcel */
