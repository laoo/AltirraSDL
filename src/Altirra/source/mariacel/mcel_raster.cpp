/* mcel_raster.cpp — pętla po pasach i ścieżka LINE (04-algorithms §4, §6).
 *
 * Kolejność pętli: PASY NA ZEWNĄTRZ, SPRAJTY W ŚRODKU. Dzięki temu każdy
 * piksel powstaje dokładnie raz.
 *
 * Cała ta ścieżka nie wie, dokąd trafiają spany (D23). Tryb "pasami" i tryb
 * "cała klatka naraz" różnią się WYŁĄCZNIE celem spanów i granicami Y0..Y1 —
 * i tylko dlatego ich diff cokolwiek dowodzi.
 */
#include "mcel_ranges.h"
#include "mcel_window.h"

#include <algorithm>
#include <vector>

namespace mcel {

/* ------------------------------------------------- skan STRIPMAP + raster */

/* s < 0 → tryb referencyjny: bez filtra pasa, granice (0, 240). */
void Mcel::scan_and_raster(int s, int Y0, int Y1, Window *win, StripStat *st) {
    /* Skan idzie po CAŁEJ liście, zawsze 256 wpisów (D48). Sprajt martwy
     * jest oznaczony w STRIPMAP jako niewidoczny i kosztuje 2 takty, czyli
     * dokładnie tyle, ile kosztowałoby jego pominięcie zakresem. */
    for (int i = 0; i < SPR_COUNT; ++i) {
        /* Skan STRIPMAP idzie z BRAM, 2 takty na sprajta, zero pasma SDRAM. */
        win->advance(2);
        if (st) st->sprites_scanned++;

        const uint8_t sm   = stripmap_[i][0];
        const uint8_t draw = stripmap_[i][1];
        const int s_min = sm & 0x0F;
        const int s_max = sm >> 4;
        if (s_min > s_max) continue;                  /* niewidoczny */
        if (s >= 0 && (s < s_min || s > s_max)) continue;
        if (st) st->sprites_hit++;

        /* --- wzór siatki na tę klatkę (D42) ------------------------------
         * PAT = DRAW[3:0] ^ (FRAME_ODD ? DRAW[7:4] : 0)
         *
         * Liczy to SKAN, a nie prologue. Prologue przy SNAP = 1 nie chodzi
         * wcale, więc PAT policzone tam zamarzłoby w każdej statycznej
         * scenie — czyli dokładnie tam, gdzie miganie jest najbardziej
         * widoczne. Wyglądałoby to na błąd semafora, a nie błąd wzoru. */
        const uint8_t pat = uint8_t((draw & 0x0F) ^
                                    (frame_odd_ ? uint8_t(draw >> 4) : 0));
        if (pat == 0) {                               /* cull PRZED prefetchem */
            if (st) st->sprites_culled_pat++;
            continue;
        }

        /* Rekord DERIVED wprost z BRAM (D52) — bez prefetchu, bez pierscienia,
         * bez pozycji w arbitrze SDRAM. Adres to i << 6, czyli czyste ciecie
         * bitow; dostep swobodny nic nie kosztuje. */
        const uint8_t *drec = &derived_ram_[size_t(i) * DER_REC_SZ];
        win->advance(DER_BRAM_RD_CYCLES);
        Derived d;
        derived_unpack(drec, d);       /* bajt po bajcie — D37 */

        if (d.mode == MODE_INVISIBLE) continue;

        /* Deadline pasa sprawdzany NA GRANICY SPRAJTA, nigdy w środku:
         * połowicznie narysowany sprajt wygląda znacznie gorzej niż
         * brakujący. Porzucane są sprajty o najwyższych indeksach, czyli
         * rysowane na wierzchu. */
        if (win->limit > 0 && win->expired()) {
            overrun_ = true;                          /* sticky, kasowany OVR_CLR */
            stats_.overrun = true;
            if (st) st->overrun = true;
            break;
        }

        writer_.begin_sprite(i, d.transp_ref(), pat);
        raster(i, d, Y0, Y1, win);
        if (st) st->sprites_drawn++;
    }
}

void Mcel::raster(int i, const Derived &d, int Y0, int Y1, Window *win) {
    if (bbox_fill_mode) { raster_bbox_fill(i, d, Y0, Y1, win); return; }

    switch (d.mode) {
    case MODE_LINE:
        raster_line(i, d, Y0, Y1, win);
        return;
    case MODE_SCALE:
        raster_scale(i, d, Y0, Y1, win);
        return;
    case MODE_AFFINE:
        raster_affine(i, d, Y0, Y1, win);
        return;
    case MODE_QUAD:
        raster_quad(i, d, Y0, Y1, win);
        return;
    default:
        return;
    }
}

/* ------------------------------------- pobranie tekseli ze zrodla (§6) --- */

/* Tor zrodla z 04-algorithms §6 ma dwa opcjonalne stopnie wejsciowe:
 *
 *   SDRAM -> FIFO -> [dekoder RLE] -> [rozpak. 4 bpp] -> barrel shift -> ...
 *
 * i sa one PROSTOPADLE do siebie i do reszty toru. Model odtwarza ten podzial
 * doslownie: fetch_source_bytes() oddaje strumien BAJTOW ZRODLA (surowy albo
 * zdekodowany z pakietow, dekoder o BPP nie wie — D38), a fetch_texels()
 * rozpakowuje go w indeksy 8-bitowe wedlug BPP. Wszystko za tym widzi zawsze
 * gotowe indeksy i o zadnym z tych stopni nie wie. */

/* Zwraca liczbe taktow. `skip[i] != 0` znaczy, ze bajt i jest POMINIETY —
 * w sprzecie byte-enable = 0, u nas niesione osobna maska az do writera. */
int Mcel::fetch_source_bytes(const Derived &d, int r, int skip_bytes, int nbytes,
                             std::vector<uint8_t> &bytes, std::vector<uint8_t> &skip) {
    bytes.assign((size_t)nbytes, 0);
    skip.assign((size_t)nbytes, 0);
    if (nbytes <= 0) return 0;

    const uint32_t B = d.src_base << 1;          /* adres BAJTOWY bazy (D37) */

    if (!d.rle()) {
        /* Surowe: TEXADDR(r, c) = B + r·SRC_STRIDE + skip_bytes.
         * Parzysty jest tylko wskaznik bazowy, nie wiersze — adresy wierszy
         * liczy sumator w pelnej precyzji bajtowej, wiec nieparzysty
         * SRC_STRIDE dziala normalnie. */
        return sdram_.burst_read(B + uint32_t(r) * d.src_stride + uint32_t(skip_bytes),
                                 bytes.data(), (size_t)nbytes);
    }

    /* --- RLE (§10) ----------------------------------------------------------
     * Tablica offsetow jest KONIECZNA, bo rendering pasami restartuje od
     * wiersza r0 i nie da sie dekodowac sekwencyjnie od wiersza 0. Offsety sa
     * BAJTOWE i liczone od B; szerokosc wpisu niesie FORMAT.RLEW (D44). */
    const int W_off = d.rle_width();
    int cycles = 0;

    uint8_t ent[3] = { 0, 0, 0 };
    cycles += sdram_.burst_read(B + uint32_t(r) * uint32_t(W_off), ent, (size_t)W_off);
    uint32_t off = 0;
    for (int b = 0; b < W_off; ++b) off |= uint32_t(ent[b]) << (8 * b);   /* little endian */

    /* Ile bajtow strumienia zajmuje ten wiersz. Dekoder i tak musi przeczytac
     * KAZDY naglowek po kolei — bez nich nie wie, gdzie sa granice pakietow —
     * wiec prefiks wiersza jest czytany w calosci i jednym burstem. Pomijanie
     * samych danych LITERAL przy duzym c0 jest mozliwa optymalizacja RTL;
     * model jej nie bierze i liczy takty na niekorzysc. */
    const uint32_t base = B + off;
    const size_t avail_cap = 4u << 20;
    bool malformed = false;
    std::vector<uint8_t> probe(256);
    size_t scanned = 0, row_len = 0;
    {   /* skan naglowkow bez obciazania licznikow — to samo, co zrobi FIFO */
        size_t p = 0;
        for (;;) {
            if (p >= avail_cap) { malformed = true; break; }
            const uint8_t hdr = sdram_.peek8(base + uint32_t(p));
            ++p;
            const uint8_t type = uint8_t(hdr & RLE_TYPE_MASK);
            const int units = int(hdr & RLE_COUNT_MASK) + 1;
            if (type == RLE_EOL) break;
            if (type == RLE_LITERAL) p += size_t(units);
            else if (type == RLE_REPEAT) p += 1;
        }
        row_len = p;
        scanned = p;
    }
    if (malformed) {
        warn("tekstura RLE: wiersz bez znacznika EOL — strumien pakietow jest "
             "uszkodzony albo FORMAT.RLEW nie zgadza sie z tablica offsetow");
        return cycles;
    }

    std::vector<uint8_t> packets(row_len);
    cycles += sdram_.burst_read(base, packets.data(), row_len);
    (void)scanned;

    /* Dekodujemy od poczatku wiersza; skok na c0 > 0 jest w BAJTACH (§10),
     * a przy 4 bpp dodatkowe zgubienie nibbla robi juz rozpakowywacz. */
    const int want = skip_bytes + nbytes;
    std::vector<uint8_t> full((size_t)want, 0), fskip((size_t)want, 0);
    const RleDecodeResult res =
        rle_decode_row(packets.data(), packets.size(), full.data(), fskip.data(), want);
    if (res.malformed)
        warn("tekstura RLE: pakiet wychodzi poza wiersz — najpewniej zly offset "
             "albo licznik liczony w pikselach zamiast w bajtach (D38)");

    for (int k = 0; k < nbytes; ++k) {
        const int j = skip_bytes + k;
        if (j < res.produced) { bytes[(size_t)k] = full[(size_t)j]; skip[(size_t)k] = fskip[(size_t)j]; }
        else                  { skip[(size_t)k] = 1; }   /* za koncem wiersza */
    }
    return cycles;
}

/* Rozpakowywacz: bajty zrodla -> indeksy 8-bitowe. Jedyne miejsce w modelu,
 * ktore wie o BPP. */
int Mcel::fetch_texels(const Derived &d, int r, int c0, int n,
                       std::vector<uint8_t> &out, std::vector<uint8_t> &out_skip) {
    out.assign((size_t)n, 0);
    out_skip.assign((size_t)n, 0);
    if (n <= 0) return 0;

    std::vector<uint8_t> bytes, skip;

    if (d.bpp_code() == FMT_BPP_8) {
        const int cycles = fetch_source_bytes(d, r, c0, n, bytes, skip);
        out = bytes;
        out_skip = skip;
        return cycles;
    }

    /* --- BPP = 4 ------------------------------------------------------------
     * Skok na c0 > 0 liczy sie w BAJTACH: c0 >> 1, plus zgubienie jednego
     * nibbla, gdy c0 jest nieparzyste. To robi wlasnie ten stopien, nie
     * adresowanie — dlatego FAZA jest osobno od adresu. */
    const int    phase  = c0 & 1;                         /* FAZA(c) = c & 1 */
    const int    nbytes = (phase + n + 1) / 2;
    const int cycles = fetch_source_bytes(d, r, c0 >> 1, nbytes, bytes, skip);

    /* Kolejnosc nibbli — MSB FIRST (D36): teksel o mniejszym c lezy w GORNYM
     * nibblu. Tak wlasnie pakuje PNG 4-bitowy, wiec tex_convert przepisuje
     * bajty bez przestawiania i round-trip jest trywialny do sprawdzenia.
     *
     * Indeks wyjsciowy sklada sie z paleta efektywna, policzona RAZ na sprajta
     * w prologue (PAL = TEXTAB.PALOFS ^ SPRLIST.OPCODE[7:4], D39):
     *   teksel8 = { PAL[3:0], nibbel }
     * Rasteryzator nie wie, ze PALXOR istnieje.
     *
     * Bajt pominiety przez TRANSPARENT to DWA przezroczyste teksele — i to
     * jest dokladnie ta wlasnosc, dla ktorej licznik pakietu liczy bajty,
     * a nie piksele (D38). */
    const uint8_t pal_hi = uint8_t(d.pal() << 4);
    for (int k = 0; k < n; ++k) {
        const int    nib_index = phase + k;
        const size_t bi = (size_t)(nib_index >> 1);
        if (skip[bi]) { out_skip[(size_t)k] = 1; continue; }
        const uint8_t byte = bytes[bi];
        const uint8_t nib  = (nib_index & 1) ? uint8_t(byte & 0x0F) : uint8_t(byte >> 4);
        out[(size_t)k] = uint8_t(pal_hi | nib);
    }
    return cycles;
}

/* ------------------------------------------------------ ścieżka LINE (§6) */

void Mcel::raster_line(int i, const Derived &d, int Y0, int Y1, Window *win) {
    (void)i;
    /* MODE = LINE gwarantuje HDX = 1.0, VDY = 1.0, HDY = VDX = 0 i całkowite
     * XPOS/YPOS — czyli DSTX/DSTY dają się odczytać wprost z rogu.
     *
     * Warunek jest sprawdzany, a nie zakładany, bo CTL.NODETECT potrafi tu
     * skierować sprajta, którego geometria tego nie spełnia — np. BLIT
     * z HFLIP ma HDX = -1.0, a burstowa kopia wiersza nie umie lustrzeć
     * (odbicia idą geometrycznie, §6). Lepiej głośno nie narysować niż po
     * cichu narysować nie to. */
    if (d.hdx != FX_ONE || d.hdy != 0 || d.vdx != 0 || d.vdy != FX_ONE ||
        fx_frac(d.xpos) != 0 || fx_frac(d.ypos) != 0) {
        warn("MODE = LINE przy geometrii, ktora nie jest 1:1 — najpewniej "
             "NODETECT = 1 na sprajcie z HFLIP/VFLIP. Sciezka LINE to burstowa "
             "kopia wiersza i nie umie lustrzec; wyzeruj NODETECT albo uzyj SCALE");
        return;
    }
    const int DSTX = fx_floor(d.xpos);
    const int DSTY = fx_floor(d.ypos);
    const int W = d.src_w, H = d.src_h;

    const int r0 = clampv(Y0 - DSTY, 0, H);
    const int r1 = clampv(Y1 - DSTY, 0, H);
    const int x0 = clampv(DSTX,     0, SCREEN_W);
    const int x1 = clampv(DSTX + W, 0, SCREEN_W);
    if (r0 >= r1 || x0 >= x1) return;

    const int skip = x0 - DSTX;          /* ile tekseli pominąć na początku wiersza */
    const int n    = x1 - x0;

    std::vector<uint8_t> row, rskip;

    for (int r = r0; r < r1; ++r) {
        /* Jeden burst na wiersz źródła. Przy SRC_STRIDE == W cały sprajt
         * dałoby się ciągnąć jednym burstem przez wiele wierszy — to
         * optymalizacja RTL, nie warunek poprawności, więc model liczy
         * przypadek ogólny (STRIDE != WIDTH, czyli podprostokąt atlasu). */
        const int src_cycles = fetch_texels(d, r, skip, n, row, rskip);

        /* Ścieżka BLIT ma DWA sufity i wiąże ten niższy:
         *   SDRAM        2 B/takt → 2 px/takt przy 8 bpp, 4 px/takt przy 4 bpp
         *   port BRAM   32 b/takt → 4 px/takt, zawsze
         *
         * Przy surowych teksturach wiąże SDRAM i port BRAM nie ma znaczenia;
         * przy 4 bpp oba wychodzą na 4 px/takt. Dopiero RLE rozdziela je na
         * dobre: pakiet REPEAT albo TRANSPARENT pokrywa do 64 tekseli przy
         * dwóch bajtach ruchu, więc SDRAM przestaje być wąskim gardłem
         * i zostaje **sufit portu BRAM** (02-architecture §8, §10).
         *
         * Bez tego członu model liczyłby dla teksturowanego tła kilkanaście
         * pikseli na takt i zaniżał OVERRUN dokładnie tam, gdzie RLE ma być
         * używane najczęściej. */
        const int bram_floor = (n + 3) / 4;
        win->advance(src_cycles > bram_floor ? src_cycles : bram_floor);

        /* Span na teksel. Przy MODE = LINE teksel to dokładnie jeden piksel,
         * więc spany mają długość 1 — to nie jest optymalne, ale jest to
         * TA SAMA droga do writera, którą pójdą SCALE, AFFINE i QUAD. */
        const int y = DSTY + r;
        for (int c = 0; c < n; ++c)
            writer_.emit(y, x0 + c, x0 + c + 1, row[(size_t)c], rskip[(size_t)c] != 0);
    }
}

/* ----------------------------------------------------- ścieżka SCALE (§7) */

/* Warunki: HDY == 0 && VDX == 0. Teksel jest prostokątem osiowym.
 *
 * HDX i VDY MOGĄ BYĆ UJEMNE i to jest przypadek dominujący, nie egzotyka:
 * odbicia są realizowane geometrycznie (HDX = -HDX, §2), więc BLIT z HFLIP
 * ma HDX = -1.0 i detekcja kieruje go tutaj, nie do LINE. Dlatego granice
 * prostokąta są PORZĄDKOWANE, a nie zakładane (D43). */
void Mcel::raster_scale(int i, const Derived &d, int Y0, int Y1, Window *win) {
    (void)i;
    const Range rr = range_r(d, Y0, Y1);
    if (rr.empty()) return;

    std::vector<uint8_t> row, rskip;

    for (int r = rr.lo; r < rr.hi; ++r) {
        const Range rc = range_c(d, r, Y0, Y1);
        if (rc.empty()) continue;

        const int64_t yv = int64_t(d.ypos) + int64_t(r) * d.vdy;
        const int ya = (int)(yv >> FRAC_BITS);
        const int yb = (int)((yv + d.vdy) >> FRAC_BITS);
        const int y_top = std::min(ya, yb);          /* VDY < 0 przy VFLIP */
        const int y_bot = std::max(ya, yb);
        if (y_top >= y_bot) continue;                /* pomniejszanie: teksel znika */

        const int n = rc.hi - rc.lo;
        win->advance(fetch_texels(d, r, rc.lo, n, row, rskip));

        int64_t xcur = int64_t(d.xpos) + int64_t(rc.lo) * d.hdx;
        for (int c = 0; c < n; ++c) {
            const int xa = (int)(xcur >> FRAC_BITS);
            const int xb = (int)((xcur + d.hdx) >> FRAC_BITS);
            const int x_l = std::min(xa, xb);        /* HDX < 0 przy HFLIP */
            const int x_r = std::max(xa, xb);
            /* Sąsiednie teksele stykają się dokładnie w (xcur+HDX) >> 16,
             * więc przy obu znakach nie ma ani szczeliny, ani nachodzenia —
             * mapa pokrycia to weryfikuje. */
            if (x_l < x_r)
                for (int y = y_top; y < y_bot; ++y)
                    writer_.emit(y, x_l, x_r, row[(size_t)c], rskip[(size_t)c] != 0);
            xcur += d.hdx;
        }
        /* Sufit: 1 teksel/takt — ścieżka jest ograniczona logiką, nie pamięcią. */
        win->advance(n);
    }
}

/* ---------------------------------------------------- ścieżka AFFINE (§8) */

/* Wszystkie teksele są PRZYSTAJĄCYMI równoległobokami (bo HDD = 0), więc bbox
 * i macierz odwrotna są wspólne dla całego sprajta, a w pętli po bboxie
 * zostają same sumatory i komparatory.
 *
 * Test pokrycia jest DOKŁADNY: próbkowanie w środku piksela sprawia, że każdy
 * piksel należy do dokładnie jednego teksela (D14) — brak dziur i brak
 * podwójnych zapisów przy dowolnym kącie. */
void Mcel::raster_affine(int i, const Derived &d, int Y0, int Y1, Window *win) {
    (void)i;
    const Range rr = range_r(d, Y0, Y1);
    if (rr.empty()) return;

    /* Bounding box teksela — stały dla całego sprajta. */
    const int64_t ahdx = d.hdx < 0 ? -int64_t(d.hdx) : d.hdx;
    const int64_t avdx = d.vdx < 0 ? -int64_t(d.vdx) : d.vdx;
    const int64_t ahdy = d.hdy < 0 ? -int64_t(d.hdy) : d.hdy;
    const int64_t avdy = d.vdy < 0 ? -int64_t(d.vdy) : d.vdy;
    const int bw = int((ahdx + avdx) >> FRAC_BITS) + 2;
    const int bh = int((ahdy + avdy) >> FRAC_BITS) + 2;
    const int64_t ofs_x = std::min<int64_t>(0, d.hdx) + std::min<int64_t>(0, d.vdx);
    const int64_t ofs_y = std::min<int64_t>(0, d.hdy) + std::min<int64_t>(0, d.vdy);

    std::vector<uint8_t> row, rskip;

    for (int r = rr.lo; r < rr.hi; ++r) {
        const Range rc = range_c(d, r, Y0, Y1);
        if (rc.empty()) continue;

        const int n = rc.hi - rc.lo;
        win->advance(fetch_texels(d, r, rc.lo, n, row, rskip));

        const int64_t xv = int64_t(d.xpos) + int64_t(r) * d.vdx;
        const int64_t yv = int64_t(d.ypos) + int64_t(r) * d.vdy;
        int64_t xcur = xv + int64_t(rc.lo) * d.hdx;
        int64_t ycur = yv + int64_t(rc.lo) * d.hdy;

        for (int c = 0; c < n; ++c) {
            const uint8_t texel = row[(size_t)c];
            const bool    rskp  = rskip[(size_t)c] != 0;

            /* §8 pomija przezroczyste teksele PRZED pętlą po bboxie — to
             * oszczędność taktów w RTL. Model przepuszcza je przez writer,
             * bo mapa pokrycia mierzy GEOMETRIĘ (D14: "każdy piksel należy do
             * dokładnie jednego teksela"), a nie to, co ostatecznie widać.
             * Takty liczymy tak jak sprzęt: teksel odrzucony kosztuje odczyt,
             * narysowany — bh taktów, bo bbox ma bh wierszy. */
            const bool transparent = rskp || texel == d.transp_ref();
            win->advance(transparent ? 1 : bh);

            cover_texel(d, xcur, ycur, texel, rskp, bw, bh, ofs_x, ofs_y);
            xcur += d.hdx;
            ycur += d.hdy;
        }
    }
}

/* POKRYJ(Ax, Ay, teksel) — test dokładny, przyrostowy (§8). */
void Mcel::cover_texel(const Derived &d, int64_t Ax, int64_t Ay, uint8_t texel,
                       bool rle_skip, int bw, int bh, int64_t ofs_x, int64_t ofs_y) {
    const int bx0 = (int)((Ax + ofs_x) >> FRAC_BITS);
    const int by0 = (int)((Ay + ofs_y) >> FRAC_BITS);

    /* Punkt startowy: ŚRODEK piksela (bx0, by0). To próbkowanie w środku
     * gwarantuje, że każdy piksel wnętrza należy do dokładnie jednego teksela. */
    const int64_t Px = ((int64_t(bx0) << FRAC_BITS) + FX_HALF) - Ax;
    const int64_t Py = ((int64_t(by0) << FRAC_BITS) + FX_HALF) - Ay;

    int64_t s_row = (int64_t(d.ia) * Px + int64_t(d.ib) * Py) >> FRAC_BITS;
    int64_t t_row = (int64_t(d.ic) * Px + int64_t(d.id) * Py) >> FRAC_BITS;

    for (int j = 0; j < bh; ++j) {
        int64_t sv = s_row, tv = t_row;
        /* Teksel jest wypukły, więc jego przecięcie z wierszem pikseli jest
         * zawsze przedziałem SPÓJNYM (D23) — stąd span, a nie maska bitowa.
         * Model liczy przebiegi i skarży się, gdyby wyszedł więcej niż jeden:
         * to byłby sygnał, że wypukłość gdzieś padła. */
        int run_lo = -1, runs = 0;
        for (int i2 = 0; i2 < bw; ++i2) {
            const bool in = (sv >= 0 && sv < FX_ONE) && (tv >= 0 && tv < FX_ONE);
            if (in && run_lo < 0) run_lo = i2;
            if (!in && run_lo >= 0) {
                writer_.emit(by0 + j, bx0 + run_lo, bx0 + i2, texel, rle_skip);
                run_lo = -1; ++runs;
            }
            sv += d.ia;
            tv += d.ic;
        }
        if (run_lo >= 0) { writer_.emit(by0 + j, bx0 + run_lo, bx0 + bw, texel, rle_skip); ++runs; }
        if (runs > 1)
            warn("POKRYJ: teksel dal wiecej niz jeden przebieg w wierszu — "
                 "wypuklosc teksela padla, D23 zaklada przedzial spojny");
        s_row += d.ib;
        t_row += d.id;
    }
}

/* ------------------------------------------------- ścieżka QUAD (§12) ---- */

/* Przy `HDD != 0` teksel `(c,r)` przestaje być przystającym równoległobokiem:
 * górna krawędź ma krok `HDX + r·HDDX`, dolna `HDX + (r+1)·HDDX`, więc są
 * RÓŻNEJ DŁUGOŚCI. Każdy teksel jest innym czworobokiem, wspólna macierz
 * odwrotna nie istnieje i test pokrycia z §8 nie ma jak zadziałać. Zostaje
 * skan-konwersja — i z nią wracają reguły CEL-a, których §8 się pozbywał.
 *
 * KONSEKWENCJA: gwarancja D14 („każdy piksel należy do dokładnie jednego
 * teksela") przestaje obowiązywać. Reguła „półotwarte w y, domknięte w x"
 * eliminuje szczeliny między sąsiednimi tekselami kosztem ~1 px nachodzenia,
 * więc weryfikacja to „licznik zapisów >= 1", a nie „== 1".
 *
 * Kolejność inkrementacji z §12 jest istotna: `HDx` rośnie PRZED pętlą kolumn,
 * więc wiersz `r` używa `HD_r` dla krawędzi górnej i `HD_{r+1}` dla dolnej.
 * Dolna krawędź wiersza `r` jest identyczna z górną wiersza `r+1` — stąd brak
 * szczelin między wierszami.
 *
 * Przy `HDDY != 0` rozciągłość pionowa wiersza zależy od `r`, więc zakres
 * wierszy liczy `range_r_quad()` z rozgałęzieniem w `r* = −HDY/HDDY`. */
void Mcel::raster_quad(int i, const Derived &d, int Y0, int Y1, Window *win) {
    (void)i;
    const Range rr = range_r_quad(d, Y0, Y1);
    if (rr.empty()) return;

    /* Skrajne wysokości teksela po całym c — przy pełnym HDD lewa krawędź
     * teksela c ma dy = VDY + c·HDDY (§12). Nadmiarowo, czyli bezpiecznie. */
    const int64_t Wq = d.src_w;
    const int64_t vq0 = d.vdy, vq1 = int64_t(d.vdy) + Wq * d.hddy;
    const int64_t vdy_lo = std::min<int64_t>(0, std::min(vq0, vq1));
    const int64_t vdy_hi = std::max<int64_t>(0, std::max(vq0, vq1));

    std::vector<uint8_t> row, rskip;

    for (int r = rr.lo; r < rr.hi; ++r) {
        const fixed_t hdx_top = fx_sat32(int64_t(d.hdx) + int64_t(r) * d.hddx);
        const fixed_t hdx_bot = fx_sat32(int64_t(d.hdx) + int64_t(r + 1) * d.hddx);
        const fixed_t hdy_top = fx_sat32(int64_t(d.hdy) + int64_t(r) * d.hddy);
        const fixed_t hdy_bot = fx_sat32(int64_t(d.hdy) + int64_t(r + 1) * d.hddy);

        /* §4 z krokami krawędzi TEGO wiersza — formuła bez zmian, inne źródło
         * argumentów (D25).
         *
         * Krawędź górna i dolna mają RÓŻNE kroki (`HD_r` i `HD_{r+1}`), a §4
         * modeluje wiersz jednym krokiem. Jeden nie ogranicza obu — zwłaszcza
         * gdy mają przeciwne znaki albo gdy jeden jest zerem. Rozwiązujemy
         * więc dwa razy i bierzemy otoczkę: nadmiarowo, czyli bezpiecznie (§4).
         *
         * Brak tego dawał niedoszacowany zakres i dziurę dokładnie na granicy
         * pasa — złapał to `strip_vs_whole`. */
        const Range c_top = range_c(d, r, Y0, Y1, hdx_top, hdy_top, vdy_lo, vdy_hi);
        const Range c_bot = range_c(d, r, Y0, Y1, hdx_bot, hdy_bot, vdy_lo, vdy_hi);
        Range rc;
        if (c_top.empty() && c_bot.empty()) continue;
        else if (c_top.empty()) rc = c_bot;
        else if (c_bot.empty()) rc = c_top;
        else { rc.lo = std::min(c_top.lo, c_bot.lo); rc.hi = std::max(c_top.hi, c_bot.hi); }
        if (rc.empty()) continue;

        const int n = rc.hi - rc.lo;
        win->advance(fetch_texels(d, r, rc.lo, n, row, rskip));

        const int64_t xv = int64_t(d.xpos) + int64_t(r) * d.vdx;
        const int64_t yv = int64_t(d.ypos) + int64_t(r) * d.vdy;
        int64_t xcur = xv + int64_t(rc.lo) * hdx_top;
        int64_t ycur = yv + int64_t(rc.lo) * hdy_top;
        int64_t xdn  = xv + d.vdx + int64_t(rc.lo) * hdx_bot;
        int64_t ydn  = yv + d.vdy + int64_t(rc.lo) * hdy_bot;

        for (int c = 0; c < n; ++c) {
            const uint8_t texel = row[(size_t)c];
            const bool    rskp  = rskip[(size_t)c] != 0;
            const bool transparent = rskp || texel == d.transp_ref();
            if (!transparent || rskp)
                fill_quad(xcur, ycur, xcur + hdx_top, ycur + hdy_top,
                          xdn + hdx_bot, ydn + hdy_bot, xdn, ydn,
                          texel, rskp, Y0, Y1);
            /* Przy HDDY != 0 dochodzi jedna odwrotność na teksel (§12) —
             * krawędź prawa teksela c jest lewą teksela c+1, więc jedna,
             * nie dwie. W RTL to osobny dzielnik potokowany 1/takt. */
            win->advance(transparent ? 1 : (d.hddy ? 4 : 3));
            xcur += hdx_top;  ycur += hdy_top;
            xdn  += hdx_bot;  ydn  += hdy_bot;
        }
    }
}

/* WYPELNIJ_CZWOROBOK(A,B,C,D) — skan-konwersja z regułą CEL-a.
 *
 *   PÓŁOTWARTE w y  — krawędź liczy się gdy y ∈ [y_start, y_end)
 *   DOMKNIĘTE w x   — span obejmuje oba skrajne piksele
 *
 * Writer przyjmuje `[x0, x1)`, więc domknięcie realizuje `x1 = x_prawy + 1`.
 * Ta asymetria jest właśnie tym, co eliminuje szczeliny między sąsiednimi
 * tekselami kosztem ~1 px nachodzenia. */
void Mcel::fill_quad(int64_t ax, int64_t ay, int64_t bx, int64_t by,
                     int64_t cx, int64_t cy, int64_t dx, int64_t dy,
                     uint8_t texel, bool rle_skip, int Y0, int Y1) {
    const int64_t px[4] = { ax >> FRAC_BITS, bx >> FRAC_BITS,
                            cx >> FRAC_BITS, dx >> FRAC_BITS };
    const int64_t py[4] = { ay >> FRAC_BITS, by >> FRAC_BITS,
                            cy >> FRAC_BITS, dy >> FRAC_BITS };

    if (px[0] == px[1] && px[1] == px[2] && px[2] == px[3]) return;   /* zdegenerowany */

    int64_t ylo = py[0], yhi = py[0];
    for (int k = 1; k < 4; ++k) { ylo = std::min(ylo, py[k]); yhi = std::max(yhi, py[k]); }
    const int y_from = (int)std::max<int64_t>(ylo, Y0);
    const int y_to   = (int)std::min<int64_t>(yhi, Y1);

    for (int y = y_from; y < y_to; ++y) {
        int64_t xs[4];
        int nx = 0;
        for (int k = 0; k < 4; ++k) {
            const int k2 = (k + 1) & 3;
            const int64_t y1 = py[k], y2 = py[k2];
            if (y1 == y2) continue;
            /* Półotwarte w y: krawędź liczy się na [min, max), więc wierzchołek
             * dzielony przez dwie krawędzie daje dokładnie jedno przecięcie. */
            if (y < std::min(y1, y2) || y >= std::max(y1, y2)) continue;
            const int64_t x1 = px[k], x2 = px[k2];
            xs[nx++] = x1 + div_d33((x2 - x1) * (int64_t(y) - y1), y2 - y1);
        }
        if (nx < 2) continue;
        std::sort(xs, xs + nx);
        for (int k = 0; k + 1 < nx; k += 2) {
            const int x0 = (int)xs[k];
            const int x1 = (int)xs[k + 1];
            writer_.emit(y, x0, x1 + 1, texel, rle_skip);   /* DOMKNIETE w x */
        }
    }
}

/* ------------------------------------- wypełniacz bboxu (dowód etapu E0.2) */

/* W miejsce rasteryzatora. Dowód: kolorowe prostokąty, ciągłe na granicy pasa.
 * Kolor bierze się z indeksu sprajta, żeby na obrazku było widać, który jest który. */
void Mcel::raster_bbox_fill(int i, const Derived &d, int Y0, int Y1, Window *win) {
    const int64_t W = d.src_w, H = d.src_h;
    const int64_t p0x = d.xpos,          p0y = d.ypos;
    const int64_t p1x = p0x + W * d.hdx, p1y = p0y + W * d.hdy;
    const int64_t p3x = p0x + H * d.vdx, p3y = p0y + H * d.vdy;
    const int64_t p2x = p1x + H * d.vdx, p2y = p1y + H * d.vdy;

    int ymin = (int)(std::min(std::min(p0y, p1y), std::min(p2y, p3y)) >> FRAC_BITS);
    int ymax = (int)(std::max(std::max(p0y, p1y), std::max(p2y, p3y)) >> FRAC_BITS);
    int xmin = (int)(std::min(std::min(p0x, p1x), std::min(p2x, p3x)) >> FRAC_BITS);
    int xmax = (int)(std::max(std::max(p0x, p1x), std::max(p2x, p3x)) >> FRAC_BITS);

    ymin = std::max(ymin, Y0);
    ymax = std::min(ymax, Y1);
    xmin = std::max(xmin, 0);
    xmax = std::min(xmax, SCREEN_W);
    if (ymin >= ymax || xmin >= xmax) return;

    const uint8_t col = uint8_t(1 + (i % 15));
    for (int y = ymin; y < ymax; ++y) writer_.emit(y, xmin, xmax, col);

    /* Bez ruchu SDRAM; sufit to port zapisu bufora pasa, 4 px/takt. */
    win->advance(int(int64_t(ymax - ymin) * (xmax - xmin) / 4));
}

/* ------------------------------------------------------------ napęd czasu */

void Mcel::strip(unsigned s) {
    if (cfg_.trace_enabled) { uint8_t p = uint8_t(s); trace_rec(0x05, &p, 1); }
    if (!enabled() || s >= (unsigned)STRIPS) return;

    if (s == 0) writer_.begin_frame();   /* mapy weryfikacyjne — raz na klatkę */

    /* Pas 0 renderuje się w wygaszaniu, RAZEM z prologue — wygaszanie musi
     * pomieścić jedno i drugie (02-architecture §8). */
    const int64_t window = (s == 0)
        ? std::max<int64_t>(0, cfg_.vblank_window_cycles - stats_.prologue_cycles)
        : cfg_.strip_window_cycles;

    StripStat &st = stats_.strip[s];
    st = StripStat();
    st.cycles_window = window;

    Window win;
    win.open(&sdram_, window, cfg_, &cpu_probe_);

    StripTarget tgt(sb_, (int)s);
    writer_.set_target(&tgt);

    const int Y0 = int(s) * STRIP_H;
    scan_and_raster((int)s, Y0, Y0 + STRIP_H, &win, &st);

    st.cycles = win.used;
    writer_.set_target(nullptr);
}

/* --------------------------------------------- tryb referencyjny (E0.4) -- */

void Mcel::render_whole_frame(uint8_t *out) {
    if (!enabled()) return;

    writer_.begin_frame();
    FrameTarget tgt(out);
    writer_.set_target(&tgt);

    /* Bez okna czasowego: limit = 0 wyłącza sprawdzanie deadline'u, bo tryb
     * referencyjny nie ma pasów, więc nie ma czego ścigać z promieniem.
     * Liczniki SDRAM nadal chodzą. */
    Window win;
    win.open(&sdram_, 0, cfg_, &cpu_probe_);

    scan_and_raster(-1, 0, SCREEN_H, &win, nullptr);
    writer_.set_target(nullptr);
}

} /* namespace mcel */
