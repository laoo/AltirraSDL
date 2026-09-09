/* mcel_ranges.h — solver zakresów r i c (04-algorithms §4).
 *
 * "To jest jedyna nietrywialna część algorytmu." Chodzi o to, by dla danego
 * pasa odwiedzić TYLKO te teksele, które w niego trafiają — i o to, żeby
 * nigdy nie odwiedzić za mało. Zakresy nadmiarowe kosztują takty; zakresy
 * niedoszacowane dają dziury na granicy pasa, czyli całą klasę błędów,
 * dla której powstał test "pasami ≡ cała klatka".
 *
 * Dwie rzeczy, które trzeba tu wiedzieć:
 *
 * 1. Zakresy są POSZERZANE o ±1 teksel (`r0-1`, `r1+1`). To nie jest asekuracja
 *    na wszelki wypadek — to jest to, co pozwala liczyć granice przez mnożenie
 *    przez odwrotność zamiast przez dzielenie (D25). Błąd rzędu ULP w RCP_*
 *    przesuwa granicę pętli, którą margines wchłania, więc P7 jest pytaniem
 *    o TAKTY, nie o piksele. Inaczej niż przy IA..ID (§8), gdzie kilka ULP
 *    przewraca test pokrycia i daje pojedyncze piksele różnicy.
 *
 * 2. Model liczy sufit DOKŁADNIE (`ceil_div_d33`), a nie przez mnożenie
 *    przez RCP_*. Wolno mu, właśnie dzięki marginesowi z punktu 1 — a
 *    `rcp_sweep()` mierzy, przy jakim formacie RCP_* obie drogi mieszczą się
 *    w tym marginesie. To jest domykanie P7.
 */
#ifndef MCEL_RANGES_H
#define MCEL_RANGES_H

#include "mcel_derived.h"
#include "mcel_fixed.h"
#include "mcel_regs.h"

namespace mcel {

/* Zakres półotwarty [lo, hi). lo >= hi znaczy "pusto". */
struct Range { int lo = 0, hi = 0; bool empty() const { return lo >= hi; } };

/* Zakres wierszy źródła dotykających pasa [Y0, Y1) — raz na sprajta w pasie.
 * Y0/Y1 w pikselach ekranu. */
Range range_r(const Derived &d, int Y0, int Y1);

/* Zakres kolumn w wierszu r — raz na wiersz źródła. Przecięcie dwóch
 * niezależnych ograniczeń: pionowego (pas) i poziomego (krawędzie ekranu). */
Range range_c(const Derived &d, int r, int Y0, int Y1);

/* Wariant dla QUAD: `HDX` i `HDY` nie są stałymi sprajta, tylko krokami
 * krawędzi WIERSZA `r` (`HDX + r·HDDX`, `HDY + r·HDDY`). Formuła jest ta sama —
 * zmienia się wyłącznie źródło argumentów, dokładnie jak przewidziało D25.
 *
 * `vdy_lo`/`vdy_hi` to skrajne wysokości teksela w tym wierszu: przy pełnym
 * `HDD` lewa krawędź teksela `c` ma `dy = VDY + c·HDDY`, więc rozciągłość
 * pionowa zależy od kolumny. Bierzemy skrajne po całym `c` — zakres wychodzi
 * nadmiarowy, a nadmiarowy jest bezpieczny (§4). */
Range range_c(const Derived &d, int r, int Y0, int Y1,
              fixed_t hdx_row, fixed_t hdy_row, int64_t vdy_lo, int64_t vdy_hi);

/* Zakres wierszy dla QUAD z pełnym `HDD`.
 *
 * Przy `HDDY != 0` wzór z §4 SIĘ ROZGAŁĘZIA: rozciągłość wiersza `r` to
 * `W·HDY_r = W·HDY + r·W·HDDY`, więc `max(0, ·)` i `min(0, ·)` zmieniają
 * gałąź w punkcie `r* = −HDY/HDDY`. Po obu stronach `r*` warunek jest znów
 * liniowy, więc rozwiązanie to suma dwóch przedziałów.
 *
 * Zwraca przedział OTACZAJĄCY zbiór rozwiązań. Zbiór bywa niespójny (warunek
 * to przecięcie wypukłego z wklęsłym), a pętla rasteryzacji potrzebuje
 * przedziału — otoczka jest nadzbiorem, czyli bezpieczna. */
Range range_r_quad(const Derived &d, int Y0, int Y1);

/* --- domykanie P7 ---------------------------------------------------------
 * Dla zadanej liczby bitów ułamkowych RCP_* sprawdza, o ile granica liczona
 * przez mnożenie przez odwrotność różni się od granicy dokładnej. Zwraca
 * największą zaobserwowaną różnicę w tekselach. Wynik <= 1 znaczy, że
 * margines ±1 z §4 ją wchłania i format jest bezpieczny. */
struct RcpSweep {
    int      frac_bits = 0;
    int      span_px = 0;         /* zakres licznika A, w pikselach ekranu    */
    int64_t  max_abs_error = 0;   /* w tekselach                              */
    uint64_t samples = 0;
    uint64_t mismatches = 0;      /* ile razy sufit wyszedł inny niż dokładny */
    bool     safe = false;        /* max_abs_error <= 1 (margines §4)         */
};

/* Zależność jest analityczna i sweep tylko ją potwierdza:
 *
 *     rcp = floor(2^(n+16) / krok)   ma błąd bezwzględny < 1 jednostki,
 *     więc  |A·rcp / 2^(n+16)  −  A/krok|  <  |A| / 2^(n+16)
 *
 * Żeby ten błąd nie przekroczył jednego teksela (czyli marginesu ±1 z §4),
 * potrzeba `2^n > |A|_max wyrażone w PIKSELACH`. Dlatego zakres licznika jest
 * parametrem, a nie stałą — odpowiedź na P7 to funkcja tego zakresu.
 *
 * `span_px` musi ograniczać |Y0 − mhi − YPOS| dla sprajtów, które w ogóle
 * przechodzą przez STRIPMAP, czyli takich, których bbox tnie ekran:
 *     |YPOS| <= SCREEN_H + |W·HDY| + |H·VDY|                                */
RcpSweep rcp_sweep(int rcp_frac_bits, int span_px, int samples_per_divisor = 512);

/* Instrumentacja: największy |A| (w 16.16), jaki solver faktycznie zobaczył.
 * Pozwala oprzeć wybór formatu RCP_* na scenach, a nie na oszacowaniu. */
void    ranges_reset_probe();
int64_t ranges_max_numerator();

} /* namespace mcel */

#endif /* MCEL_RANGES_H */
