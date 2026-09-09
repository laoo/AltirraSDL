/* mcel_helpers.h — warstwa wygody NAD magistralą, nie obok niej.
 *
 * Te funkcje nie robią nic poza wołaniem wr(). Odwzorowują sekwencje
 * z 03-interface-spec §7, więc podwójnie służą jako dokumentacja sterownika.
 * W to miejsce wpina się później prawdziwy emulator 6502 — rdzeń woła
 * wr()/rd() i nic w modelu się nie zmienia.
 *
 * Jeżeli kiedykolwiek pojawi się tu funkcja sięgająca do stanu modelu
 * z pominięciem magistrali, ślad transakcji przestaje być odtwarzalny
 * w testbenchu RTL i cały pomysł się sypie.
 */
#ifndef MCEL_HELPERS_H
#define MCEL_HELPERS_H

#include <cstdint>

#include "mcel.h"

namespace mcel {

/* --- rejestr ------------------------------------------------------------- */
/* Blitter ma JEDEN rejestr (D60): CTL przy zapisie, STATUS przy odczycie,
 * pod adresem bezpośrednim `regbase` — adresowania pośredniego nie używa (D60).
 *
 * `ctl_wr` pisze DOKŁADNIE tę wartość, którą dostał — także wtedy, gdy gasi
 * bit poziomowy. Tak zachowuje się sprzęt. Funkcje niżej robią to, co ma robić
 * oprogramowanie: doczytują bity poziomowe przez `STATUS` i doklejają strobe. */
void ctl_wr        (mcel_t *m, uint8_t v);
uint8_t status_rd  (mcel_t *m);
/* Bity poziomowe (`ENABLE`, `NODETECT`) odczytane przez `STATUS` — b1:0 leżą
 * tam na tych samych pozycjach co w `CTL` (D60). Maska jest obowiązkowa: `SNAP`
 * w b7 `STATUS`-u to `RESET` w b7 `CTL`-a. */
uint8_t ctl_levels (mcel_t *m);
void set_enable    (mcel_t *m, bool on);
void set_nodetect  (mcel_t *m, bool on);
/* Baz nie ma żadnych: SPRLIST (D46), DERIVED (D52) i TEXTAB (D58) leżą
 * w BRAM blittera. Sprajta gasi się przez DRAW = 0 (D48), a NODETECT siedzi
 * w CTL.b1 (D60). */

/* Okna 6502 (D54). Po resecie żadne nie jest wpięte, więc `win_map` jest
 * pierwszą rzeczą, jaką robi program — i pierwszą, jaką robi ta warstwa,
 * zanim dotknie SPRLIST. */
void win_map   (mcel_t *m, int slot, uint8_t block);
void win_unmap (mcel_t *m, int slot);
/* DERIVED leży w BRAM i nie ma bazy (D52). */
void clear_overrun (mcel_t *m);
void device_reset  (mcel_t *m);

/* --- pola sprajta: okno + (strona << 8) + indeks ------------------------- */
void spr_opcode  (mcel_t *m, uint8_t i, uint8_t op, uint8_t palxor = 0);
void spr_texid   (mcel_t *m, uint8_t i, uint8_t t);
/* x/y w 12.4 — piksel to 16, zakres +-2048 px, ziarno 1/16 px (D83). */
void spr_pos     (mcel_t *m, uint8_t i, int16_t x, int16_t y);
void spr_flip    (mcel_t *m, uint8_t i, bool h, bool v);
void spr_draw    (mcel_t *m, uint8_t i, uint8_t draw);   /* cały bajt {PATX,PAT} */
void spr_skip    (mcel_t *m, uint8_t i, bool skip);      /* D40: odwrotność DRAW */
/* w/h w 12.4, bez znaku — 0..4095,9375 px (D83). */
void spr_scale   (mcel_t *m, uint8_t i, uint16_t w, uint16_t h);
/* pivx/pivy w 12.4, OD SRODKA tekstury; zera znacza obrot wokol srodka (D82). */
void spr_rotscale(mcel_t *m, uint8_t i, uint8_t ang,
                  uint16_t sx, uint16_t sy, int16_t pivx = 0, int16_t pivy = 0);
void spr_affine  (mcel_t *m, uint8_t i, int16_t eux, int16_t euy,
                  int16_t evx, int16_t evy);
/* QUAD: czwarty rog jako wektor P0 -> P2, w tym samym formacie 12.4 co EU/EV
 * (D45). HDD blitter liczy sam. Wolac PO spr_affine, ktore ustawia EU i EV. */
void spr_quad    (mcel_t *m, uint8_t i, int16_t ewx, int16_t ewy);

/* hit-box: PRZESUNIECIA WLASNYCH ROGOW sprajta, int16 12.4 w tekselach ze
 * znakiem (D85). Zera znacza hit-box ROWNY sprajtowi, wiec wolanie tej funkcji
 * jest potrzebne tylko wtedy, gdy prostokat kolizji ma sie od sprajta roznic.
 * Tylko BLIT i SCALE — przy pozostalych opcodach te strony niosa geometrie. */
void spr_hitbox  (mcel_t *m, uint8_t i, int16_t dx0, int16_t dy0,
                  int16_t dx1, int16_t dy1);

/* --- strona $14 okna SPRLIST: viewport i kolizje ------------------------- */
/* Viewport (D84): pozycja piksela widocznego w LEWYM GORNYM ROGU ekranu,
 * int16 12.4. Zeby punkt (0,0) przestrzeni renderingu wypadl na srodku
 * ekranu: set_viewport(m, -160*16, -120*16). */
void set_viewport(mcel_t *m, int16_t vpx, int16_t vpy);

/* Akcelerator kolizji (D85). Zapisuje COLA i COLBMAX (zatrzaski), potem COLB —
 * i to on startuje skan. Zwraca bajt statusu; przy COL_FOUND indeks trafionego
 * czyta sie z COLB przez collide_index().
 *
 * Para bez skanu: bmax <= b (np. bmax = 0). */
uint8_t collide  (mcel_t *m, uint8_t a, uint8_t b, uint8_t bmax = 0);
uint8_t collide_index(mcel_t *m);

/* --- deskryptor tekstury: okno WIN_TEXTAB, czyli 6502 -------------------- */
/* TEXTAB leży w BRAM (D58), tak samo jak SPRLIST (D46), więc ścieżka wgrywania
 * urządzenia do żadnej z nich nie sięga. Te dwie funkcje piszą przez okno,
 * dokładnie tak jak `spr_*`, tylko slotem 1 (`$8000`), żeby SPRLIST mógł
 * zostać wpięty w slot 0 przez całą klatkę. */
struct TexDesc {
    uint32_t base_byte = 0;    /* adres BAJTOWY; musi być PARZYSTY (D37)     */
    uint16_t stride    = 0;    /* w bajtach                                   */
    uint16_t width     = 1;    /* w tekselach (do rekordu idzie width-1)      */
    uint16_t height    = 1;
    uint8_t  palofs    = 0;    /* b3:0                                        */
    uint8_t  bpp_code  = FMT_BPP_8;
    int      rle_width = 0;    /* 0 = bez RLE; 1/2/3 = szerokość wpisu tablicy (D44) */
};
void tex_write(mcel_t *m, uint8_t id, const TexDesc &t);

/* Deskryptor SUROWY: szesnascie bajtow rekordu TEXTAB wprost, bez skladania
 * FORMAT-u i bez odejmowania jedynki od wymiarow. Istnieje po to, zeby dalo
 * sie opisac deskryptor, ktorego zaden obrazek nie wyprodukuje — NIBOFS,
 * rezerwe b7:5, WIDTH = $FFFF, RLEW = 3. Prologue czyta deskryptory, nie
 * teksele, wiec dla niego to pelnoprawne wejscie (docs/12 §2). */
void tex_write_raw(mcel_t *m, uint8_t id, const uint8_t rec[TEX_REC_SZ]);

/* --- protokół klatki ----------------------------------------------------- */
/* SNAP_CLR, potem czekaj aż SNAP wróci do 1. Odtwarza pętlę z §2:
 *      wait:  bit REGBASE+0 / bpl wait
 * Model nie ma własnego napędu czasu, więc oczekiwanie napędza vblank() —
 * dokładnie tak, jak 6502 kręci się w pętli, dopóki blitter nie skończy. */
void mcel_commit(mcel_t *m, int max_vblanks = 4);

/* Jedna pełna klatka: vblank → pasy 0..9 → scanout → frame_done.
 * `out` (320*240) może być nullptr. */
void render_frame(mcel_t *m, uint8_t *out);

} /* namespace mcel */

#endif /* MCEL_HELPERS_H */
