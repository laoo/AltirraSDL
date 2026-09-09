#include "mcel_ranges.h"

#include <algorithm>

namespace mcel {

/* Sonda: model jest jednowatkowy, wiec zwykla zmienna wystarczy. */
static int64_t g_max_num = 0;
void    ranges_reset_probe()   { g_max_num = 0; }
int64_t ranges_max_numerator() { return g_max_num; }

namespace {

inline void probe(int64_t A, int64_t B) {
    const int64_t a = A < 0 ? -A : A, b = B < 0 ? -B : B;
    if (a > g_max_num) g_max_num = a;
    if (b > g_max_num) g_max_num = b;
}

/* Sufit ilorazu dwóch wielkości 16.16 → liczba całkowita tekseli.
 * Dokładny (D33 + reszta), bo model może — patrz nagłówek mcel_ranges.h. */
inline int64_t ceil_q(int64_t num_fx, fixed_t den_fx) {
    return ceil_div_d33(num_fx, den_fx);
}

inline int64_t fxmin0(fixed_t v) { return v < 0 ? v : 0; }
inline int64_t fxmax0(fixed_t v) { return v > 0 ? v : 0; }

/* Wspólny kształt obu zakresów z §4: pas wyznaczony przez A i B, dzielony
 * przez krok, z gałęzią na znak kroku i przypadkiem zdegenerowanym krok == 0.
 *
 *   krok > 0:  [ceil(A/krok), ceil(B/krok))
 *   krok < 0:  [ceil(B/krok), ceil(A/krok))     <- dzielenie odwraca nierówność
 *   krok == 0: pełny [0, n) albo pusto — cała linia leży po jednej stronie
 */
Range solve(int64_t A, int64_t B, fixed_t step, int n, bool degenerate_hit) {
    Range r;
    if (step) probe(A, B);
    if (step > 0)      { r.lo = (int)ceil_q(A, step); r.hi = (int)ceil_q(B, step); }
    else if (step < 0) { r.lo = (int)ceil_q(B, step); r.hi = (int)ceil_q(A, step); }
    else               { if (degenerate_hit) { r.lo = 0; r.hi = n; } else { r.lo = r.hi = 0; } }
    return r;
}

/* Podłoga ilorazu — przez sufit, żeby zostać przy jednej konwencji (D33). */
inline int64_t floor_div_d33(int64_t a, int64_t b) { return -ceil_div_d33(-a, b); }

/* Zbiór r w [0,n) spełniających  a + r·s >= t. */
Range half_ge(int64_t a, int64_t s, int64_t t, int n) {
    Range r{0, n};
    if (s > 0)      r.lo = (int)ceil_div_d33(t - a, s);
    else if (s < 0) r.hi = (int)floor_div_d33(t - a, s) + 1;
    else if (a < t) { r.lo = 0; r.hi = 0; }
    return r;
}

/* Zbiór r w [0,n) spełniających  a + r·s < t. */
Range half_lt(int64_t a, int64_t s, int64_t t, int n) {
    Range r{0, n};
    if (s > 0)       r.hi = (int)ceil_div_d33(t - a, s);
    else if (s < 0)  r.lo = (int)floor_div_d33(t - a, s) + 1;
    else if (a >= t) { r.lo = 0; r.hi = 0; }
    return r;
}

inline Range isect(Range a, Range b) {
    return Range{ std::max(a.lo, b.lo), std::min(a.hi, b.hi) };
}

} /* anon */

Range range_r_quad(const Derived &d, int Y0, int Y1) {
    const int     H  = (int)d.src_h;
    const int64_t W  = d.src_w;
    const int64_t y0 = int64_t(Y0) << FRAC_BITS;
    const int64_t y1 = int64_t(Y1) << FRAC_BITS;

    /* Rozciągłość pozioma wiersza r na ekranie: f(r) = W·HDY + r·(W·HDDY). */
    const int64_t L = W * d.hdy;
    const int64_t S = W * d.hddy;

    /* Rozciągłość pionowa teksela — przy pełnym HDD lewa krawędź teksela c ma
     * dy = VDY + c·HDDY, więc bierzemy skrajne po całym c (nadmiarowo). */
    const int64_t v0 = d.vdy, v1 = int64_t(d.vdy) + W * d.hddy;
    const int64_t vlo = std::min<int64_t>(0, std::min(v0, v1));
    const int64_t vhi = std::max<int64_t>(0, std::max(v0, v1));

    /* Wiersz r dotyka pasa gdy jednocześnie
     *   yv_r + max(0, f(r)) + vhi >= Y0        (górna krawędź sięga pasa)
     *   yv_r + min(0, f(r)) + vlo <  Y1        (dolna krawędź jeszcze w pasie)
     * gdzie yv_r = YPOS + r·VDY. Po obu stronach r* obie są liniowe. */
    const int64_t P = d.ypos;

    /* Gałąź 1: f(r) <= 0  →  max(0,f) = 0, min(0,f) = f */
    Range b1 = isect(half_lt(L, S, 1, H),                       /* f <= 0        */
              isect(half_ge(P + vhi,      d.vdy,     y0, H),     /* warunek górny */
                    half_lt(P + L + vlo,  d.vdy + S, y1, H)));   /* warunek dolny */

    /* Gałąź 2: f(r) >= 0  →  max(0,f) = f, min(0,f) = 0 */
    Range b2 = isect(half_ge(L, S, 0, H),
              isect(half_ge(P + L + vhi,  d.vdy + S, y0, H),
                    half_lt(P + vlo,      d.vdy,     y1, H)));

    Range out;
    if (b1.empty() && b2.empty()) { out.lo = 0; out.hi = 0; }
    else if (b1.empty())          out = b2;
    else if (b2.empty())          out = b1;
    else { out.lo = std::min(b1.lo, b2.lo); out.hi = std::max(b1.hi, b2.hi); }

    /* Margines ±1 teksel, tak jak w §4. */
    out.lo = clampv<int>(out.lo - 1, 0, H);
    out.hi = clampv<int>(out.hi + 1, 0, H);
    return out;
}

Range range_r(const Derived &d, int Y0, int Y1) {
    const int64_t W = d.src_w, H = d.src_h;
    const int64_t y0 = int64_t(Y0) << FRAC_BITS;
    const int64_t y1 = int64_t(Y1) << FRAC_BITS;

    /* Wiersz r startuje w yv_r = YPOS + r·VDY i rozciąga się pionowo o grubość
     * wynikającą z W·HDY (długość wiersza na ekranie) i VDY (wysokość teksela). */
    const int64_t whdy = W * d.hdy;
    const int64_t mlo = std::min<int64_t>(0, whdy) + fxmin0(d.vdy);
    const int64_t mhi = std::max<int64_t>(0, whdy) + fxmax0(d.vdy);

    const int64_t A = y0 - mhi - d.ypos;
    const int64_t B = y1 - mlo - d.ypos;

    /* Przy VDY == 0 wszystkie wiersze leżą na tej samej wysokości: albo pas
     * dotyka ich wszystkich, albo żadnego. */
    const bool hit = (mhi >= y0 - d.ypos) && (mlo < y1 - d.ypos);
    Range r = solve(A, B, d.vdy, (int)H, hit);

    /* Margines ±1 teksel — patrz nagłówek. */
    r.lo = clampv<int>(r.lo - 1, 0, (int)H);
    r.hi = clampv<int>(r.hi + 1, 0, (int)H);
    return r;
}

Range range_c(const Derived &d, int r, int Y0, int Y1) {
    return range_c(d, r, Y0, Y1, d.hdx, d.hdy,
                   fxmin0(d.vdy), fxmax0(d.vdy));
}

Range range_c(const Derived &d, int r, int Y0, int Y1,
              fixed_t hdx_row, fixed_t hdy_row, int64_t vdy_lo, int64_t vdy_hi) {
    const int64_t W = d.src_w;
    const int64_t y0 = int64_t(Y0) << FRAC_BITS;
    const int64_t y1 = int64_t(Y1) << FRAC_BITS;

    const int64_t yv = int64_t(d.ypos) + int64_t(r) * d.vdy;
    const int64_t xv = int64_t(d.xpos) + int64_t(r) * d.vdx;

    /* --- ograniczenie z pasa: y(c) = yv + c·HDY, teksel rozciąga się o VDY --- */
    const int64_t tlo = vdy_lo, thi = vdy_hi;
    const bool hit_y = (yv + thi >= y0) && (yv + tlo < y1);
    const Range cy = solve(y0 - thi - yv, y1 - tlo - yv, hdy_row, (int)W, hit_y);

    /* --- ograniczenie z krawędzi ekranu: x(c) = xv + c·HDX, rozciąga się o VDX --- */
    const int64_t ulo = fxmin0(d.vdx), uhi = fxmax0(d.vdx);
    const int64_t sx1 = int64_t(SCREEN_W) << FRAC_BITS;
    const bool hit_x = (xv + uhi >= 0) && (xv + ulo < sx1);
    const Range cx = solve(0 - uhi - xv, sx1 - ulo - xv, hdx_row, (int)W, hit_x);

    Range c;
    c.lo = clampv<int>(std::max(cy.lo, cx.lo) - 1, 0, (int)W);
    c.hi = clampv<int>(std::min(cy.hi, cx.hi) + 1, 0, (int)W);
    return c;
}

/* ------------------------------------------------------------------- P7 --- */

RcpSweep rcp_sweep(int rcp_frac_bits, int span_px, int samples_per_divisor) {
    RcpSweep out;
    out.frac_bits = rcp_frac_bits;
    out.span_px   = span_px;

    /* Dzielniki: realne kroki geometryczne. VDY/HDX/HDY mieszczą się między
     * ~1/16 teksela na piksel (skala 16:1) a 16 pikseli na teksel (1:16),
     * plus wartości ujemne (odbicia i obroty) i kilka wartości sin/cos. */
    static const int32_t steps[] = {
        FX_ONE / 16, FX_ONE / 8, FX_ONE / 4, FX_ONE / 3, FX_ONE / 2,
        (FX_ONE * 2) / 3, FX_ONE - 1, FX_ONE, FX_ONE + 1,
        (FX_ONE * 3) / 2, FX_ONE * 2, FX_ONE * 3, FX_ONE * 5, FX_ONE * 16,
        23197, 46341, 12345, 65535, 4096, 1024, 337, 65534,
    };

    const int64_t span = int64_t(span_px) << FRAC_BITS;
    const int64_t shift = FRAC_BITS + rcp_frac_bits;
    const int64_t one = int64_t(1) << rcp_frac_bits;

    for (int sgn = 0; sgn < 2; ++sgn) {
        for (int32_t mag : steps) {
            const int32_t step = sgn ? -mag : mag;
            const int64_t rcp = div_d33(one << FRAC_BITS, step);   /* konwencja D33 */
            if (rcp == 0) continue;

            const int64_t stride = std::max<int64_t>(1, (2 * span) / samples_per_divisor);
            /* Granice zakresu i okolice zera badane gesto — tam wypadaja
             * przypadki brzegowe sufitu (iloraz dokladnie calkowity). */
            for (int64_t A = -span; A <= span; A += stride) {
                for (int64_t k = -2; k <= 2; ++k) {
                    const int64_t a = A + k;
                    const int64_t exact = ceil_div_d33(a, step);
                    const int64_t prod  = a * rcp;
                    const int64_t q     = prod >> shift;                    /* podłoga  */
                    const int64_t approx = (prod & ((int64_t(1) << shift) - 1)) ? q + 1 : q;
                    const int64_t err = exact - approx;
                    out.samples++;
                    if (err) {
                        out.mismatches++;
                        const int64_t ae = err < 0 ? -err : err;
                        if (ae > out.max_abs_error) out.max_abs_error = ae;
                    }
                }
            }
        }
    }
    out.safe = out.max_abs_error <= 1;
    return out;
}

} /* namespace mcel */
