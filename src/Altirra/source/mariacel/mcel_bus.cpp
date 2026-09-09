/* mcel_bus.cpp — dekodowanie rejestru i okna na bloki BRAM (E0.1).
 *
 * To są miejsca, które model ze strukturą całkowicie by ominął, a które
 * najłatwiej pomylić. Dlatego magistrala jest jedynym wejściem (D61).
 *
 * SPRLIST leży w BRAM blittera, a 6502 i prologue adresują to samo A[12:0] —
 * transpozycji nie ma (D46). Rekord ma 20 B (D47), więc strony $14-$1F są POZA
 * MAPĄ: odczyt oddaje $FF, zapis jest ignorowany. Wyjątkiem jest siedem bajtów
 * $1400-$1406 — viewport (D84) i sterowanie kolizjami (D85), mcel_collide.cpp.
 *
 * Okna są TRZY i niezależne, a blitter sam dekoduje RAMMAP (D54, D80); po
 * resecie żadne nie jest wpięte. Rozmiar okna jest per slot: $4000 i $8000
 * mają 16 KB, $C000 — 4 KB. Zapisywalne są wyłącznie WEJŚCIA prologue — SPRLIST
 * i TEXTAB, oba w BRAM (D55, D58).
 *
 * Paleta CEL jest cudza, ale dekodowana tu z tego samego powodu co RAMMAP:
 * PBIRAMBANK ($D14F) wybiera stronę pod $DF00, a banki $19-$1B niosą R, G, B
 * (D86). Model nie modeluje selekcji PBI — strona $DFxx jest w nim zawsze.
 */
#include "mcel.h"

#include <cstdio>
#include <cstring>

namespace mcel {

Mcel::Mcel(const McelConfig &cfg) : cfg_(cfg) {
    sb_.clear_all();
    /* WSZYSTKIE bufory BRAM wstaja WYZEROWANE — jedna regula, bez wyjatkow
     * (D78). TEXTAB i DERIVED zeruje inicjalizator w klasie. */
    std::memset(sprlist_,  0, sizeof sprlist_);
    std::memset(stripmap_, 0, sizeof stripmap_);
    if (cfg_.trace_enabled) trace_begin();
}

void Mcel::warn(const std::string &s) {
    for (const auto &w : warnings_) if (w == s) return;   /* bez powtórek */
    warnings_.push_back(s);
}

/* ------------------------------------------------------------- adresowanie */

/* Znaczenie RAMMAP liczone JEST TUTAJ, przy dostępie — nie przy zapisie.
 * L i H przychodzą jako dwa osobne zapisy, więc dekodowanie w momencie zapisu
 * kazałoby odpowiedzieć, co widać między nimi i czy kolejność ma znaczenie.
 * Tak te pytania nie istnieją: nie ma zatrzaśniętej interpretacji (D54). */
int Mcel::bram_lookup(uint16_t a, uint16_t &off) const {
    for (int k = 0; k < WIN_SLOTS; ++k) {
        if (!(rammap_[k][1] & RAMMAP_BRAM)) continue;      /* slot pokazuje SDRAM */
        const uint16_t base = cfg_.win_base[k];
        /* Rozmiar bierzemy z SLOTU, nie ze stalej: okno $C000 ma 4 KB (D80). */
        if (a < base || a >= uint32_t(base) + cfg_.win_size[k]) continue;
        off = uint16_t(a - base);
        return rammap_[k][0];
    }
    return -1;
}

int Mcel::bram_block_at(uint16_t base) const {
    for (int k = 0; k < WIN_SLOTS; ++k)
        if (cfg_.win_base[k] == base && (rammap_[k][1] & RAMMAP_BRAM))
            return rammap_[k][0];
    return -1;
}

/* RAMMAP należy do MARII, ale blitter go zatrzaskuje: bez tego nie wie, co
 * znaczy kolejny dostęp pod bazę okna (D54). Zapis PRZELATUJE DALEJ, do reszty
 * urządzenia. */
bool Mcel::rammap_wr(uint16_t a, uint8_t v) {
    for (int k = 0; k < WIN_SLOTS; ++k)
        for (int b = 0; b < 2; ++b)
            if (a == cfg_.rammap[k][b]) { rammap_[k][b] = v; return true; }
    return false;
}

/* Odwzorowanie adresu 6502 na plaski indeks rejestru blittera (D53).
 * Rejestr jest JEDEN (D60), wiec jest to jeden komparator — i tyle.
 * Adresowanie posrednie urzadzenia jest dla blittera cudze: $d100..$d104 sa
 * dla niego zwyklymi obcymi adresami. */
int Mcel::reg_index(uint16_t a) const {
    /* CTL/STATUS: adres bezposredni. To po to tam jest — petla oczekiwania
     * "bit/bvs/bpl" nie moze zalezec od niczyjego stanu globalnego. */
    if (a >= cfg_.regbase && a < uint32_t(cfg_.regbase) + REGBASE_SIZE)
        return int(a - cfg_.regbase);
    return -1;
}

/* ---------------------------------------------------------------- rejestry */

void Mcel::write_reg(uint8_t r, uint8_t v) {
    if (r == REG_CTL) {
        /* CTL i STATUS to dwa różne rejestry pod jednym adresem, rozdzielone
         * kierunkiem dostępu. Bity POZIOMOWE to ENABLE i NODETECT (D60);
         * reszta to strobe'y działające na zboczu zapisu i kasujące się same.
         *
         * Zapis podaje KOMPLET bitów poziomowych — RMW nie ma jak zrobić, bo
         * spod tego samego adresu czyta się STATUS. Zapomniany NODETECT gaśnie
         * tu po cichu i to jest cena, którą D60 bierze świadomie. */
        ctl_ = uint8_t(v & (CTL_ENABLE | CTL_NODETECT));

        if (v & CTL_RESET) {
            snap_req_  = true;      /* po RESET SNAP = 0 → pierwszy vblank robi prologue */
            overrun_   = false;
            frame_odd_ = false;
            sdram_.reset_banks();
            /* ZAWARTOSC BRAM ZOSTAJE — SPRLIST, TEXTAB, DERIVED i STRIPMAP
             * (D78). RESET jest strobem i obejmuje stan sterujacy; kilkunastu
             * kilobajtow BRAM nie ma jak przepisac i nikt tego nie oczekuje.
             * Wyzerowanie jest stanem POCZATKOWYM, nie skutkiem RESET-u. */
        }
        /* Kasowanie semafora w trakcie prologue jest bezpieczne: req zostaje
         * ustawiony ponownie i kolejny vblank wykona kolejny prologue. */
        if (v & CTL_SNAP_CLR) snap_req_ = true;
        if (v & CTL_OVR_CLR)  overrun_  = false;
        /* DBG_STEP jest strobem, nie trybem (D56): "wyrenderuj jeden pas
         * teraz". Musi isc PO obsludze SNAP_CLR, zeby jeden zapis
         * ENABLE | SNAP_CLR | DBG_STEP oddal liste i od razu ruszyl prologue
         * z pasem 0. */
        if (v & CTL_DBG_STEP) step_strobe();
        return;
    }
    /* Innego rejestru nie ma (D60) — zapis wpada w prozne miejsce. */
}

/* --------------------------------------------------------------- magistrala */

void Mcel::wr(uint16_t addr, uint8_t val) {
    if (cfg_.trace_enabled) {
        uint8_t p[3] = { uint8_t(addr), uint8_t(addr >> 8), val };
        trace_rec(0x01, p, sizeof p);
    }
    /* RAMMAP dekodujemy PRZED oknem — inaczej zapis pod $D1A8 przy oknie
     * wpiętym pod $D000 nie miałby jak zadziałać. Kolejność jest bezpieczna,
     * bo obszary są rozłączne. */
    if (rammap_wr(addr, val)) return;

    /* Paleta CEL (D86) — tez PRZED oknem, z tego samego powodu co RAMMAP.
     * PBIRAMBANK jest zatrzaskiem; strona $DFxx dostaje skladowa wybranego
     * banku. Bank, ktorego model nie zna, polyka zapis i mowi o tym raz. */
    if (addr == cfg_.pbirambank) { pbirambank_ = val; return; }
    if (pbiram_hit(addr)) {
        if (pbiram_bank_is_celpal())
            celpal_[pbirambank_ - PBIRAM_CELPAL_R][addr - cfg_.pbiram_base] = val;
        else
            pbiram_unknown_bank();
        return;
    }

    uint16_t off = 0;
    const int blk = bram_lookup(addr, off);
    if (blk >= 0) {
        /* Zapis 6502 do okna idzie do BRAM (D46, D52) — nie dotyka SDRAM
         * w ogóle, więc nie kosztuje ani jednego taktu budżetu pasa.
         * Poza zakresem bloku model zapis ignoruje; sprzęt nie obiecuje
         * tam niczego, więc pisać tam po prostu nie wolno (D76).
         *
         * ZAPISYWALNE SĄ WEJŚCIA PROLOGUE: SPRLIST i TEXTAB (D55, D58).
         * DERIVED, STRIPMAP i pas to widok — prologue i rasteryzator mają
         * jedyne prawo zapisu do swoich wyjść, a drugi port zapisu do BRAM
         * byłby kosztem w RTL-u za funkcję, którą tryb STEP (D56) załatwia
         * lepiej.
         *
         * TEXTAB jest zapisywalny z tego samego powodu, co SPRLIST: jest
         * WEJŚCIEM, a ścieżka wgrywania urządzenia do BRAM nie sięga, więc
         * gdyby nie 6502, nikt nie miałby jak go wypełnić. */
        /* Strona $14: viewport (D84) i akcelerator kolizji (D85). Siedem
         * bajtow tuz za rekordami sprajtow, w obszarze, ktory poza tym jest
         * poza mapa (D76). Dekodowane PRZED SPRLIST, bo SPRLIST_BYTES konczy
         * sie dokladnie na $1400. */
        if (blk == WIN_SPRLIST && sprwin_is_reg(off)) { sprwin_wr(off, val); return; }
        if (blk == WIN_SPRLIST && off < SPRLIST_BYTES) sprlist_[off] = val;
        if (blk == WIN_TEXTAB  && off < TEXTAB_BYTES)  textab_[off]  = val;
        return;
    }
    const int r = reg_index(addr);
    if (r >= 0) { write_reg(uint8_t(r), val); return; }
    /* Poza oknami blitter nie odpowiada — dekoder urządzenia kieruje gdzie indziej. */
}

/* Ile bajtów bloku jest WAŻNYCH. Reszta okna, aż do 16 KB, leży poza mapą
 * bloku i **sprzęt nie obiecuje tam żadnej wartości** (D76). */
int Mcel::win_block_bytes(int blk) {
    switch (blk) {
    case WIN_SPRLIST:  return SPRLIST_BYTES;
    case WIN_TEXTAB:   return TEXTAB_BYTES;
    case WIN_DERIVED:  return DERIVED_BYTES;
    case WIN_STRIPMAP: return STRIPMAP_BYTES;
    case WIN_STRIP:    return STRIP_BYTES;
    default:           return 0;
    }
}

/* Odczyt jednego bloku BRAM przez okno.
 *
 * POZA ZAKRESEM BLOKU MODEL ODDAJE $FF, ale to NIE JEST KONTRAKT (D76):
 * sprzęt tam niczego nie obiecuje, a coś oddać trzeba, bo ślad ma być
 * deterministyczny. Program, który tę wartość czyta, dostaje ostrzeżenie
 * z Mcel::rd() — i nie wolno na niej niczego opierać. */
uint8_t Mcel::win_rd_block(int blk, uint16_t off) const {
    if (int(off) >= win_block_bytes(blk)) return 0xFF;
    switch (blk) {
    case WIN_SPRLIST:  return sprlist_[off];
    case WIN_DERIVED:  return derived_ram_[off];
    case WIN_STRIPMAP: return (&stripmap_[0][0])[off];
    case WIN_TEXTAB:   return textab_[off];
    case WIN_STRIP: {
        /* Ostatnio wyrenderowany pas (D56). Układ dla 6502 jest LINIOWY —
         * wiersz po wierszu po 320 B — a nie wewnętrznym układem bufora
         * (grupa/offset/lane, mcel_video.h). Bufor wybiera parzystość pasa. */
        const int buf = (step_last_ < 0 ? 0 : step_last_) & 1;
        return sb_.get(buf, int(off / SCREEN_W), int(off % SCREEN_W));
    }
    default: return 0xFF;
    }
}

uint8_t Mcel::rd(uint16_t addr) {
    uint8_t val = 0xFF;
    uint16_t off = 0;
    const int blk = pbiram_hit(addr) ? -1 : bram_lookup(addr, off);
    if (pbiram_hit(addr)) {
        /* Strona PBIRAMBANK (D86): banki $19-$1B oddaja skladowa palety CEL.
         * Inne banki model zna tylko z nazwy — $FF i ostrzezenie, bez skutkow
         * ubocznych poza sladem, jak reszta rd(). */
        if (pbiram_bank_is_celpal())
            val = celpal_[pbirambank_ - PBIRAM_CELPAL_R][addr - cfg_.pbiram_base];
        else
            pbiram_unknown_bank();
    } else if (blk == WIN_SPRLIST && sprwin_is_reg(off)) {
        /* Strona $14 — viewport i status kolizji (D84, D85). Osobno, bo
         * ostrzezenie "poza zakresem bloku" nizej dotyczyloby i tych siedmiu
         * bajtow: SPRLIST konczy sie dokladnie na $1400. */
        val = sprwin_rd(off);
    } else if (blk >= 0) {
        /* Okno jest czytelne (w BRAM to darmowe) i to ono zastąpiło zrzut
         * SDRAM jako mechanizm debugowania (D52, D53, D54). */
        val = win_rd_block(blk, off);
        /* Poza zakresem bloku sprzęt nie obiecuje żadnej wartości (D76).
         * Model musi coś oddać, więc oddaje $FF — ale program, który stamtąd
         * czyta, ma się o tym dowiedzieć TUTAJ, a nie na cudzym Atari. */
        if (int(off) >= win_block_bytes(blk))
            warn("odczyt poza zakresem bloku BRAM — sprzet nie obiecuje tam "
                 "zadnej wartosci; $FF od modelu jest wylacznie po to, zeby "
                 "slad byl deterministyczny (D76)");
        /* BUSY nigdy nie jest w modelu widziane jako 1, bo strip() jest
         * atomowe — więc program, który po strobie STEP czyta pas, nie
         * przeczytawszy ani razu STATUS, przejdzie tu, a na sprzęcie trafiłby
         * w niedokończony bufor. Model tego nie odtworzy; może za to
         * powiedzieć (D56). */
        if (blk == WIN_STRIP && step_unread_)
            warn("odczyt pasa bez czekania na BUSY = 0 — na sprzecie bufor "
                 "bylby niedokonczony (D56)");
    } else {
        const int r = reg_index(addr);
        if (r == REG_STATUS) {
            /* b1:0 to ODCZYT bitow poziomowych CTL na TYCH SAMYCH pozycjach
             * (D60) — nie osobny stan, tylko to samo ENABLE i NODETECT
             * ogladane z drugiej strony. b4:2 sa zerami: pod nimi leza
             * strobe'y, ktore stanu nie maja. */
            val = uint8_t((snap() ? STATUS_SNAP : 0) |
                          (overrun_ ? STATUS_OVERRUN : 0) |
                          (ctl_ & (STATUS_ENABLE | STATUS_NODETECT)));
            /* [model] BUSY czytamy zawsze jako 0 — patrz wyżej. Sam fakt
             * odczytu STATUS zdejmuje jednak podejrzenie o brak czekania. */
            step_unread_ = false;
        }
        /* Innego rejestru nie ma (D60): poza REGBASE blitter nie odpowiada,
         * wiec val zostaje $FF. */
    }
    if (cfg_.trace_enabled) {
        uint8_t p[3] = { uint8_t(addr), uint8_t(addr >> 8), val };
        trace_rec(0x02, p, sizeof p);
    }
    return val;
}

void Mcel::upload(uint32_t sdram_addr, const void *src, size_t n) {
    if (cfg_.trace_enabled) {
        std::vector<uint8_t> p(8 + n);
        for (int i = 0; i < 4; ++i) p[i]     = uint8_t(sdram_addr >> (8 * i));
        for (int i = 0; i < 4; ++i) p[4 + i] = uint8_t(uint32_t(n) >> (8 * i));
        std::memcpy(&p[8], src, n);
        trace_rec(0x03, p.data(), p.size());
    }
    sdram_.poke(sdram_addr, src, n);
}

/* ------------------------------------------------------- paleta CEL (D86) */

void Mcel::pbiram_unknown_bank() {
    char b[96];
    std::snprintf(b, sizeof b,
                  "PBIRAMBANK $%02X nie jest modelowany — blitter zna tylko banki "
                  "palety CEL $%02X-$%02X (D86)",
                  pbirambank_, PBIRAM_CELPAL_R, PBIRAM_CELPAL_B);
    warn(b);
}

/* Sciezka URZADZENIOWA — analog upload(): bez oplaty, z rekordem sladu 0x08.
 * Testbench RTL dostaje z niego palete tak samo, jak z UPLOAD dostaje
 * tekstury; program 6502 idzie zamiast tego przez PBIRAMBANK i zapisy WR. */
void Mcel::set_celpal(const uint8_t rgb[CELPAL_ENTRIES * 3]) {
    if (cfg_.trace_enabled) trace_rec(0x08, rgb, size_t(CELPAL_ENTRIES) * 3);
    for (int i = 0; i < CELPAL_ENTRIES; ++i)
        for (int c = 0; c < CELPAL_COMPONENTS; ++c)
            celpal_[c][i] = rgb[i * 3 + c];
}

void Mcel::celpal_rgb(uint8_t out[CELPAL_ENTRIES * 3]) const {
    for (int i = 0; i < CELPAL_ENTRIES; ++i)
        for (int c = 0; c < CELPAL_COMPONENTS; ++c)
            out[i * 3 + c] = celpal_[c][i];
}

uint8_t Mcel::vid_pixel(unsigned x, unsigned y) const { return sb_.vid_pixel(x, y); }

void Mcel::scanout(unsigned s, uint8_t *out) {
    if (cfg_.trace_enabled) { uint8_t p = uint8_t(s); trace_rec(0x06, &p, 1); }
    sb_.scanout(int(s), out);
}

void Mcel::frame_done() {
    if (cfg_.trace_enabled) trace_rec(0x07, nullptr, 0);
    /* Jeden przerzutnik przełączany na frame_done (§9). Wzór siatki liczy
     * z niego SKAN STRIPMAP, nie prologue. */
    frame_odd_ = !frame_odd_;
}

/* ------------------------------------------------------- tryb STEP (D56) */
/* STEP nie zmienia semantyki — podmienia zrodlo zegara. To jest TA SAMA
 * sekwencja vblank(); strip(s); co w normalnej petli, wywolywana zapisem do
 * rejestru zamiast uplywem czasu.
 *
 * Bufor konsumowany jest na starcie NASTEPNEGO kroku, nie na koncu biezacego:
 * dzieki temu okno WIN_STRIP jest wazne tak dlugo, jak 6502 go potrzebuje,
 * a po pelnym cyklu dziesieciu krokow wychodzi kompletna klatka. Odzwierciedla
 * to sprzet, gdzie bufor i tak czyta tor obrazu dopiero w oknie nastepnego
 * pasa. */
void Mcel::step_strobe() {
    if (!enabled()) return;

    if (step_last_ >= 0) {
        sb_.scanout(step_last_,
                    &step_frame_[size_t(step_last_) * STRIP_H * SCREEN_W]);
        if (step_last_ == STRIPS - 1) frame_done();
        step_last_ = -1;
    }
    if (step_next_ == 0) vblank();        /* prologue, o ile SNAP = 0 */
    strip(unsigned(step_next_));
    step_last_ = step_next_;
    step_next_ = (step_next_ + 1) % STRIPS;
    step_unread_ = true;
}

Derived Mcel::read_derived(int i) const {
    /* Wprost z BRAM (D52) — rekord nigdy nie trafia do SDRAM, więc nie ma
     * czego peekować i nie ma jak zaburzyć liczników transakcji. */
    Derived d;
    derived_unpack(&derived_ram_[size_t(i) * DER_REC_SZ], d);
    return d;
}

/* ------------------------------------------------------------------- ślad */

void Mcel::trace_begin() {
    trace_.clear();
    const char magic[8] = { 'M','C','E','L','T','R','C','1' };
    trace_.insert(trace_.end(), magic, magic + 8);
    cfg_.trace_enabled = true;
}

void Mcel::trace_rec(uint8_t type, const void *payload, size_t n) {
    if (trace_.empty()) trace_begin();
    trace_.push_back(type);
    const uint8_t *p = (const uint8_t *)payload;
    trace_.insert(trace_.end(), p, p + n);
}

} /* namespace mcel */
