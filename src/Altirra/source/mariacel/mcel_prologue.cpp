/* mcel_prologue.cpp — przebieg wstępny (04-algorithms §2, §3).
 *
 * Uruchamiany TYLKO gdy 6502 skasował semafor SNAP. Czyta SPRLIST (BRAM, D46)
 * i TEXTAB (BRAM, D58), liczy DERIVED i STRIPMAP (BRAM, D52), na końcu
 * SNAP = 1. Na SDRAM nie robi ANI JEDNEJ transakcji — ani w tę, ani w tamtą
 * stronę.
 *
 * Prologue jest punktem przekazania własności listy. Przez resztę klatki
 * SPRLIST nie jest w ogóle czytany.
 *
 * Zakres E0.1–E0.5: setup tylko dla OPCODE = 0 (BLIT). Pozostałe opcody mają
 * jawne stuby, żeby model nie renderował po cichu czegoś innego niż deklaruje.
 */
#include "mcel_window.h"

#include <algorithm>
#include <cstring>

namespace mcel {
namespace {

inline int16_t rd16s(const uint8_t *rec, int lo, int hi) {
    return (int16_t)((uint16_t)rec[lo] | ((uint16_t)rec[hi] << 8));
}
inline uint16_t rd16u(const uint8_t *rec, int lo, int hi) {
    return (uint16_t)((uint16_t)rec[lo] | ((uint16_t)rec[hi] << 8));
}

} /* anon */

/* --------------------------------------------------------------- setup --- */

bool Mcel::setup_opcode(int i, const uint8_t rec[SPR_REC_SZ],
                        const uint8_t tex[TEX_REC_SZ], Derived &d,
                        int64_t &cycles) {
    (void)i;
    const uint8_t opcode = uint8_t(rec[SPR_OPCODE] & 0x0F);   /* D39: maska! */
    /* DSTX/DSTY sa int16 12.4 (D83) — jak reszta geometrii rekordu. */
    const int16_t dstx   = rd16s(rec, SPR_DSTX_LO, SPR_DSTX_HI);
    const int16_t dsty   = rd16s(rec, SPR_DSTY_LO, SPR_DSTY_HI);

    /* W = WIDTH+1, H = HEIGHT+1 — deskryptor trzyma rozmiar minus jeden. */
    d.src_w = uint16_t(rd16u(tex, TEX_WIDTH_LO,  TEX_WIDTH_HI)  + 1);
    d.src_h = uint16_t(rd16u(tex, TEX_HEIGHT_LO, TEX_HEIGHT_HI) + 1);
    d.src_stride = rd16u(tex, TEX_STRIDE_LO, TEX_STRIDE_HI);
    d.src_base   = (uint32_t)tex[TEX_BASE_0] |
                   ((uint32_t)tex[TEX_BASE_1] << 8) |
                   ((uint32_t)tex[TEX_BASE_2] << 16);   /* A[24:1], D37 */

    switch (opcode) {
    case OPC_BLIT:
        /* Zero mnożeń, zero dzieleń. */
        d.hdx = FX_ONE;  d.hdy = 0;
        d.vdx = 0;       d.vdy = FX_ONE;
        d.xpos = fx_field(dstx);
        d.ypos = fx_field(dsty);
        return true;

    case OPC_SCALE: {
        /* HDX to PIKSELE EKRANU NA TEKSEL, więc dzielimy rozmiar docelowy
         * przez źródłowy, a nie odwrotnie. 2 dzielenia.
         *
         * DSTW/DSTH są uint16 12.4 (D83), więc "<< FIELD_SH" wynosi je do
         * 16.16 — dokładnie ten sam kształt co AFFINE niżej. Dzielenie jest
         * CAŁKOWITE (nie fx_div): argument jest już przeskalowany, więc
         * przesunięcie występuje dokładnie raz. */
        const uint16_t dstw = rd16u(rec, SPR_DSTW_LO, SPR_DSTW_HI);
        const uint16_t dsth = rd16u(rec, SPR_DSTH_LO, SPR_DSTH_HI);
        d.hdx = fx_sat32(div_d33(int64_t(dstw) << FIELD_SH, (int64_t)d.src_w));  d.hdy = 0;
        d.vdx = 0;
        d.vdy = fx_sat32(div_d33(int64_t(dsth) << FIELD_SH, (int64_t)d.src_h));
        d.xpos = fx_field(dstx);
        d.ypos = fx_field(dsty);
        cycles += 2;                    /* dzielnik potokowany: 1 wynik/takt */
        return true;
    }

    case OPC_ROTSCALE: {
        /* 6 mnożeń, ZERO dzieleń. Kąt rośnie zgodnie z ruchem wskazówek
         * zegara (oś y skierowana w dół). */
        const uint8_t  angle = rec[SPR_ANGLE];
        const int32_t  sx = int32_t(rd16u(rec, SPR_SCALEX_LO, SPR_SCALEX_HI)) << 8;  /* 8.8 → 16.16 */
        const int32_t  sy = int32_t(rd16u(rec, SPR_SCALEY_LO, SPR_SCALEY_HI)) << 8;

        /* D35: przy ANGLE[5:0] == 0 stałe ±1.0 zamiast odczytu z tablicy.
         * Bez tego ROTSCALE(0°, 1.0) dałby HDX = 65534 i detekcja MODE = LINE
         * by nie zadziałała. */
        const SinCos k = sincos_1_15(angle);

        /* Minus wchodzi w OPERAND, nie w wynik: VDX = (sy · (-sa)) >> SIN_FRAC.
         * To ma znaczenie, bo ">>" jest podłogą, więc -(x >> n) i (-x) >> n
         * różnią się o 1 LSB dla x niepodzielnych. W RTL zanegowanie wyjścia
         * tablicy jest darmowe, więc taka forma jest i tańsza, i jednoznaczna. */
        d.hdx = (fixed_t)((int64_t(sx) *  k.ca) >> SIN_FRAC);
        d.hdy = (fixed_t)((int64_t(sx) *  k.sa) >> SIN_FRAC);
        d.vdx = (fixed_t)((int64_t(sy) * -k.sa) >> SIN_FRAC);
        d.vdy = (fixed_t)((int64_t(sy) *  k.ca) >> SIN_FRAC);

        /* DSTX/DSTY to pozycja PIVOTU na ekranie, nie lewego górnego rogu.
         * Pivot to ŚRODEK TEKSTURY + (PIVX, PIVY), wszystko w 12.4 (D82).
         * Pola trybu nie ma — środek jest jedynym początkiem.
         *
         * Środek w 12.4 to W << 3, więc przy NIEPARZYSTYM W wypada dokładnie
         * w połowie teksela; dzielenia całkowitego, które gubiło pół piksela,
         * tu nie ma.
         *
         * px i py są ze znakiem i szersze niż pole: środek sięga W << 3, czyli
         * 2^19 przy W = 65536, a PIVX dokłada ±2^15. To razem 21 bitów ze
         * znakiem — pole jest 16-bitowe, akumulator nie. */
        const int64_t px = (int64_t(d.src_w) << 3) + rd16s(rec, SPR_PIVX_LO, SPR_PIVX_HI);
        const int64_t py = (int64_t(d.src_h) << 3) + rd16s(rec, SPR_PIVY_LO, SPR_PIVY_HI);

        /* Suma PRZED przesunięciem, jeden shift na oś. ">>" jest podłogą, więc
         * przesunięcie każdego składnika osobno różniłoby się o 1 LSB dla sum
         * niepodzielnych — ta sama pułapka co minus w operandzie wyżej. */
        d.xpos = fx_sat32(int64_t(fx_field(dstx)) - ((px * d.hdx + py * d.vdx) >> FIELD_FRAC));
        d.ypos = fx_sat32(int64_t(fx_field(dsty)) - ((px * d.hdy + py * d.vdy) >> FIELD_FRAC));
        return true;
    }

    case OPC_AFFINE: {
        /* EU/EV to int16 w formacie 12.4, więc "<< FIELD_SH" wynosi je do 16.16;
         * podzielenie przez W/H daje krok na jeden teksel. 2 dzielenia.
         * Dzielenie CAŁKOWITE (nie fx_div) — argument jest już przeskalowany. */
        const int16_t eux = rd16s(rec, SPR_EUX_LO, SPR_EUX_HI);
        const int16_t euy = rd16s(rec, SPR_EUY_LO, SPR_EUY_HI);
        const int16_t evx = rd16s(rec, SPR_EVX_LO, SPR_EVX_HI);
        const int16_t evy = rd16s(rec, SPR_EVY_LO, SPR_EVY_HI);

        d.hdx = fx_sat32(div_d33(int64_t(eux) << FIELD_SH, (int64_t)d.src_w));
        d.hdy = fx_sat32(div_d33(int64_t(euy) << FIELD_SH, (int64_t)d.src_w));
        d.vdx = fx_sat32(div_d33(int64_t(evx) << FIELD_SH, (int64_t)d.src_h));
        d.vdy = fx_sat32(div_d33(int64_t(evy) << FIELD_SH, (int64_t)d.src_h));
        d.xpos = fx_field(dstx);
        d.ypos = fx_field(dsty);
        cycles += 4;
        return true;
    }

    case OPC_QUAD: {
        /* Jak AFFINE, plus czwarty rog. Wszystkie trzy rogi poza P0 podaje sie
         * TAK SAMO — jako wektor od P0 (D45):
         *     P1 = P0 + EU    P3 = P0 + EV    P2 = P0 + EW
         * a czlon drugiego rzedu blitter liczy sam:
         *     HDD = EW - EU - EV
         * bo W·HDX = EUX<<12, H·VDX = EVX<<12 i W·H·HDDX = HDDX_pole<<12,
         * wiec P2 = P0 + EU + EV + HDD. */
        const int16_t eux = rd16s(rec, SPR_EUX_LO, SPR_EUX_HI);
        const int16_t euy = rd16s(rec, SPR_EUY_LO, SPR_EUY_HI);
        const int16_t evx = rd16s(rec, SPR_EVX_LO, SPR_EVX_HI);
        const int16_t evy = rd16s(rec, SPR_EVY_LO, SPR_EVY_HI);
        const int16_t ewx = rd16s(rec, SPR_EWX_LO, SPR_EWX_HI);
        const int16_t ewy = rd16s(rec, SPR_EWY_LO, SPR_EWY_HI);

        /* SZERZEJ NIZ 16 BITOW: EU, EV i EW sa int16 12.4 (±2048 px), ale ich
         * roznica siega ±6144 px i w int16 sie nie miesci. Obciecie tutaj
         * popsuloby po cichu mocno wygiete czworoboki (D45). */
        const int32_t hddx_f = int32_t(ewx) - int32_t(eux) - int32_t(evx);
        const int32_t hddy_f = int32_t(ewy) - int32_t(euy) - int32_t(evy);

        d.hdx = fx_sat32(div_d33(int64_t(eux) << FIELD_SH, (int64_t)d.src_w));
        d.hdy = fx_sat32(div_d33(int64_t(euy) << FIELD_SH, (int64_t)d.src_w));
        d.vdx = fx_sat32(div_d33(int64_t(evx) << FIELD_SH, (int64_t)d.src_h));
        d.vdy = fx_sat32(div_d33(int64_t(evy) << FIELD_SH, (int64_t)d.src_h));

        const int64_t wh = int64_t(d.src_w) * int64_t(d.src_h);
        d.hddx = fx_sat32(div_d33(int64_t(hddx_f) << FIELD_SH, wh));
        d.hddy = fx_sat32(div_d33(int64_t(hddy_f) << FIELD_SH, wh));

        d.xpos = fx_field(dstx);
        d.ypos = fx_field(dsty);
        cycles += (d.hddy ? 6 : 4);
        return true;
    }

    default:
        warn("OPCODE " + std::to_string(opcode) + ": nieznany opcode (5-15 wolne)");
        stats_.unsupported_opcode++;
        return false;
    }
}

/* -------------------------------------------------------------- viewport -- */

/* D84 — okno wyświetlania w przestrzeni renderingu.
 *
 * `VPX`/`VPY` to pozycja piksela widocznego w LEWYM GÓRNYM ROGU ekranu, więc
 * do geometrii wchodzą ze znakiem minus. Żeby punkt (0,0) przestrzeni wypadł
 * na środku ekranu: `VPX = -160*16`, `VPY = -120*16`.
 *
 * Krok stoi MIĘDZY `setup_opcode()` a `apply_flip()` i jest JEDEN dla
 * wszystkich pięciu opcodów. Wolno tak, bo przesunięcie okna jest translacją,
 * a `apply_flip()` — który dodaje `W*HDX` i neguje kroki — jest na translację
 * odporny. Przy ROTSCALE odejmuje się to od pozycji PIVOTU; sam pivot,
 * skala i kąt zostają nietknięte.
 *
 * Różnica `DSTX - VPX` sięga ±4096 px i w polu 16-bitowym się nie mieści,
 * więc liczy się ją w akumulatorze szerszym niż pole — ta sama zasada, co
 * `px`/`py` przy ROTSCALE (D82).
 *
 * Konsekwencja dla rasteryzatora: ŻADNA. `DERIVED` i `STRIPMAP` są już
 * w przestrzeni ekranu, więc bbox (§4.7) odcina sprajty wyjechane za kamerę
 * sam — i to jest powód, dla którego viewport MUSI być tutaj, a nie w torze
 * wideo: `STRIPMAP` zależy od `Y`.
 *
 * Uwaga o ułamkowym viewporcie: `frac(XPOS) != 0` wyklucza `MODE = LINE`
 * (§4.6), więc `VPX` niebędące wielokrotnością 16 przestawia WSZYSTKIE zwykłe
 * blity na ścieżkę SCALE. Scroll subpikselowy działa i wygląda gładko, ale
 * kosztuje przepustowość rasteryzatora dla całej sceny naraz. */
void Mcel::apply_viewport(Derived &d, int16_t vpx, int16_t vpy) {
    if (!vpx && !vpy) return;           /* najczestszy przypadek — bez zmian */
    d.xpos = fx_sat32(int64_t(d.xpos) - (int64_t(vpx) << FIELD_SH));
    d.ypos = fx_sat32(int64_t(d.ypos) - (int64_t(vpy) << FIELD_SH));
}

/* --------------------------------------------------------------- odbicia -- */

/* Czworokąt docelowy zostaje bez zmian, lustrzana jest tylko zawartość.
 * HFLIP = FLIP.b7, VFLIP = FLIP.b6 (D41 — flagi wypełniają bajt od góry). */
void Mcel::apply_flip(Derived &d, bool hflip, bool vflip) {
    const int64_t W = d.src_w, H = d.src_h;
    if (hflip) {
        d.xpos = fx_sat32(int64_t(d.xpos) + W * d.hdx);
        d.ypos = fx_sat32(int64_t(d.ypos) + W * d.hdy);
        d.hdx  = -d.hdx;  d.hdy = -d.hdy;
    }
    if (vflip) {
        d.xpos = fx_sat32(int64_t(d.xpos) + H * d.vdx);
        d.ypos = fx_sat32(int64_t(d.ypos) + H * d.vdy);
        d.vdx  = -d.vdx;  d.vdy = -d.vdy;
    }
}

/* -------------------------------------------------------- macierz odwrotna */

bool Mcel::inverse_matrix(Derived &d) {
    /* det = (HDX·VDY − HDY·VDX) >> 16 — wszystko na modułach ze znakiem
     * doklejanym po (D33), bo dzielnik restoring liczy tak samo. */
    const int64_t det64 = ((int64_t)d.hdx * d.vdy - (int64_t)d.hdy * d.vdx) >> FRAC_BITS;
    if (det64 == 0) { stats_.degenerate++; return false; }
    const int32_t det = fx_sat32(det64);

    d.ia = fx_div( d.vdy, det);
    d.ib = fx_div(-(int64_t)d.vdx, det);
    d.ic = fx_div(-(int64_t)d.hdy, det);
    d.id = fx_div( d.hdx, det);

    /* RCP_* — solver zakresów r/c (§4) czyta je z rejestru, nie ze stałej (D25).
     * P7 (format stałoprzecinkowy RCP_*) jest OTWARTE i rozstrzygnie się
     * empirycznie przy E6. Do tego czasu model trzyma je w 16.16, tak jak
     * resztę geometrii — i nic z nich jeszcze nie czyta, bo ścieżka LINE
     * ma własne, trywialne zakresy (§6). */
    /* Złożenie macierzy odwrotnej z prostą — powinno dać tożsamość.
     * IA..ID są kwantowane do FRAC_BITS bitów ułamkowych, więc na ogół nie
     * dają jej dokładnie; residuum mierzy, o ile. Patrz ModelStats. */
    {
        const int32_t ds_c = (int32_t)(((int64_t)d.ia * d.hdx + (int64_t)d.ib * d.hdy) >> FRAC_BITS) - FX_ONE;
        const int32_t dt_r = (int32_t)(((int64_t)d.ic * d.vdx + (int64_t)d.id * d.vdy) >> FRAC_BITS) - FX_ONE;
        const int32_t ds_r = (int32_t)(((int64_t)d.ia * d.vdx + (int64_t)d.ib * d.vdy) >> FRAC_BITS);
        const int32_t dt_c = (int32_t)(((int64_t)d.ic * d.hdx + (int64_t)d.id * d.hdy) >> FRAC_BITS);
        const int32_t all[4] = { ds_c, dt_r, ds_r, dt_c };
        bool exact = true;
        for (int32_t v : all) {
            if (v < stats_.inv_residual_min) stats_.inv_residual_min = v;
            if (v > stats_.inv_residual_max) stats_.inv_residual_max = v;
            if (v) exact = false;
        }
        if (exact) stats_.inv_exact_sprites++;
    }

    d.rcp_hdy = d.hdy ? fx_recip(d.hdy) : 0;
    d.rcp_vdy = d.vdy ? fx_recip(d.vdy) : 0;
    d.rcp_hdx = d.hdx ? fx_recip(d.hdx) : 0;
    return true;
}

/* --------------------------------------------------------- setup QUAD ---- */

/* QUAD nie ma macierzy odwrotnej (§12), więc zamiast IA..ID liczymy tu tylko
 * odwrotności potrzebne solverowi zakresów i nachyleniom krawędzi.
 *
 * Oba warianty z §12 są obsługiwane i różnią się kosztem, nie poprawnością:
 *
 *   HDDY = 0 (trapez)      y(c,r) zostaje afiniczne. §4 działa bez zmiany poza
 *                          `HDX` branym z wiersza; krawędzie boczne mają stałe
 *                          `dy = VDY`, górna i dolna stałe `dy = HDY`, więc
 *                          w pętli wewnętrznej NIE MA ŻADNEGO DZIELENIA.
 *                          4 dzielenia w setupie.
 *
 *   HDDY != 0 (pełny)      `dy` krawędzi bocznej to `VDY + c·HDDY`, czyli
 *                          zmienia się z każdą kolumną → jedna odwrotność
 *                          NA TEKSEL. W §4 wzór na zakres `r` rozgałęzia się
 *                          w punkcie `r* = −HDY/HDDY` (range_r_quad).
 *                          6 dzieleń w setupie, + dzielnik potokowany w RTL. */
bool Mcel::quad_setup(Derived &d) {
    if (d.hdy == 0 && d.vdy == 0 && d.hddy == 0) { stats_.degenerate++; return false; }

    d.ia = d.ib = d.ic = d.id = 0;      /* macierz odwrotna nie istnieje */
    d.rcp_hdy = d.hdy ? fx_recip(d.hdy) : 0;
    d.rcp_vdy = d.vdy ? fx_recip(d.vdy) : 0;
    d.rcp_hdx = d.hdx ? fx_recip(d.hdx) : 0;   /* wiersz 0; dalsze liczy raster */
    return true;
}

/* ----------------------------------------------------- detekcja ścieżki --- */

void Mcel::detect_mode(Derived &d, uint8_t opcode) {
    /* D32: NODETECT jest bitem DIAGNOSTYCZNYM. Bez niego test "AFFINE
     * z wektorami (W,0) i (0,H) daje to samo co BLIT" przechodzi NIE DOTYKAJĄC
     * ścieżki afinicznej, bo detekcja zwija go do MODE = LINE. */
    if (ctl_ & CTL_NODETECT) {
        static const uint8_t from_opcode[5] = {
            MODE_LINE, MODE_SCALE, MODE_AFFINE, MODE_AFFINE, MODE_QUAD
        };  /* ROTSCALE nie ma własnego rasteryzatora — jedzie na AFFINE */
        d.mode = opcode < 5 ? from_opcode[opcode] : MODE_AFFINE;
        /* NODETECT wymusza QUAD takze przy HDD = 0 — i o to wlasnie chodzi.
         * Bez tego skan-konwertera nie da sie uruchomic na ksztalcie, ktorego
         * poprawny wynik znamy skadinad. Uwaga: rownolegloboku NIE narysuje
         * bit w bit tak jak §8, bo regula "domkniete w x" daje ~1 px
         * nachodzenia na granicy teksela — i to jest cecha, nie usterka. */
        return;
    }

    /* EW = EU + EV daje HDD = 0, czyli rownoleglobok — sprajt degeneruje sie
     * do lancucha afinicznego przez zwykla detekcje, bez osobnej reguly (D45).
     * Niezerowe HDD nie da sie zwinac: kazdy teksel jest innym czworobokiem. */
    if (d.hddx || d.hddy) { d.mode = MODE_QUAD; return; }
    if (d.hdx == FX_ONE && d.hdy == 0 && d.vdx == 0 && d.vdy == FX_ONE &&
        fx_frac(d.xpos) == 0 && fx_frac(d.ypos) == 0) {
        d.mode = MODE_LINE;
    } else if (d.hdy == 0 && d.vdx == 0) {
        d.mode = MODE_SCALE;
    } else {
        d.mode = MODE_AFFINE;
    }
}

/* ------------------------------------------------- bbox i zasięg pasów ---- */

void Mcel::bbox_stripmap(int i, const Derived &d) {
    const int64_t W = d.src_w, H = d.src_h;

    /* Cztery rogi czworokąta docelowego (§3). W int64, bo W·HDX przy
     * dużej teksturze wychodzi poza int32. */
    const int64_t p0x = d.xpos,                  p0y = d.ypos;
    const int64_t p1x = p0x + W * d.hdx,         p1y = p0y + W * d.hdy;
    const int64_t p3x = p0x + H * d.vdx,         p3y = p0y + H * d.vdy;
    /* Przy QUAD czwarty rog NIE lezy na rownolegloboku — dochodzi czlon
     * W·H·HDD. Dla pozostalych sciezek HDD jest zerem, wiec wzor jest wspolny
     * i nie trzeba rozgalezienia. */
    const int64_t p2x = p1x + H * d.vdx + W * H * d.hddx;
    const int64_t p2y = p1y + H * d.vdy + W * H * d.hddy;

    const int64_t ymin = std::min(std::min(p0y, p1y), std::min(p2y, p3y)) >> FRAC_BITS;
    const int64_t ymax = std::max(std::max(p0y, p1y), std::max(p2y, p3y)) >> FRAC_BITS;
    const int64_t xmin = std::min(std::min(p0x, p1x), std::min(p2x, p3x)) >> FRAC_BITS;
    const int64_t xmax = std::max(std::max(p0x, p1x), std::max(p2x, p3x)) >> FRAC_BITS;

    if (ymax < 0 || ymin >= SCREEN_H || xmax < 0 || xmin >= SCREEN_W) {
        stripmap_[i][0] = STRIPMAP_INVISIBLE;
        return;
    }
    const int s_min = strip_of((int)clampv<int64_t>(ymin, 0, SCREEN_H - 1));
    const int s_max = strip_of((int)clampv<int64_t>(ymax, 0, SCREEN_H - 1));
    stripmap_[i][0] = uint8_t((s_max << 4) | s_min);
    /* stripmap_[i][1] = DRAW — surowy bajt wzoru, ustawiany przez prologue().
     * Wzór na klatkę liczy SKAN, nie prologue (D42). */
}

/* --------------------------------------------------------------- prologue - */

void Mcel::prologue() {
    Window win;
    win.open(&sdram_, cfg_.vblank_window_cycles, cfg_, &cpu_probe_);

    stats_.prologue_sprites = 0;
    stats_.unsupported_opcode = 0;
    stats_.degenerate = 0;
    stats_.invisible = 0;
    stats_.inv_residual_min = 0;
    stats_.inv_residual_max = 0;
    stats_.inv_exact_sprites = 0;
    for (int k = 0; k < 8; ++k) stats_.mode_count[k] = 0;

    /* D84 — viewport zdejmuje sie RAZ na przebieg, przed petla po sprajtach.
     * Model nie ma jak zobaczyc roznicy, bo prologue() jest atomowe wzgledem
     * magistrali — ale to jest KONTRAKT (10 §4.2b), nie optymalizacja: w RTL-u
     * odczyt per sprajt pozwolilby zmienic VP w polowie listy i rozerwac
     * klatke na dwie. Referencja ma robic to, co spec, takze wtedy, gdy sama
     * nie potrafi zlamania tej reguly pokazac. */
    const int16_t vp_x = vpx_, vp_y = vpy_;

    /* Cała lista, bez SPR_LAST (D48). Sprajt z DRAW = 0 kosztuje osiem
     * odczytów BRAM i ani jednej transakcji SDRAM, więc ograniczanie zakresu
     * rejestrem nie miałoby czego oszczędzić. */
    for (int i = 0; i < SPR_COUNT; ++i) {
        /* SPRLIST jest w BRAM (D46): dostęp swobodny, port bajtowy, bez
         * transpozycji. Adres to (strona << 8) | indeks, czyli dokładnie to
         * A[12:0], pod które pisze 6502.
         *
         * Czytamy najpierw OSIEM stron wspólnych — jest wśród nich OPCODE
         * i DRAW — a resztę dociągamy tylko wtedy, gdy opcode jej wymaga.
         * Przy SDRAM była to opcjonalna optymalizacja, tu jest naturalna. */
        uint8_t rec[SPR_REC_SZ] = { 0 };
        auto read_pages = [&](int from, int to) {
            for (int pg = from; pg < to; ++pg) rec[pg] = sprlist_[(pg << 8) | i];
            win.advance((to - from) * cfg_.sprlist_read_cycles_per_byte);
        };
        read_pages(0, 8);
        stats_.prologue_sprites++;

        const uint8_t draw = rec[SPR_DRAW];
        stripmap_[i][1] = draw;          /* surowy bajt wzoru → BRAM (D42) */

        /* D40: zera znaczą "nie rysuj". Wyzerowana albo niezainicjowana lista
         * daje PUSTY EKRAN, a nie 256 sprajtów w rogu. Oba nibble zerowe →
         * trwale niewidoczny, więc nie ma po co czytać ani reszty rekordu,
         * ani deskryptora. */
        if (draw == 0) {
            stripmap_[i][0] = STRIPMAP_INVISIBLE;
            stats_.invisible++;
            continue;
        }

        read_pages(8, spr_pages_for_opcode(uint8_t(rec[SPR_OPCODE] & 0x0F)));

        /* TEXTAB w BRAM (D58): adres to (TEXID << 4), czyste ciecie bitow,
         * dostep swobodny. Przedtem byl to jedyny burst SDRAM prologue'u. */
        const uint8_t *tex = tex_rec(rec[SPR_TEXID]);
        win.advance(TEX_BRAM_RD_CYCLES);

        Derived d;
        /* D41: flagi per-sprajt wypełniają bajt od góry. Odbicia NIE są polem
         * rekordu DERIVED — wchodzą w geometrię (§2), więc żyją tylko tutaj. */
        const bool hflip = (rec[SPR_FLIP] & FLIP_H) != 0;
        const bool vflip = (rec[SPR_FLIP] & FLIP_V) != 0;

        int64_t setup_cycles = 0;
        bool ok = setup_opcode(i, rec, tex, d, setup_cycles);
        win.advance(cfg_.prologue_setup_cycles + (int)setup_cycles);

        if (ok) {
            /* D84 — jedna reguła dla wszystkich opcodów, przed odbiciami. */
            apply_viewport(d, vp_x, vp_y);
            apply_flip(d, hflip, vflip);
            detect_mode(d, uint8_t(rec[SPR_OPCODE] & 0x0F));
            /* Przy MODE = QUAD macierz odwrotna traci sens (§12) — odwrotnosc
             * odwzorowania biliniowego wymagalaby pierwiastka. Nie liczymy jej
             * ani jej residuum; te cztery slowa rekordu nios HDD. */
            ok = (d.mode == MODE_QUAD) ? quad_setup(d) : inverse_matrix(d);
        }

        if (ok) {

            /* --- paleta (§6, D39) -----------------------------------------
             * PALXOR siedzi w GÓRNYM NIBBLU OPCODE, nie na własnej stronie.
             * Maska pominięta w jednym z dwóch miejsc daje albo złą paletę,
             * albo losowy opcode — dlatego opcode ma tu wszędzie & 0x0F.
             *
             * PAL rządzi też przezroczystością: odniesienie detektora to
             * indeks 0 w palecie efektywnej (D72), wyprowadzane z FORMAT
             * przez Derived::transp_ref(). */
            const uint8_t palofs = uint8_t(tex[TEX_PALOFS] & 0x0F);
            const uint8_t palxor = uint8_t(rec[SPR_OPCODE] >> 4);
            const uint8_t pal    = uint8_t(palofs ^ palxor);
            const uint8_t tfmt   = tex[TEX_FORMAT];
            d.format = uint8_t((pal << 4) | (tfmt & (FMT_RLEW_MASK | FMT_BPP_MASK)));

            bbox_stripmap(i, d);
            stats_.mode_count[d.mode & 7]++;
        } else {
            d.mode = MODE_INVISIBLE;
            stripmap_[i][0] = STRIPMAP_INVISIBLE;
        }

        derived_pack(d, &derived_ram_[size_t(i) * DER_REC_SZ]);
        win.advance(DER_BRAM_WR_CYCLES);
    }

    /* Zadnego zrzutu na koncu przebiegu (D52). DERIVED i STRIPMAP sa juz
     * na miejscu — rasteryzator czyta je z BRAM, a 6502 przez okno (D53).
     * Prologue nie dotyka SDRAM w OGOLE i nie ma portu do kontrolera
     * pamieci (D58). */
    stats_.prologue_cycles = win.used;
    snap_req_ = false;      /* SNAP = 1 — DERIVED jest aktualne */
}

void Mcel::vblank() {
    if (cfg_.trace_enabled) trace_rec(0x04, nullptr, 0);
    if (!enabled()) return;
    /* Jeśli SNAP = 1, DERIVED z poprzedniej klatki obowiązuje dalej
     * i scena statyczna nie kosztuje nic. */
    if (snap_req_) prologue();
}

} /* namespace mcel */
