/* mcel_rle.h — dekoder pakietów RLE (04-algorithms §10).
 *
 * Dekoder jest **ślepy na BPP** i to nie jest wygoda, tylko cała istota D38:
 * licznik pakietu liczy JEDNOSTKI ŹRÓDŁA, czyli bajty, a nie piksele. Dzięki
 * temu produkuje strumień bajtów, który dopiero rozpakowywacz 4 bpp (§6)
 * zamienia w indeksy — i nie ma kombinacji zabronionej, drugiego wariantu
 * dekodera ani reguły dopełnienia dla nieparzystego licznika.
 *
 * Nagłówek pakietu — 1 bajt:
 *
 *   b7:6 = typ, b5:0 = licznik; liczba jednostek = licznik + 1  (1..64)
 *
 *     00  EOL           koniec wiersza; licznik ignorowany, brak danych
 *     01  LITERAL       count+1 bajtów danych, wprost za nagłówkiem
 *     10  REPEAT        1 bajt danych, powtórzony count+1 razy
 *     11  TRANSPARENT   count+1 bajtów pominiętych, brak danych
 *
 * TRANSPARENT nie niesie wartości — niesie **brak zapisu**. Dekoder oddaje
 * więc obok strumienia bajtów maskę „pominięte", która w sprzęcie jest wprost
 * byte-enable = 0; maska jedzie osobno aż do writera spanów, bo dekoder nie wie
 * ani o palecie, ani o BPP. Byte-enable pozwala pominąć CAŁE SŁOWA bez zapisu
 * do BRAM i to jest powód, dla którego `TRANSPARENT` jest szybszy
 * od `REPEAT($00)`.
 */
#ifndef MCEL_RLE_H
#define MCEL_RLE_H

#include <cstdint>
#include <cstddef>

namespace mcel {

enum : uint8_t {
    RLE_EOL         = 0x00,
    RLE_LITERAL     = 0x40,
    RLE_REPEAT      = 0x80,
    RLE_TRANSPARENT = 0xC0,
    RLE_TYPE_MASK   = 0xC0,
    RLE_COUNT_MASK  = 0x3F,
};
constexpr int RLE_MAX_UNITS = 64;   /* licznik 6-bitowy: count+1 = 1..64 */

struct RleDecodeResult {
    int    produced = 0;    /* ile bajtów źródła wyprodukowano       */
    size_t consumed = 0;    /* ile bajtów strumienia pakietów zjedzono */
    bool   hit_eol  = false;
    bool   malformed = false;
};

/* Dekoduje jeden wiersz. `out` i `skip` muszą mieć po `max_out` bajtów;
 * `skip[i] != 0` znaczy „ten bajt źródła jest pominięty" (TRANSPARENT).
 * Zatrzymuje się na EOL, na wyczerpaniu `max_out` albo na końcu bufora. */
RleDecodeResult rle_decode_row(const uint8_t *packets, size_t avail,
                               uint8_t *out, uint8_t *skip, int max_out);

/* Ile bajtów strumienia zajmuje jeden wiersz, licząc z bajtem EOL.
 * Potrzebne, żeby policzyć ruch na SDRAM jednym burstem zamiast pakiet po
 * pakiecie — dekoder i tak musi przeczytać każdy nagłówek po kolei, bo bez
 * nich nie wie, gdzie są granice pakietów. */
size_t rle_row_length(const uint8_t *packets, size_t avail, bool *malformed = nullptr);

} /* namespace mcel */

#endif /* MCEL_RLE_H */
