/* mcel_fixed.h — arytmetyka stałoprzecinkowa modelu referencyjnego MariaCEL.
 *
 * Trzy rzeczy, które MUSZĄ być tu od pierwszej linijki, bo inaczej model
 * rozjedzie się z przyszłym RTL-em w sposób trudny do zdiagnozowania:
 *
 *   D24 — FRAC_BITS jest stałą, nigdzie nie ma zaszytego ">> 16".
 *   D33 — dzielenie na modułach, znak doklejany po. "/" w C++ obcina do zera,
 *         dzielnik restoring w RTL liczy w dół; wspólna konwencja to moduły.
 *   D35 — kąty kardynalne omijają tablicę sinusa: 1.0 nie mieści się w int16.
 *
 * Odniesienia: docs/04-algorithms.md §1, §2; docs/05-decisions.md D20/D24/D33/D35.
 */
#ifndef MCEL_FIXED_H
#define MCEL_FIXED_H

#include <cstdint>
#include <cstdlib>

#include "mcel_sintab.h"

namespace mcel {

/* ---------------------------------------------------------------- format */

/* D24: parametr, nie stała rozsiana po kodzie. Człon c·r·HDD (QUAD)
 * akumuluje się przez W·H kroków, więc może wymagać poszerzenia. */
constexpr int     FRAC_BITS = 16;
using             fixed_t   = int32_t;
constexpr fixed_t FX_ONE    = fixed_t(1) << FRAC_BITS;
constexpr fixed_t FX_HALF   = FX_ONE >> 1;

constexpr fixed_t fx_int  (int32_t v) { return (fixed_t)((uint32_t)v << FRAC_BITS); }

/* WSZYSTKIE pola geometryczne SPRLIST sa w formacie 12.4 (D83): DSTX/DSTY,
 * DSTW/DSTH, EUx/EVx/EWx, PIVX/PIVY. Jeden format na cala geometrie rekordu,
 * jeden shift na wejsciu do 16.16 — i tak samo jak FRAC_BITS jest to
 * PARAMETR, a nie "<< 12" rozsiane po kodzie (D24). */
constexpr int     FIELD_FRAC = 4;
constexpr int     FIELD_SH   = FRAC_BITS - FIELD_FRAC;
constexpr fixed_t fx_field(int32_t v) { return (fixed_t)((uint32_t)v << FIELD_SH); }
constexpr int32_t fx_floor(fixed_t v) { return v >> FRAC_BITS; }   /* arytmetyczne */
constexpr fixed_t fx_frac (fixed_t v) { return v & (FX_ONE - 1); }

/* Mnożenie dwóch wielkości 16.16 → 16.16, z zejściem przez 64 bity.
 * ">> FRAC_BITS" na wartości ujemnej to shift arytmetyczny, czyli floor —
 * tak samo jak obcięcie młodszych bitów w RTL. */
inline fixed_t fx_mul(int64_t a, int64_t b) { return (fixed_t)((a * b) >> FRAC_BITS); }

/* Nasycenie do int32 z opcjonalnym zgłoszeniem. RTL ma szerokość skończoną,
 * więc model musi wiedzieć, kiedy z niej wyszedł — cicha zawijka to
 * dokładnie ta klasa błędów, której ma nie być. */
inline int32_t fx_sat32(int64_t v, bool *overflow = nullptr) {
    if (v >  INT32_MAX) { if (overflow) *overflow = true; return INT32_MAX; }
    if (v <  INT32_MIN) { if (overflow) *overflow = true; return INT32_MIN; }
    return (int32_t)v;
}

/* ------------------------------------------------------- dzielenie (D33) */

/* Jedyne miejsce w modelu, w którym wolno użyć operatora "/" na wartościach
 * ze znakiem. Wszystko inne przechodzi przez te trzy funkcje.
 *
 * Dzielnik restoring liczy iloraz MODUŁÓW i osobno wystawia znak; C++ obcina
 * do zera. Żeby obie strony dawały bit w bit to samo, model liczy tak jak RTL:
 *
 *      q = |a| / |b| ;  jeśli sign(a) != sign(b): q = -q
 *
 * (To jest obcięcie do zera. Zapisane jawnie przez moduły, a nie zostawione
 *  operatorowi "/", żeby przy przenoszeniu do VHDL nie było czego zgadywać.) */
struct DivResult {
    uint64_t qmag;   /* |a| / |b|  — dokładnie to, co wystawia dzielnik      */
    uint64_t rmag;   /* |a| % |b|  — reszta, też dostępna w restoring        */
    bool     neg;    /* sign(a) != sign(b) — ZNAK ILORAZU, nie znak wyniku.
                      * Nie tłumimy go przy qmag == 0: dla div_d33 nie ma to
                      * znaczenia (bo -0 == 0), ale ceil_div_d33 wybiera po nim
                      * KIERUNEK ZAOKRĄGLENIA. Iloraz w (-1, 0) ma qmag == 0
                      * i jest ujemny — jego sufit to 0, a nie 1. */
};

inline DivResult div_mag(int64_t a, int64_t b) {
    uint64_t ua = (uint64_t)(a < 0 ? -(uint64_t)a : (uint64_t)a);
    uint64_t ub = (uint64_t)(b < 0 ? -(uint64_t)b : (uint64_t)b);
    DivResult r;
    r.qmag = ub ? ua / ub : 0;
    r.rmag = ub ? ua % ub : 0;
    r.neg  = (a < 0) != (b < 0);
    return r;
}

/* Dzielenie całkowite z konwencją D33. b == 0 → 0; wywołujący ma obsłużyć
 * przypadek zdegenerowany osobno (det == 0, HDY == 0, ...). */
inline int64_t div_d33(int64_t a, int64_t b) {
    if (b == 0) return 0;
    DivResult d = div_mag(a, b);
    return d.neg ? -(int64_t)d.qmag : (int64_t)d.qmag;
}

/* Dzielenie stałoprzecinkowe: (a << FRAC_BITS) / b, wynik 16.16.
 * Konwencja znaku: D33. */
inline fixed_t fx_div(int64_t a, int32_t b, bool *overflow = nullptr) {
    if (b == 0) return 0;
    return fx_sat32(div_d33(a << FRAC_BITS, b), overflow);
}

/* Odwrotność: (1 << 2*FRAC_BITS) / x, wynik 16.16. Zero dla x == 0 —
 * tak jak DERIVED.RCP_HDY ma być zerem przy HDY == 0 (§1). */
inline fixed_t fx_recip(int32_t x, bool *overflow = nullptr) {
    return fx_div(FX_ONE, x, overflow);
}

/* Sufit ilorazu, dokładny — potrzebny w solverze zakresów r/c (§4).
 * Zbudowany z ilorazu i reszty MODUŁÓW, czyli z tego, co dzielnik restoring
 * i tak wystawia: dla wyniku dodatniego dokładamy 1 przy niezerowej reszcie,
 * dla ujemnego obcięcie do zera JEST sufitem. */
inline int64_t ceil_div_d33(int64_t a, int64_t b) {
    if (b == 0) return 0;
    DivResult d = div_mag(a, b);
    /* Dla ilorazu ujemnego obcięcie do zera JEST sufitem — także wtedy, gdy
     * moduł ilorazu wynosi zero, czyli dla wartości z przedziału (-1, 0). */
    if (d.neg) return -(int64_t)d.qmag;
    return (int64_t)d.qmag + (d.rmag != 0 ? 1 : 0);
}

/* --------------------------------------------------- sinus / cosinus (D35) */

/* Tablica ma 1.0 = 0x7FFF, więc SIN_ONE (32768) NIE MIEŚCI SIĘ w int16.
 * Kąty kardynalne muszą więc omijać tablicę, a wynik musi być int32.
 *
 * Gdyby ANGLE = 0 liczyć z tablicy, wyszłoby HDX = 65534 zamiast 65536 —
 * detekcja MODE = LINE (§2) by nie zadziałała i sprajt nieobrócony
 * renderowałby się ścieżką afiniczną ~6× wolniej, a test
 * "ROTSCALE(0°, 1.0) ≡ BLIT" nie przeszedłby bit w bit. */
constexpr int32_t SIN_ONE = int32_t(1) << SIN_FRAC;   /* 32768 = dokładne 1.0 */

struct SinCos { int32_t ca, sa; };                    /* format 1.15, ale int32 */

inline SinCos sincos_1_15(uint8_t angle) {
    if ((angle & 0x3F) == 0) {                        /* 0, 64, 128, 192 */
        static const int32_t k[4][2] = {
            {  SIN_ONE,        0 },                   /*   0° */
            {        0,  SIN_ONE },                   /*  90° */
            { -SIN_ONE,        0 },                   /* 180° */
            {        0, -SIN_ONE },                   /* 270° */
        };
        return { k[angle >> 6][0], k[angle >> 6][1] };
    }
    return { (int32_t)COS(angle), (int32_t)SIN(angle) };
}

/* ---------------------------------------------------------------- pomocne */

template <typename T> constexpr T clampv(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

} /* namespace mcel */

#endif /* MCEL_FIXED_H */
