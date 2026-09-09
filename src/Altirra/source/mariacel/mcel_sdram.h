/* mcel_sdram.h — pamięć płaska + model transakcji (E0.1 / E0.5).
 *
 * "Transakcje, nie pełna symulacja" (model/README.md): cztery banki
 * z otwartym wierszem plus liczniki. Stąd bierze się przepustowość, z niej
 * takty na pas, a z tego OVERRUN.
 *
 * Mapowanie adresu (02-architecture §7):
 *     adres SŁOWA [23:0] = { row[12:0], bank[1:0], col[8:0] }
 * Banki poniżej wiersza → liniowy strumień przechodzi na kolejny bank
 * co 512 słów = 1024 B, więc ACTIVATE następnego banku chowa się w burscie
 * z bieżącego.
 */
#ifndef MCEL_SDRAM_H
#define MCEL_SDRAM_H

#include <cstdint>
#include <cstddef>
#include <vector>

/* ALTIRRA: device wrapper granted friend access for save states. */
class ATDeviceMaria;

namespace mcel {

/* Stałe czasowe przy 135 MHz (02-architecture §7). Wszystko w taktach.
 * Pola oznaczone [model] nie są w specyfikacji — to parametry modelu,
 * jawnie nazwane, żeby nie udawały liczb z dokumentu. */
struct SdramParams {
    int t_rcd      = 3;      /* ACTIVATE → odczyt/zapis                    */
    int t_rp       = 3;      /* PRECHARGE                                  */
    int t_ras      = 6;      /* minimalny czas otwartego wiersza           */
    int t_rc       = 9;      /* pełny cykl wiersza (używane przy refresh)  */
    int cl         = 3;      /* CAS latency                                */
    int t_refi     = 1054;   /* 64 ms / 8192 przy 135 MHz                  */
    int turnaround = 2;      /* [model] kara za zmianę kierunku RD/WR      */
    int bytes_per_cycle = 2; /* przepustowość danych                       */
};

struct SdramStats {
    uint64_t activates      = 0;   /* ACTIVATE — otwarcie wiersza          */
    uint64_t row_hits       = 0;   /* trafienie w już otwarty wiersz       */
    uint64_t activates_hidden = 0; /* ACTIVATE schowany w trwającym burscie */
    uint64_t precharges     = 0;
    uint64_t refreshes      = 0;
    uint64_t turnarounds    = 0;
    uint64_t bursts_rd      = 0;
    uint64_t bursts_wr      = 0;
    uint64_t bytes_rd       = 0;
    uint64_t bytes_wr       = 0;
    uint64_t cycles_data    = 0;   /* takty samego transferu               */
    uint64_t cycles_overhead= 0;   /* aktywacje, prechargi, turnaroundy    */
    uint64_t cycles_refresh = 0;
    uint64_t cycles_cpu6502 = 0;
};

class Sdram {
    friend class ::ATDeviceMaria;   /* ALTIRRA: save state */
public:
    /* 25 bitów adresu bajtowego = 32 MB, czyli cała pamięć urządzenia. */
    static constexpr uint32_t SIZE = 32u << 20;

    Sdram();

    /* --- ścieżka wgrywania urządzenia: bez opłaty za transakcję ---------
     * To NIE jest dostęp blittera. Tekstury, TEXTAB i początkowa SPRLIST
     * trafiają do SDRAM ścieżką zapisu urządzenia (03-interface-spec §3),
     * więc nie obciążają budżetu pasa.                                    */
    void poke(uint32_t addr, const void *src, size_t n);
    void peek(uint32_t addr, void *dst, size_t n) const;
    uint8_t peek8(uint32_t addr) const {
        return addr < SIZE ? mem_[addr] : 0;
    }
    void poke8(uint32_t addr, uint8_t v) { if (addr < SIZE) mem_[addr] = v; }

    /* --- dostęp blittera: liczony ------------------------------------- */
    /* Zwracają liczbę taktów zajętych przez transakcję. */
    int burst_read (uint32_t addr, void *dst, size_t n);
    int burst_write(uint32_t addr, const void *src, size_t n);

    /* Żądanie 6502 wstrzykiwane przez arbitra (D65).
     * Zamyka strumień blittera — po nim wiersz może wymagać reaktywacji. */
    int cpu_access(uint32_t addr, bool write);

    /* Odświeżanie: PRECHARGE ALL + REFRESH, zamyka wszystkie wiersze. */
    int refresh();

    void reset_banks();
    const SdramStats &stats() const { return st_; }
    SdramStats       &stats()       { return st_; }
    SdramParams      &params()      { return p_; }
    const SdramParams&params() const{ return p_; }

private:
    int  burst(uint32_t addr, size_t n, bool write);
    int  open_row_for(uint32_t word_addr, int prev_data_cycles);

    std::vector<uint8_t> mem_;
    SdramParams p_;
    SdramStats  st_;
    int  open_row_[4] = { -1, -1, -1, -1 };
    bool last_was_write_ = false;
    bool have_direction_ = false;
};

} /* namespace mcel */

#endif /* MCEL_SDRAM_H */
