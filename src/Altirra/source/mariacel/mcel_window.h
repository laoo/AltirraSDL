/* mcel_window.h — okno czasowe pasa: budżet taktów, refresh, dostępy 6502.
 *
 * Przy beam racingu budżetem nie jest klatka, tylko okno jednego pasa
 * (02-architecture §8). Pas ciężki nie może pożyczyć czasu od lekkiego.
 *
 * Model SERIALIZUJE rasteryzację i scanout, podczas gdy sprzęt je nakłada.
 * Piksele są przez to identyczne, a OVERRUN wynika z liczników taktów,
 * a nie z symulacji współbieżności (model/README.md).
 */
#ifndef MCEL_WINDOW_H
#define MCEL_WINDOW_H

#include "mcel.h"

namespace mcel {

struct Mcel::Window {
    Sdram   *sd = nullptr;
    int64_t  used = 0;
    int64_t  limit = 0;
    int64_t  next_refresh = 0;
    int64_t  next_cpu = 0;
    int      cpu_period = 0;
    bool     cpu_enabled = false;
    uint32_t cpu_addr_base = 0;
    uint32_t *cpu_probe = nullptr;

    void open(Sdram *s, int64_t window_cycles, const McelConfig &cfg,
              uint32_t *probe) {
        sd = s;
        used = 0;
        limit = window_cycles;
        next_refresh = s->params().t_refi;
        cpu_period = cfg.cpu6502_period_cycles;
        cpu_enabled = cfg.cpu6502_enabled && cpu_period > 0;
        next_cpu = cpu_enabled ? cpu_period : INT64_MAX;
        cpu_addr_base = cfg.cpu6502_addr;
        cpu_probe = probe;
    }

    /* Dokłada `c` taktów, wstrzykując po drodze zdarzenia okresowe:
     * odświeżanie (tREFI, nie da się odłożyć daleko) i dostępy 6502
     * (twardy deadline, bezwzględny priorytet — D16). */
    void advance(int c) {
        int64_t remaining = c;
        while (remaining > 0) {
            const int64_t ev = next_refresh < next_cpu ? next_refresh : next_cpu;
            if (used + remaining <= ev) break;      /* nic nie wypada po drodze */
            const int64_t step = ev > used ? ev - used : 0;
            used      += step;
            remaining -= step;
            if (next_refresh <= next_cpu) {
                used += sd->refresh();
                next_refresh = ev + sd->params().t_refi;
            } else {
                /* Okno sprajtów nie dotyka SDRAM (D46) — to jest pozostały
                 * ruch 6502 przez okno bankowane urządzenia (patrz McelConfig). */
                const uint32_t a = cpu_addr_base + ((*cpu_probe) & 0x1FFF);
                *cpu_probe += 32;
                used += sd->cpu_access(a, /*write=*/((*cpu_probe) & 0x40) != 0);
                next_cpu = ev + cpu_period;
            }
        }
        used += remaining;
    }

    bool expired() const { return used >= limit; }
};

} /* namespace mcel */

#endif /* MCEL_WINDOW_H */
