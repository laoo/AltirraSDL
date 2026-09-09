/* mcel.h — publiczne API modelu referencyjnego blittera MariaCEL (etap E0).
 *
 * ZASADA NACZELNA: jedynym wejściem do modelu jest magistrala 6502.
 * Nie ma i nie będzie dostępu do pól rekordu sprajta z pominięciem magistrali.
 * Powód jest praktyczny: każdy scenariusz testowy generuje wtedy ślad
 * transakcji, który testbench RTL odtwarza jeden do jednego, a model
 * przechodzi przez dekodowanie adresu okna i rejestrów — czyli dokładnie
 * te kawałki, które najłatwiej pomylić.
 *
 * Wygoda idzie w warstwę helperów NAD magistralą (mcel_helpers.h), która nie
 * robi nic poza wołaniem wr().
 *
 * Zakres tej wersji: E0.1–E0.5 — infrastruktura, ścieżka BLIT (MODE = LINE,
 * 8 bpp surowe), tryb referencyjny, mapa pokrycia, model transakcji SDRAM.
 * SCALE / ROTSCALE / AFFINE / 4 bpp / RLE / QUAD dochodzą w E0.6+.
 */
#ifndef MCEL_H
#define MCEL_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "mcel_derived.h"
#include "mcel_fixed.h"
#include "mcel_regs.h"
#include "mcel_rle.h"
#include "mcel_sdram.h"
#include "mcel_span.h"
#include "mcel_video.h"

/* ALTIRRA: device wrapper granted friend access for save states. */
class ATDeviceMaria;

namespace mcel {

/* ------------------------------------------------------------ konfiguracja */

struct McelConfig {
    /* Umiejscowienie okien i rejestrów w przestrzeni 6502 ustala dekoder
     * adresowy URZĄDZENIA (03-interface-spec §1), nie blitter. D53, D54. */
    uint16_t win_base[WIN_SLOTS] = { DEF_WIN0_BASE, DEF_WIN1_BASE,
                                     DEF_WIN2_BASE };
    /* Rozmiar okna jest PER SLOT: $4000 i $8000 mają 16 KB, $C000 — 4 KB
     * (D80). Przez to najmniejsze widać pierwsze 4 KB bloku; TEXTAB (4 096 B)
     * mieści się w nim co do bajta. */
    uint16_t win_size[WIN_SLOTS] = { WIN_SIZE, WIN_SIZE, WIN_SIZE_4K };
    /* Rejestry RAMMAP każdego slotu: { młodszy, starszy }. Blitter je dekoduje,
     * bo bez tego nie wie, co znaczy kolejny dostęp pod bazę okna (D54). */
    uint16_t rammap[WIN_SLOTS][2] = { { MARIA_RAMMAP0_L, MARIA_RAMMAP0_H },
                                      { MARIA_RAMMAP1_L, MARIA_RAMMAP1_H },
                                      { MARIA_RAMMAP2_L, MARIA_RAMMAP2_H } };
    /* JEDYNY adres, pod ktory blitter odpowiada (D59, D60). */
    uint16_t regbase = DEF_REGBASE;    /* CTL (zapis) / STATUS (odczyt) */

    /* Paleta CEL przez PBIRAMBANK (D86). Oba adresy naleza do dekodera
     * URZADZENIA, jak `rammap` (D18, D53) — blitter je tylko dekoduje. */
    uint16_t pbirambank  = MARIA_PBIRAMBANK;   /* (W) wybor strony pod $DF00 */
    uint16_t pbiram_base = PBIRAM_BASE;        /* 256 B strony po wyborze     */

    /* Okno czasowe pasa (docs/02-architecture.md §8). 60 Hz, 262 linie, 135 MHz. */
    int64_t strip_window_cycles  = 206100;
    int64_t vblank_window_cycles = 188900;

    /* [model] Setup jednego sprajta w prologue poza ruchem SDRAM: dzielnik
     * jest potokowany (D20, 1 wynik/takt), więc to kilka taktów, nie kilkadziesiąt. */
    int prologue_setup_cycles = 6;

    /* [model] Odczyt jednego bajta rekordu sprajta z BRAM (D46). Port jest
     * bajtowy — to on daje 8 bloków zamiast więcej — więc rekord kosztuje
     * tyle taktów, ile stron trzeba przeczytać (spr_pages_for_opcode).
     * Nakłada się na ruch SDRAM tego samego sprajta, ale model serializuje,
     * więc liczy się wprost. */
    int sprlist_read_cycles_per_byte = 1;

    /* Dostępy 6502 wstrzykiwane jako okresowe żądania (D65) —
     * nie ma potrzeby symulować prawdziwego procesora, żeby odtworzyć 3,6%
     * okna pasa. Jedno żądanie kosztuje tRP+tRCD+CL+1 = 10 taktów, więc okres
     * 275 daje 749 żądań na okno pasa i 7 490 taktów: dokładnie pozycja
     * "dostępy 6502 — 7 500 taktów, 3,64%" z docs/02-architecture.md §8.
     *
     * Aktualizacje sprajtów tędy NIE idą: SPRLIST leży w BRAM i nie kosztuje
     * ani jednego taktu SDRAM (D46). Parametr modeluje pozostały ruch 6502
     * przez okno bankowane urządzenia i jest czystym pokrętłem; przy programie
     * 6502 zastępuje go zmierzona gęstość dostępów (D65).
     * `cpu6502_addr` [model] — adres, pod który idą te żądania. */
    uint32_t cpu6502_addr      = 0x040000;
    int  cpu6502_period_cycles = 275;
    bool cpu6502_enabled       = true;

    bool trace_enabled = false;
};

/* -------------------------------------------------------------- statystyki */

struct StripStat {
    int64_t cycles        = 0;
    int64_t cycles_window = 0;
    int     sprites_scanned = 0;
    int     sprites_hit     = 0;   /* w zasięgu pasa                     */
    int     sprites_culled_pat = 0;/* PAT == 0 → odrzucone PRZED prefetchem */
    int     sprites_drawn   = 0;
    bool    overrun         = false;
};

struct ModelStats {
    /* Residuum macierzy odwrotnej: o ile złożenie IA..ID z HDX..VDY odbiega
     * od tożsamości. Zero znaczy, że test pokrycia z §8 jest DOKŁADNY i D14
     * ("każdy piksel należy do dokładnie jednego teksela") obowiązuje co do
     * bitu. Wartość niezerowa mierzy szerokość szczeliny/zakładki na granicy
     * teksela, w jednostkach 2^-FRAC_BITS teksela:
     *     < 0  → sąsiednie teksele NACHODZĄ  → możliwy podwójny zapis
     *     > 0  → między tekselami SZCZELINA  → możliwa dziura
     * To jest liczba, przeciwko której da się sprawdzić RTL przy E6. */
    int32_t   inv_residual_min = 0;
    int32_t   inv_residual_max = 0;
    int       inv_exact_sprites = 0;   /* ile sprajtów ma residuum == 0 */

    int64_t   prologue_cycles = 0;
    int       prologue_sprites = 0;
    int       mode_count[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    int       invisible = 0;
    int       unsupported_opcode = 0;   /* E0.6+ */
    int       degenerate = 0;           /* det == 0 */
    StripStat strip[STRIPS];
    bool      overrun = false;
};

/* ---------------------------------------------------------------- urządzenie */

class Mcel {
    /* ALTIRRA: the device wrapper serialises the private BRAM/latch state
     * for save states; nothing else in Altirra touches the internals. */
    friend class ::ATDeviceMaria;
public:
    explicit Mcel(const McelConfig &cfg = McelConfig());

    /* === magistrala 6502 — JEDYNE wejście ============================== */
    void    wr(uint16_t addr, uint8_t val);
    uint8_t rd(uint16_t addr);

    /* === ścieżka wgrywania danych URZĄDZENIA (to NIE jest 6502) ========
     * Tędy trafiają do SDRAM już tylko DANE TEKSTUR (03-interface-spec §3).
     * SPRLIST (D46), DERIVED (D52) i TEXTAB (D58) leżą w BRAM, do którego ta
     * ścieżka nie sięga — ich jedynym pisarzem jest 6502 przez okno.
     * Nie obciąża budżetu pasa.                                            */
    void upload(uint32_t sdram_addr, const void *src, size_t n);

    /* Paleta CEL ta sama sciezka URZADZENIOWA co upload() — bez oplaty,
     * z rekordem sladu 0x08 (768 B, RGB przeplatane). Dla scen YAML; program
     * 6502 wpisuje palete sam, przez PBIRAMBANK i strone $DF00 (D86). */
    void set_celpal(const uint8_t rgb[CELPAL_ENTRIES * 3]);

    /* === tor wideo ===================================================== */
    uint8_t vid_pixel(unsigned x, unsigned y) const;
    /* Paleta CEL — wglad tylko do odczytu. Wejsciem jest magistrala
     * (PBIRAMBANK + $DFxx) albo set_celpal(). `component`: 0 = R, 1 = G,
     * 2 = B; celpal_rgb() sklada trzy banki w RGBRGB... dla PNG i okna. */
    uint8_t        pbirambank() const { return pbirambank_; }
    const uint8_t *celpal(int component) const { return celpal_[component]; }
    void           celpal_rgb(uint8_t out[CELPAL_ENTRIES * 3]) const;

    /* === napęd czasu =================================================== */
    void vblank();                              /* prologue, jeśli !SNAP    */
    void strip(unsigned s);                     /* rasteryzacja pasa s      */
    void scanout(unsigned s, uint8_t *out);     /* odczyt + czyszczenie     */
    void frame_done();                          /* impuls: przełącza FRAME_ODD */

    /* === tryb referencyjny (E0.4) ======================================
     * Cała klatka naraz, bez pasów. TA SAMA ścieżka rasteryzacji — zmienia
     * się wyłącznie cel spanów i granice Y0..Y1 (D23).                     */
    void render_whole_frame(uint8_t *out /* 320*240 */);

    /* === wgląd dla testów i statystyk (tylko odczyt) ==================== */
    bool     snap()      const { return !snap_req_; }
    bool     overrun()   const { return overrun_; }
    bool     enabled()   const { return (ctl_ & CTL_ENABLE) != 0; }
    bool     frame_odd() const { return frame_odd_; }
    /* NODETECT jest bitem POZIOMOWYM w CTL (D60). Przez magistrale odczytuje
     * sie go tak samo jak ENABLE — przez STATUS.b1 — wiec ten wglad jest
     * wygoda dla testow, a nie jedyna droga. */
    bool     nodetect()  const { return (ctl_ & CTL_NODETECT) != 0; }

    /* Który blok BRAM widać pod bazą okna `base`, albo -1 gdy slot pokazuje
     * SDRAM (albo stock RAM) — czyli gdy blitter pod tym adresem milczy.
     * Wgląd tylko do odczytu; jedynym wejściem jest zapis do RAMMAP. */
    int      bram_block_at(uint16_t base) const;

    /* --- tryb STEP (D56) — wgląd dla testów i dla mcel_run --------------- */
    /* Indeks pasa, którego bufor widać w oknie WIN_STRIP; -1 przed pierwszym
     * strobem. Następny krok wyrenderuje step_next(). */
    int      step_last() const { return step_last_; }
    int      step_next() const { return step_next_; }
    /* Klatka składana z pasów konsumowanych w trybie krokowym. Pas trafia tu
     * na starcie NASTĘPNEGO kroku, nie na końcu bieżącego (D56). */
    const uint8_t *step_frame() const { return step_frame_; }

    const uint8_t *stripmap() const { return &stripmap_[0][0]; }

    /* DERIVED rezydentne w BRAM (D52) — to jest CAŁE wyjście prologue,
     * nie kopia. Wgląd tylko do odczytu; wejściem jest prologue albo okno. */
    const uint8_t *derived_ram() const { return derived_ram_; }

    /* SPRLIST w BRAM blittera (D46). Adres to A[12:0] okna, czyli
     * (strona << 8) | indeks — bez transpozycji. Wgląd tylko do odczytu;
     * jedynym wejściem pozostaje magistrala. */
    const uint8_t *sprlist() const { return sprlist_; }
    uint8_t sprlist_byte(int page, int idx) const {
        return (page < SPR_PAGES) ? sprlist_[(page << 8) | idx] : 0xFF;
    }

    /* TEXTAB w BRAM blittera (D58). Adres to (TEXID << 4) | offset — ten sam
     * po obu stronach okna. Wglad tylko do odczytu; wejsciem jest magistrala. */
    const uint8_t *textab() const { return textab_; }
    const uint8_t *tex_rec(int id) const { return &textab_[size_t(id) << 4]; }
    Sdram         &sdram()          { return sdram_; }
    const Sdram   &sdram()    const { return sdram_; }
    SpanWriter    &writer()         { return writer_; }
    const SpanWriter &writer() const { return writer_; }
    StripBuffers  &buffers()        { return sb_; }
    const ModelStats &stats() const { return stats_; }
    ModelStats       &stats()       { return stats_; }
    McelConfig       &config()      { return cfg_; }
    const McelConfig &config() const{ return cfg_; }

    /* Rekord DERIVED sprajta i — rozpakowywany bajt po bajcie (D37).
     * Do diagnostyki i testów; ścieżka rasteryzacji używa tej samej funkcji. */
    Derived read_derived(int i) const;

    /* --- viewport (D84) i akcelerator kolizji (D85) --------------------
     * Wgląd tylko do odczytu; jedynym wejściem pozostaje magistrala —
     * strona $14 okna SPRLIST. */
    int16_t vpx()        const { return vpx_; }
    int16_t vpy()        const { return vpy_; }
    uint8_t col_status() const { return col_status_; }
    uint8_t col_b()      const { return col_b_; }

    /* Zamiast rasteryzatora: wypełniacz bboxu (dowód E0.2 — kolorowe
     * prostokąty, ciągłe na granicy pasa). */
    bool bbox_fill_mode = false;

    /* --- ślad transakcji (E0.5) --------------------------------------- */
    void trace_begin();
    bool trace_write(const std::string &path) const;

    /* --- ostrzeżenia modelu ------------------------------------------- */
    const std::vector<std::string> &warnings() const { return warnings_; }
    void warn(const std::string &s);

private:
    /* --- magistrala --- */
    /* Wyszukanie slotu okna pokazującego BRAM i obejmującego adres `a`.
     * Zwraca numer bloku albo -1; przy trafieniu ustawia offset w bloku.
     * Znaczenie RAMMAP liczy się TUTAJ, a nie przy zapisie — dzięki temu
     * kolejność zapisów L/H nie istnieje jako pytanie (D54). */
    int      bram_lookup(uint16_t a, uint16_t &off) const;
    /* Zapis do RAMMAP? Jeśli tak, zatrzaskuje surowy bajt i zwraca true. */
    bool     rammap_wr(uint16_t a, uint8_t v);
    /* Liczba WAZNYCH bajtow bloku BRAM. Reszta okna, az do 16 KB, jest POZA
     * ZAKRESEM i sprzet NIE OBIECUJE TAM ZADNEJ WARTOSCI (D76). */
    static int win_block_bytes(int blk);
    uint8_t  win_rd_block(int blk, uint16_t off) const;
    /* Strona $14 okna SPRLIST: viewport (D84) i akcelerator kolizji (D85).
     * Siedem bajtow wyjetych z obszaru, ktory poza tym jest POZA MAPA (D76). */
    static bool sprwin_is_reg(uint16_t off) {
        return off >= SPRWIN_REGS && off < SPRWIN_REGS_END;
    }
    void     sprwin_wr(uint16_t off, uint8_t v);
    uint8_t  sprwin_rd(uint16_t off) const;
    /* D56 — jeden krok renderowania, napędzany strobem CTL.DBG_STEP. */
    void     step_strobe();
    /* Zwraca plaski indeks rejestru dla adresu 6502, albo -1 gdy blitter pod
     * tym adresem nie odpowiada. Jeden komparator na $d1B2 — i to jest CALE
     * odwzorowanie adresow blittera (D53, D59, D60). */
    int      reg_index(uint16_t a) const;
    void     write_reg(uint8_t r, uint8_t v);

    /* --- prologue --- */
    void prologue();
    bool setup_opcode(int i, const uint8_t rec[SPR_REC_SZ],
                      const uint8_t tex[TEX_REC_SZ], Derived &d, int64_t &cycles);
    void apply_flip(Derived &d, bool hflip, bool vflip);
    bool inverse_matrix(Derived &d);
    bool quad_setup(Derived &d);
    void detect_mode(Derived &d, uint8_t opcode);
    void bbox_stripmap(int i, const Derived &d);
    /* D84 — krok MIĘDZY setup_opcode() a apply_flip(): jedna reguła dla
     * wszystkich pięciu opcodów, bo przesunięcie okna jest translacją,
     * a apply_flip() jest na translację odporne. */
    void apply_viewport(Derived &d, int16_t vpx, int16_t vpy);

    /* --- akcelerator kolizji (D85) --------------------------------------
     * Liczy się NA ŻĄDANIE, przy zapisie COLB, wprost z SPRLIST i TEXTAB —
     * nic się nie prelicza i rekord DERIVED nie rośnie ani o bajt. */
    struct Hitbox { int64_t x0, y0, x1, y1; };      /* 12.4, przestrzeń świata */
    bool hitbox_of(int i, Hitbox &hb) const;
    static bool hb_overlap(const Hitbox &a, const Hitbox &b);
    void col_scan();

    /* --- rasteryzacja --- */
    struct Window;
    void scan_and_raster(int s, int Y0, int Y1, Window *win, StripStat *st);
    void raster(int i, const Derived &d, int Y0, int Y1, Window *win);
    int  fetch_source_bytes(const Derived &d, int r, int skip_bytes, int nbytes,
                            std::vector<uint8_t> &bytes, std::vector<uint8_t> &skip);
    int  fetch_texels(const Derived &d, int r, int c0, int n,
                      std::vector<uint8_t> &out, std::vector<uint8_t> &out_skip);
    void raster_line(int i, const Derived &d, int Y0, int Y1, Window *win);
    void raster_scale(int i, const Derived &d, int Y0, int Y1, Window *win);
    void raster_affine(int i, const Derived &d, int Y0, int Y1, Window *win);
    void raster_quad(int i, const Derived &d, int Y0, int Y1, Window *win);
    void fill_quad(int64_t ax, int64_t ay, int64_t bx, int64_t by,
                   int64_t cx, int64_t cy, int64_t dx, int64_t dy,
                   uint8_t texel, bool rle_skip, int Y0, int Y1);
    void cover_texel(const Derived &d, int64_t Ax, int64_t Ay, uint8_t texel,
                     bool rle_skip, int bw, int bh, int64_t ofs_x, int64_t ofs_y);
    void raster_bbox_fill(int i, const Derived &d, int Y0, int Y1, Window *win);

    /* --- ślad --- */
    void trace_rec(uint8_t type, const void *payload, size_t n);

    McelConfig cfg_;
    Sdram      sdram_;
    StripBuffers sb_;
    SpanWriter writer_;

    /* CTL jest write-only i osobny od STATUS; trzyma DWA bity poziomowe —
     * ENABLE i NODETECT (D60). Innych rejestrow blitter nie ma. */
    uint8_t ctl_      = 0;
    bool    snap_req_ = true;   /* SNAP = !req; po RESET req = 1 → SNAP = 0 */
    bool    overrun_  = false;
    bool    frame_odd_= false;

    /* Rezydentne w BRAM. Wstaje WYZEROWANE, jak kazdy inny blok BRAM (D78).
     * Zera znacza s_min = s_max = 0, czyli "widoczny w pasie 0" — i to samo
     * w sobie nic nie daje: rasteryzator odcina sprajta przy PAT = 0 (D42),
     * a PAT liczy sie z surowego DRAW, ktory tez jest zerem. */
    uint8_t stripmap_[SPR_COUNT][2] = { { 0 } };
    /* DERIVED w BRAM (D52). Wstaje wyzerowane — jak BRAM z bitstreamu —
     * więc rekordy sprajtów z DRAW = 0, których prologue nie dotyka, są
     * zerami dokładnie tak, jak zakłada wzorzec (§4.1). */
    uint8_t derived_ram_[DERIVED_BYTES] = { 0 };
    /* --- viewport (D84) -------------------------------------------------
     * Dwa pola 16-bitowe 12.4 ze znakiem, w oknie SPRLIST pod $1400. Po
     * resecie ZERA, czyli okno w (0,0) — zachowanie identyczne z tym sprzed
     * D84, więc żaden istniejący wzorzec DERIVED się nie zmienia.
     *
     * Prologue czyta je RAZ na przebieg, przed pętlą po sprajtach — i to jest
     * formalne domknięcie reguły "viewportu nie rusza się w trakcie klatki":
     * zmiana w trakcie przebiegu nie ma jak zadziałać w połowie listy. */
    int16_t vpx_ = 0;
    int16_t vpy_ = 0;

    /* --- akcelerator kolizji (D85) --------------------------------------
     * COLA i COLBMAX są ZATRZASKAMI; skan startuje wyłącznie zapis COLB.
     * col_status_ trzyma ostatnią odpowiedź — w modelu zawsze gotową, bo
     * col_scan() liczy natychmiast (NOTREADY nigdy nie jest tu widziane
     * jako 1, tak samo jak BUSY, D56). */
    uint8_t col_a_      = 0;
    uint8_t col_b_      = 0;
    uint8_t col_bmax_   = 0;
    uint8_t col_status_ = 0;

    /* Surowe bajty RAMMAP obu slotów: [slot][0] = młodszy, [1] = starszy.
     * Zero = stock RAM Atari, czyli po resecie ŻADNE okno nie jest wpięte
     * (D54) — to jest prawda o sprzęcie, nie wygodna fikcja. */
    uint8_t rammap_[WIN_SLOTS][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 } };

    /* --- paleta CEL (D86) ----------------------------------------------
     * Zatrzask PBIRAMBANK i trzy banki skladowych. Wstaja WYZEROWANE — cala
     * paleta czarna, ta sama regula co dla BRAM (D78) — i RESET ich nie
     * rusza, jak BRAM: to stan, ktory program ma wpisac sam. */
    uint8_t pbirambank_ = 0;
    uint8_t celpal_[CELPAL_COMPONENTS][CELPAL_ENTRIES] = { { 0 } };
    /* Zapis/odczyt pod $DFxx przy banku, ktorego model nie zna — ostrzezenie
     * jednorazowe, jak reszta warn(). */
    bool    pbiram_bank_is_celpal() const {
        return pbirambank_ >= PBIRAM_CELPAL_R && pbirambank_ <= PBIRAM_CELPAL_B;
    }
    bool    pbiram_hit(uint16_t a) const {
        return a >= cfg_.pbiram_base && a < uint32_t(cfg_.pbiram_base) + PBIRAM_SIZE;
    }
    void    pbiram_unknown_bank();

    /* --- tryb STEP (D56). Bez stanu trybu: to są wyłącznie liczniki. --- */
    int  step_next_    = 0;    /* pas, który wyrenderuje następny strob      */
    int  step_last_    = -1;   /* pas widoczny w oknie; -1 = jeszcze żaden   */
    bool step_unread_  = false;/* strob był, STATUS nieczytany — patrz rd()  */
    uint8_t step_frame_[size_t(SCREEN_W) * SCREEN_H] = { 0 };
    /* Adresowanie posrednie urzadzenia jest dla blittera cudze (D60): nie ma
     * tam ani jednego jego rejestru, wiec nie musi wiedziec, ktory bank jest
     * wybrany, i jego konfiguracja jest atomowa bez SEI. */
    /* SPRLIST w BRAM (D46). Wstaje wyzerowane — jak BRAM z bitstreamu —
     * więc DRAW = 0 dla wszystkich sprajtów i fail-safe z D40 jest
     * sprzętowy, a nie zależny od uprzejmości oprogramowania. */
    uint8_t sprlist_[SPRLIST_BYTES] = { 0 };
    /* TEXTAB w BRAM (D58). Zawartosc poczatkowa jest z punktu widzenia
     * kontraktu NIEOKRESLONA: deskryptor jest wejsciem obowiazkowym, wiec
     * oprogramowanie musi go wypelnic przed uzyciem, a fail-safe'u takiego jak
     * DRAW = 0 przy SPRLIST (D40) tu nie ma i miec nie musi — sprajt zgaszony
     * wypada PRZED odczytem deskryptora. Model trzyma zera, bo czyms musi;
     * nie jest to wartosc, na ktorej wolno cokolwiek oprzec. */
    uint8_t textab_[TEXTAB_BYTES] = { 0 };

    ModelStats stats_;
    std::vector<std::string> warnings_;
    std::vector<uint8_t>     trace_;
    uint32_t cpu_probe_ = 0;
};

/* ------------------------------------------------------------------------
 * Nazwy z model/README.md, rozdz. API — dosłownie, żeby dokument i kod
 * dało się czytać obok siebie. To jest cały interfejs modelu.
 * ---------------------------------------------------------------------- */
using mcel_t = Mcel;

inline void     mcel_wr     (mcel_t *m, uint16_t a, uint8_t v) { m->wr(a, v); }
inline uint8_t  mcel_rd     (mcel_t *m, uint16_t a)            { return m->rd(a); }
inline void     mcel_upload (mcel_t *m, uint32_t a, const void *s, size_t n) { m->upload(a, s, n); }
inline uint8_t  mcel_vid_pixel(mcel_t *m, unsigned x, unsigned y) { return m->vid_pixel(x, y); }
inline void     mcel_vblank (mcel_t *m)                        { m->vblank(); }
inline void     mcel_strip  (mcel_t *m, unsigned s)            { m->strip(s); }
inline void     mcel_scanout(mcel_t *m, unsigned s, uint8_t *o){ m->scanout(s, o); }

} /* namespace mcel */

#endif /* MCEL_H */
