/* mcel_regs.h — wszystkie stałe kontraktu: rejestry, offsety rekordów, geometria.
 *
 * Jedno źródło dla modelu, loadera scen i testów. Nazwy 1:1 z
 * docs/03-interface-spec.md, żeby dało się czytać spec i kod obok siebie.
 *
 * Umiejscowienie rejestrów i okna w przestrzeni 6502 ustala dekoder adresowy
 * URZĄDZENIA, nie blitter — dlatego są to parametry konfiguracji, a nie
 * zaszyte adresy. Wartości domyślne to D53.
 */
#ifndef MCEL_REGS_H
#define MCEL_REGS_H

#include <cstdint>

namespace mcel {

/* ---------------------------------------------- generiki RTL (§1, nie rejestry) */
constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;
constexpr int STRIP_H  = 24;
constexpr int STRIPS   = 10;
constexpr int NBUF     = 2;

constexpr int SPR_COUNT   = 256;
constexpr int SPR_PAGES   = 20;    /* strony $00-$13; $14-$1F poza mapa (D47) */
                                   /* poza mapa nie ma zdefiniowanej wartosci (D76), */
                                   /* z wyjatkiem $1400-$1406 — viewport i kolizje */
                                   /* (D84, D85), patrz SPRWIN_REGS nizej */
constexpr int SPR_REC_SZ  = SPR_PAGES;   /* 20 B — rekord spakowany (D47)     */
constexpr int TEX_REC_SZ  = 16;    /* 10 §3.2 */
constexpr int DER_REC_SZ  = 64;    /* §1 04-algorithms — twarde: adres = i << 6 */

/* SPRLIST mieszka w BRAM blittera, nie w SDRAM (D46). 256 x 20 B = 5 120 B,
 * czyli DOKLADNIE 8 blokow po 640 B — zero marnotrawstwa, tak samo jak bufor
 * pasa 320x24. Adres BRAM to wprost A[12:0] okna, bo (strona << 8) | indeks
 * TO JEST A[12:0] po obu stronach — transpozycji nie ma (D46). */
constexpr int SPRLIST_BYTES = SPR_PAGES * SPR_COUNT;   /* 5 120 */

constexpr int      STRIPMAP_BYTES = SPR_COUNT * 2;             /* 512    */
constexpr int      DERIVED_BYTES  = SPR_COUNT * DER_REC_SZ;    /* 16 384 */
/* TEXTAB tez mieszka w BRAM (D58). 256 x 16 B = 4 096 B, czyli 6,4 bloku po
 * 640 B — 7 blokow z 384 B zmarnowanymi. Adres w oknie to (TEXID << 4) | ofs,
 * czyli czyste ciecie bitow, tak samo jak i << 6 przy DERIVED. */
constexpr int      TEXTAB_BYTES   = SPR_COUNT * TEX_REC_SZ;    /*  4 096 */
/* Wyrenderowany pas w oknie 6502 (D56) — 320 x 24 B, uklad liniowy
 * wiersz po wierszu. To NIE jest wewnetrzny uklad bufora (ten jest
 * grupa/offset/lane, patrz mcel_video.h), tylko widok dla 6502. */
constexpr int      STRIP_BYTES    = SCREEN_W * STRIP_H;        /* 7 680  */

/* DERIVED i STRIPMAP mieszkaja w BRAM, tak samo jak SPRLIST (D52). Blitter
 * nie zapisuje do SDRAM ANI JEDNEGO BAJTA — jest klientem tylko do odczytu.
 * Wglad dla 6502 daje okno mapowane (D53), a nie zrzut pamieci.
 *
 * [model] Koszt dostepu do rekordu DERIVED przez port BRAM. 64 B przez port
 * 32-bitowy = 16 taktow; model serializuje, wiec liczy sie wprost. Sprzet
 * moze to zrobic taniej szerszym portem — to ZALECENIE, nie kontrakt. */
constexpr int DER_BRAM_WR_CYCLES = 16;
constexpr int DER_BRAM_RD_CYCLES = 16;

/* [model] Odczyt deskryptora TEXTAB z BRAM (D58) — 16 B przez ten sam port
 * 32-bitowy, czyli 4 takty. Przedtem byl to burst SDRAM: 8 taktow danych plus
 * CL i ACTIVATE, zmierzone ~8,5 taktu na sprajta przy jednej teksturze
 * i ~17-20 przy rozrzuconych TEXID-ach. */
constexpr int TEX_BRAM_RD_CYCLES = 4;

/* ------------------------------------- okna 6502 i rejestry (D53, D54, D80) */
/* Urzadzenie ma TRZY niezalezne okna — $4000 i $8000 po 16 KB, $C000 po 4 KB —
 * i kazde z nich pokazuje albo blok SDRAM, albo dowolny blok BRAM blittera
 * (D54, D80). Wyboru dokonuje para rejestrow RAMMAP: b7 starszego bajta
 * wlacza BRAM, mlodszy bajt wybiera blok.
 *
 * Okno $C000 ma 4 KB, wiec widac przez nie PIERWSZE 4 KB bloku i nic wiecej.
 * TEXTAB ma rowno 4 096 B, czyli miesci sie tam CO DO BAJTA — i to jest ten
 * scenariusz, dla ktorego trzeci slot istnieje w modelu (D80). Blok wiekszy
 * niz 4 KB jest przez to okno widoczny tylko czesciowo; to nie jest blad,
 * tylko konsekwencja ziarna.
 *
 * POZA ZAKRESEM BLOKU SPRZET NIE OBIECUJE ZADNEJ WARTOSCI (D76). Odczyt
 * tamtejszych adresow jest bledem programu; model oddaje $FF wylacznie po to,
 * zeby slad byl deterministyczny, i mowi o tym ostrzezeniem.
 *
 * ZAPISYWALNE SA WEJSCIA PROLOGUE: SPRLIST i TEXTAB (D55 po korekcie z D58).
 * Reszta blokow to widok — DERIVED i STRIPMAP nalezy do prologue, pas do
 * rasteryzatora. */
enum : uint8_t {
    /* Kolejnosc nie jest przypadkowa: najpierw WEJSCIA prologue (oba
     * zapisywalne), potem jego WYJSCIA, na koncu widok diagnostyczny
     * rasteryzatora. */
    WIN_SPRLIST  = 0,   /*  5 120 B ważne — wejscie, zapisywalny    */
    WIN_TEXTAB   = 1,   /*  4 096 B ważne — wejscie, zapisywalny (D58) */
    WIN_DERIVED  = 2,   /* 16 384 B — wyjscie; wypelnia okno co do bajta */
    WIN_STRIPMAP = 3,   /*    512 B ważne — wyjscie                 */
    WIN_STRIP    = 4,   /*  7 680 B — ostatnio wyrenderowany pas (D56) */
    WIN_BLOCKS   = 5,   /* numer bloku ma odtad 3 bity, nie 2       */
};
/* WIN_SIZE to ziarno okien $4000/$8000 — i zarazem rozmiar bloku SDRAM,
 * na ktory dzieli sie 32 MB (2048 blokow). WIN_SIZE_4K to ziarno okna $C000,
 * gdzie tych blokow jest 8192. Rozmiar okna jest ODTAD PER SLOT (McelConfig
 * .win_size), bo sloty nie sa juz jednakowe. */
constexpr uint16_t WIN_SIZE    = 0x4000;   /* 16 KB — $4000, $8000 */
constexpr uint16_t WIN_SIZE_4K = 0x1000;   /*  4 KB — $C000 (D80) */

/* Trzy sloty okna. Baza w przestrzeni 6502 nalezy do dekodera URZADZENIA,
 * blitter tylko musi ja znac, zeby wiedziec, co znaczy kolejny dostep. */
constexpr int      WIN_SLOTS       = 3;
constexpr uint16_t DEF_WIN0_BASE   = 0x4000;   /* RAMMAP4000 */
constexpr uint16_t DEF_WIN1_BASE   = 0x8000;   /* RAMMAP8000 */
constexpr uint16_t DEF_WIN2_BASE   = 0xC000;   /* RAMMAPC000, 4 KB (D80) */

/* RAMMAP nalezy do MARII, nie do blittera — ale blitter go dekoduje, bo bez
 * tego nie wie, co znaczy dostep pod $4000 (D54). To JEDYNY cudzy rejestr,
 * ktorego musi pilnowac (D60). */
constexpr uint16_t MARIA_RAMMAP0_L = 0xD1A8;   /* RAMMAP4000L */
constexpr uint16_t MARIA_RAMMAP0_H = 0xD1A9;   /* RAMMAP4000H */
constexpr uint16_t MARIA_RAMMAP1_L = 0xD1AA;   /* RAMMAP8000L */
constexpr uint16_t MARIA_RAMMAP1_H = 0xD1AB;   /* RAMMAP8000H */
constexpr uint16_t MARIA_RAMMAP2_L = 0xD1AC;   /* RAMMAPC000L (D80) */
constexpr uint16_t MARIA_RAMMAP2_H = 0xD1AD;   /* RAMMAPC000H (D80) */
/* b7 starszego bajta: 1 = BRAM blittera, 0 = blok SDRAM urzadzenia. Numer
 * bloku SDRAM ma 11 bitow znaczacych przy ziarnie 16 KB i 13 przy 4 KB —
 * w OBU przypadkach b7 zostaje poza numerem, i to jest cale zalozenie. */
constexpr uint8_t  RAMMAP_BRAM     = 0x80;

/* ------------------------------------ paleta CEL przez PBIRAMBANK (D86) */
/* Paleta toru wideo jest CUDZA — nalezy do urzadzenia, nie do blittera — ale
 * blitter ja dekoduje z tego samego powodu, z ktorego dekoduje RAMMAP (D54):
 * bez niej obraz z modelu jest czarny, a program, ktory palety nie wpisal,
 * ma dostac DOKLADNIE to, co dostanie na sprzecie i w Altirze.
 *
 * PBIRAMBANK (MARIA_BASE + $4F) wybiera 256-bajtowa strone podstawiana pod
 * $DF00-$DFFF — region overlay PBI, widoczny tylko przy MARII wybranej przez
 * PDVREG (D74). Trzy banki niosa trzy skladowe palety CEL, po 256 x 8 bitow;
 * wpis `i` lezy pod $DF00 + i. Zapis i odczyt.
 *
 * Po wlaczeniu zasilania WSZYSTKIE skladowe sa zerami — cala paleta czarna,
 * ta sama regula co dla BRAM (D78). Indeks 0 zostaje tlem (D57) i jest
 * pokazywany jako kolor wpisu 0, czyli domyslnie czern.
 *
 * Innych bankow PBIRAMBANK ($00-$18 mapa kolorow i palety toru 7800,
 * $20-$2F charsety) blitter NIE modeluje: zapis przepada, odczyt oddaje $FF
 * z ostrzezeniem. Model nie modeluje tez selekcji PBI — strona $DFxx jest
 * w nim dekodowana zawsze; to ta sama klasa swiadomej roznicy co bramka
 * PDVREG dla RAMMAP (D74). */
constexpr uint16_t MARIA_PBIRAMBANK = 0xD14F;  /* (W) wybor strony pod $DF00 */
constexpr uint16_t PBIRAM_BASE      = 0xDF00;  /* strona widoczna po wyborze PBI */
constexpr uint16_t PBIRAM_SIZE      = 0x0100;
constexpr uint8_t  PBIRAM_CELPAL_R  = 0x19;    /* skladowa R palety CEL */
constexpr uint8_t  PBIRAM_CELPAL_G  = 0x1A;    /* skladowa G */
constexpr uint8_t  PBIRAM_CELPAL_B  = 0x1B;    /* skladowa B */
constexpr int      CELPAL_ENTRIES   = 256;
constexpr int      CELPAL_COMPONENTS = 3;

/* JEDEN rejestr, pod adresem bezposrednim (D60). Blitter widzi PLASKI indeks
 * i nic nie wie o tym, jak 6502 do niego dociera; mapowanie na $d1B2 nalezy do
 * dekodera urzadzenia (D18, D53, D59).
 *
 * Model MIMO TO odtwarza prawdziwy adres, i to jest swiadome odstepstwo od
 * "modelujemy tylko blitter": bez tego trace.bin pokazywalby adres, ktory
 * w sprzecie nie istnieje, a program testowy na Atari pisalby w prozne
 * miejsce — bez kolizji, wiec i bez objawu.
 *
 *   $d1B2                CTL (zapis) / STATUS (odczyt)     -> indeks 0
 *
 * I to jest cala mapa. Adresowania posredniego blitter nie uzywa wcale (D60). */
constexpr uint16_t DEF_REGBASE   = 0xD1B2;  /* [DO UZGODNIENIA] — D53, D59 */
constexpr uint16_t REGBASE_SIZE  = 1;       /* jedyny adres blittera */

/* Adresowanie posrednie urzadzenia — ADDR0..ADDR3 ($d100..$d103), selektor
 * banku ($d104) i numer banku $50 — jest dla blittera CUDZE (D60). Zadnej
 * stalej na to nie ma i nie ma jej celowo: blitter nie ma w tym mechanizmie
 * ani jednego rejestru, wiec nie ma tam czego nazywac. */

/* -------------------------------------------------------- rejestry (§2) */
/* JEDEN rejestr (D60):
 *
 *   0            CTL (zapis) / STATUS (odczyt)     -> wprost, $d1B2
 *
 * STATUS musi byc pod adresem BEZPOSREDNIM, bo pętla oczekiwania to
 * "bit REGBASE / bvs overrun / bpl poll" — dwie flagi za darmo w N i V.
 * Za adresowaniem posrednim ten idiom umiera, a z nim uzasadnienie
 * sticky OVERRUN (§2.5).
 *
 * Zadna tablica blittera nie ma rejestru bazowego, bo wszystkie leza w BRAM:
 * SPRLIST (D46), DERIVED ze STRIPMAP (D52) i TEXTAB (D58). Konfiguracja
 * blittera to JEDEN zapis pod JEDEN adres (D60). */
enum : uint8_t {
    REG_CTL           = 0,   /* zapis  */
    REG_STATUS        = 0,   /* odczyt */
    REG_COUNT         = 1,
};

/* CTL — zapis. Bity POZIOMOWE to ENABLE i NODETECT, i siedza w b1:0; strobe'y
 * to b4:2 plus RESET w b7. Strobe'y dzialaja na zboczu zapisu i kasuja sie same.
 *
 * UKLAD NIE JEST PRZYPADKOWY (D60): bity poziomowe leza w b1:0 DOKLADNIE tam,
 * gdzie STATUS je oddaje przy odczycie. Dzieki temu da sie je odczytac
 * i dolozyc strobe bez kopii w RAM-ie:
 *
 *      lda CELCTL
 *      and #CELENABLE|CELNODETECT      <- MASKA JEST OBOWIAZKOWA
 *      ora #CELSNAPCLR
 *      sta CELCTL
 *
 * Maski pominac NIE WOLNO i to jest ostrzejsza regula niz "nigdy RMW":
 * SNAP siedzi w b7 STATUS-u, a b7 CTL-a to RESET. Goly `ora`/`sta` zresetowalby
 * blitter w kazdej klatce, w ktorej SNAP = 1 — czyli prawie zawsze. */
enum : uint8_t {
    CTL_ENABLE   = 0x01,   /* poziom */
    CTL_NODETECT = 0x02,   /* poziom; D32 — bit diagnostyczny, nie optymalizacyjny */
    CTL_SNAP_CLR = 0x04,
    CTL_OVR_CLR  = 0x08,
    CTL_DBG_STEP = 0x10,   /* D56 — wyrenderuj JEDEN pas teraz; bez stanu trybu.
                            * Nazwa mowi, ze bit jest TYMCZASOWY: ma zniknac
                            * razem z powstaniem toru wideo. */
    CTL_RESET    = 0x80,
};

/* STATUS — odczyt. SNAP w b7, bo "bit + bpl" to najgorętszy odczyt sterownika.
 * BUSY swiadomie NIE siedzi w b6/b7, gdzie `bit` dalby flage za darmo: to
 * sciezka wylacznie diagnostyczna i tymczasowa (D56), a b6/b7 naleza do
 * OVERRUN i SNAP, ktore sa gorace naprawde.
 *
 * b1:0 to ODCZYT bitow poziomowych CTL, na TYCH SAMYCH pozycjach (D60). Nie sa
 * osobnym stanem — to jest to samo ENABLE i NODETECT, ogladane z drugiej
 * strony. Dlatego maski sa wspolne i STATUS_ENABLE == CTL_ENABLE; pilnuje tego
 * check_vars_s.py, bo caly zysk z tego ukladu znika, gdy pozycje sie rozjada.
 *
 * b4:2 czytaja sie jako ZERA — pod nimi leza strobe'y, ktore nie maja stanu. */
enum : uint8_t {
    STATUS_ENABLE   = CTL_ENABLE,     /* D60 — odczyt bitu poziomowego */
    STATUS_NODETECT = CTL_NODETECT,   /* D60 — j.w.                    */
    STATUS_BUSY     = 0x20,           /* D56 — pas w trakcie renderowania */
    STATUS_OVERRUN  = 0x40,
    STATUS_SNAP     = 0x80,
};

/* Przezroczysty jest indeks 0 W PALECIE EFEKTYWNEJ sprajta (D72): przy 8 bpp
 * bajt $00, przy 4 bpp nibbel $0, czyli po zlozeniu z paleta bajt {PAL, 0000}.
 * Ta sama regula obowiazuje kazdy sprajt i kazda teksture. Bufor pasa jest
 * zerowany zawsze, a tor obrazu czyta 0 jako "tu nic nie narysowano" (D57). */

/* ------------------------------- rekord sprajta: strona okna == offset w rekordzie */
/* 6502 pisze  okno + (strona << 8) + indeks   przy WIN_SPRLIST
 * blitter zna BRAM[(strona << 8) | indeks]                   — ten sam adres.
 *
 * SPRLIST jest STRONA-MAJOR (D46, D53): indeks
 * sprajta siedzi w X, a dwadziescia pol to dwadziescia "sta STRONA_n,x"
 * pod etykietami odleglymi o 256 B. Uklad rekord-major (i*20) wymagalby
 * mnozenia przez 20 na kazda aktualizacje pozycji — a to gorąca pętla,
 * 256 sprajtow na klatke. DERIVED i STRIPMAP sa rekord-major, bo maja
 * innego klienta: czyta sie je calymi rekordami.
 *
 * Adres jest ten sam po obu stronach — transpozycji nie ma (D46), bo BRAM ma
 * dostep swobodny i nie potrzebuje bursta:
 * 6502 i prologue adresuja to samo A[12:0]. */
enum : uint8_t {
    SPR_DRAW     = 0x00,   /* b7:4 PATX, b3:0 PAT (D40, D42) */
    SPR_TEXID    = 0x01,
    SPR_DSTX_LO  = 0x02,
    SPR_DSTX_HI  = 0x03,
    SPR_DSTY_LO  = 0x04,
    SPR_DSTY_HI  = 0x05,
    SPR_FLIP     = 0x06,   /* b7 HFLIP, b6 VFLIP (D41) */
    SPR_OPCODE   = 0x07,   /* b3:0 opcode, b7:4 PALXOR (D39) */
    /* opcode 1 — SCALE */
    SPR_DSTW_LO  = 0x08, SPR_DSTW_HI = 0x09,
    SPR_DSTH_LO  = 0x0A, SPR_DSTH_HI = 0x0B,
    /* opcode 2 — ROTSCALE (D82). Cztery pola 16-bitowe ida parami od $08,
     * a jedyne pole 8-bitowe — ANGLE — na koniec, zeby nie rozbijac zadnej
     * pary. Bajtu trybu nie ma: poczatek pivota to ZAWSZE srodek tekstury. */
    SPR_SCALEX_LO= 0x08, SPR_SCALEX_HI = 0x09,
    SPR_SCALEY_LO= 0x0A, SPR_SCALEY_HI = 0x0B,
    SPR_PIVX_LO  = 0x0C, SPR_PIVX_HI = 0x0D,
    SPR_PIVY_LO  = 0x0E, SPR_PIVY_HI = 0x0F,
    SPR_ANGLE    = 0x10,
    /* opcode 3 — AFFINE (int16 12.4) */
    SPR_EUX_LO   = 0x08, SPR_EUX_HI  = 0x09,
    SPR_EUY_LO   = 0x0A, SPR_EUY_HI  = 0x0B,
    SPR_EVX_LO   = 0x0C, SPR_EVX_HI  = 0x0D,
    SPR_EVY_LO   = 0x0E, SPR_EVY_HI  = 0x0F,
    /* opcode 4 — QUAD: czwarty rog jako wektor P0 -> P2, w tym samym formacie
     * 12.4 co EU i EV (D45). HDD blitter liczy sam: HDD = EW - EU - EV. */
    SPR_EWX_LO   = 0x10, SPR_EWX_HI  = 0x11,
    SPR_EWY_LO   = 0x12, SPR_EWY_HI  = 0x13,
    /* opcode 0 i 1 — HIT-BOX akceleratora kolizji (D85). Strony $0C-$13 sa
     * przy BLIT i SCALE wolne (BLIT czyta 8 stron, SCALE 12), i tylko dlatego
     * kolizje istnieja wylacznie dla tych dwoch opcodow: przy ROTSCALE leza
     * tam PIVX/PIVY, przy AFFINE EV, przy QUAD EV i EW.
     *
     * To sa PRZESUNIECIA ROGOW wlasnego prostokata sprajta, int16 12.4
     * w TEKSELACH, ze znakiem — nie wspolrzedne. Zera znacza hit-box rowny
     * sprajtowi, czyli "bez zadnych translacji" (D78, D85).
     *
     * PROLOGUE ICH NIE CZYTA. Kolizje licza sie NA ZADANIE, przy zapisie
     * COLB, wprost z SPRLIST — dlatego spr_pages_for_opcode() zostaje bez
     * zmiany, a rekord DERIVED nie rosnie ani o bajt. */
    SPR_HBX0_LO  = 0x0C, SPR_HBX0_HI = 0x0D,   /* lewy gorny rog: dx */
    SPR_HBY0_LO  = 0x0E, SPR_HBY0_HI = 0x0F,   /*                 dy */
    SPR_HBX1_LO  = 0x10, SPR_HBX1_HI = 0x11,   /* prawy dolny rog: dx */
    SPR_HBY1_LO  = 0x12, SPR_HBY1_HI = 0x13,   /*                  dy */
};

/* ------------------------- strona $14 okna SPRLIST: viewport i kolizje --- */
/* Siedem bajtow tuz za rekordami sprajtow (D84, D85). Rekordy koncza sie na
 * stronie $13, wiec strona $14 byla dotad POZA MAPA (D76) — te siedem bajtow
 * wyjmuje sie z niej i definiuje; reszta strony $14 i strony $15-$1F zostaja
 * poza mapa jak dotad.
 *
 * DLACZEGO TUTAJ, A NIE POD REGBASE: blitter ma JEDEN rejestr pod adresem
 * bezposrednim i to jest decyzja D60, ktorej nie ruszamy. Umieszczenie tych
 * pol w oknie SPRLIST daje trzy rzeczy za darmo:
 *   - dekoder adresowy URZADZENIA sie nie zmienia (D18) — to nasz modul,
 *   - zapis 16-bitowy nie ma problemu atomowosci wzgledem prologue, bo
 *     obowiazuje ten sam kontrakt co dla listy: pisz przy SNAP = 1,
 *   - gra i tak trzyma okno wpiete na SPRLIST przez cala klatke (D53). */
enum : uint16_t {
    SPRWIN_REGS_PAGE = 0x14,        /* numer strony okna */
    SPRWIN_REGS      = 0x1400,      /* offset w oknie = SPRWIN_REGS_PAGE << 8 */

    /* Viewport (D84) — pozycja piksela widocznego w LEWYM GORNYM ROGU ekranu,
     * int16 12.4 ze znakiem, w przestrzeni renderingu. Zeby punkt (0,0) tej
     * przestrzeni wypadl na srodku ekranu: VPX = -160*16, VPY = -120*16. */
    SPRWIN_VPX_LO    = 0x1400, SPRWIN_VPX_HI = 0x1401,
    SPRWIN_VPY_LO    = 0x1402, SPRWIN_VPY_HI = 0x1403,

    /* Akcelerator kolizji (D85). COLA i COLBMAX sa ZATRZASKAMI; skan startuje
     * WYLACZNIE zapis COLB — inaczej zapis COLA ruszalby przemiat po
     * nieaktualnym B. Kolejnosc jest przez to ustalona: COLA i COLBMAX przed,
     * COLB na koncu i tylko on w petli. */
    SPRWIN_COLA      = 0x1404,      /* (W) indeks A   (R) status */
    SPRWIN_COLBMAX   = 0x1406,      /* (W)/(R) gorna granica zakresu, WLACZNIE */
    SPRWIN_COLB      = 0x1405,      /* (W) indeks startowy — STARTUJE SKAN
                                     * (R) indeks biezacy / trafiony */
    SPRWIN_REGS_END  = 0x1407,      /* pierwszy offset juz POZA MAPA */
};

/* Status kolizji — odczyt spod SPRWIN_COLA.
 *
 * Uklad wziety wprost ze STATUS (D59): NOTREADY w b7 i FOUND w b6, zeby
 * "bit COLA" dalo obie flagi jednym 4-cyklowym rozkazem, nie ruszajac
 * akumulatora:
 *
 *      sta  CELCOLB
 *   p: bit  CELCOLA
 *      bmi  p              ; N = NOTREADY
 *      bvs  trafienie      ; V = FOUND
 *
 * NOBOX lezy przy b7 = 0 CELOWO. Gdyby zly opcode zglaszal sie jako NOTREADY,
 * petla "bmi p" wisialaby w nieskonczonosc na sprajcie obroconym; tak jest to
 * odpowiedz POPRAWNA ("nie koliduje"), a przyczyna widoczna. */
enum : uint8_t {
    COL_NOBOX    = 0x20,   /* b5 — A nie ma hit-boxa: opcode /= BLIT,SCALE */
    COL_FOUND    = 0x40,   /* b6 — trafienie; wtedy COLB niesie indeks */
    COL_NOTREADY = 0x80,   /* b7 — odpowiedzi jeszcze nie ma, powtorz odczyt */
};

/* [model] Ile taktow blittera kosztuje jeden kandydat skanu: odczyt stron
 * OPCODE, TEXID, DSTX/DSTY i osmiu bajtow hit-boxa plus deskryptor. Sluzy
 * WYLACZNIE do ostrzezenia: model liczy kolizje natychmiast, wiec NOTREADY
 * nigdy nie jest w nim widziane jako 1 — tak samo jak BUSY (D56). */
constexpr int COL_CAND_CYCLES  = 25;
/* [model] Ile taktow blittera mija miedzy "sta COLB" a najwczesniejszym
 * odczytem: 4 cykle 6502 przy zegarze rdzenia 135 MHz i 6502 ~1,79 MHz.
 * Zegar 6502 nalezy do URZADZENIA, wiec to jest ZALOZENIE, nie kontrakt. */
constexpr int COL_READY_BUDGET = 300;

enum : uint8_t { FLIP_H = 0x80, FLIP_V = 0x40 };

enum : uint8_t {
    OPC_BLIT = 0, OPC_SCALE = 1, OPC_ROTSCALE = 2, OPC_AFFINE = 3, OPC_QUAD = 4,
};

/* Pola trybu pivota NIE MA (D82). Przy OPCODE = ROTSCALE DSTX/DSTY to pozycja
 * pivota na ekranie, a pivot to zawsze
 *
 *     SRODEK TEKSTURY + (PIVX, PIVY)
 *
 * Srodek w 12.4 to W << 3, wiec przy nieparzystym W wypada DOKLADNIE w polowie
 * teksela. Rekord, ktorego nikt nie tknal, obraca sprajta wokol jego srodka —
 * ta sama regula co DRAW = 0 (D40) i wyzerowana BRAM (D78).
 *
 * Pivot w rogu nie jest juz darmowy: trzeba podac (-W/2, -H/2), czyli
 * PIVX = -(W << 3). To swiadoma cena za jedno pole mniej. */

/* Ile stron rekordu prologue MUSI przeczytac dla danego opcodu.
 *
 * Zawsze zaczyna od osmiu (pola wspolne, w tym OPCODE i DRAW), decyduje
 * i dociaga reszte. Przy SPRLIST w SDRAM byla to opcjonalna optymalizacja
 * (03-interface-spec §4, "czytac 16 B i dociagac reszte"); przy SPRLIST
 * w BRAM jest naturalna i darmowa, bo dostep jest swobodny. */
constexpr int spr_pages_for_opcode(uint8_t op) {
    return op == OPC_BLIT     ?  8 :
           op == OPC_SCALE    ? 12 :
           op == OPC_ROTSCALE ? 17 :
           op == OPC_AFFINE   ? 16 :
           op == OPC_QUAD     ? 20 : 8;
}

/* ------------------------------------ deskryptor tekstury (10 §3.2, 16 B) */
/* TEXTAB mieszka w BRAM blittera (D58), tak samo jak SPRLIST, DERIVED
 * i STRIPMAP. Adres deskryptora to (TEXID << 4) | offset — ten sam po stronie
 * 6502 (okno WIN_TEXTAB) i po stronie prologue. Bazy nie ma, bo nie ma czego
 * bazowac: blok jest jeden i zaczyna sie od zera. */
enum : uint8_t {
    TEX_FORMAT    = 0,
    TEX_BASE_0    = 1,   /* A[8:1]   — BASE to A[24:1], adres PARZYSTY (D37) */
    TEX_BASE_1    = 2,   /* A[16:9]  */
    TEX_BASE_2    = 3,   /* A[24:17] */
    TEX_STRIDE_LO = 4, TEX_STRIDE_HI = 5,   /* w BAJTACH */
    TEX_WIDTH_LO  = 6, TEX_WIDTH_HI  = 7,   /* szerokość − 1 */
    TEX_HEIGHT_LO = 8, TEX_HEIGHT_HI = 9,   /* wysokość − 1  */
    TEX_RSV_10    = 10,  /* rezerwa (D72) */
    TEX_PALOFS    = 11,  /* b3:0; b7:4 ignorowane */
};

/* FORMAT — układ bitów (D44).
 *
 *   b0     BPP    0 = 8 bpp, 1 = 4 bpp        (jeden bit; 2 bpp zamknięte, D36)
 *   b2:1   RLEW   00 = bez RLE, 01/10/11 = wpis tablicy offsetów na 1/2/3 B
 *   b3     rezerwa (D72)
 *   b4     NIBOFS rezerwa (D37)
 *   b7:5   rezerwa
 *
 * RLEW niesie jednocześnie „czy RLE" i szerokość wpisu, bo tablica offsetów
 * ma H wpisów niezależnie od kompresji i przy kaflu 16×16 potrafi być 37%
 * całości przy wpisach 4-bajtowych. BPP ma przez to jeden bit, a 2 bpp jest
 * zamknięte nieodwracalnie (D36, D44). */
enum : uint8_t {
    FMT_BPP_MASK  = 0x01,
    FMT_BPP_8     = 0x00,
    FMT_BPP_4     = 0x01,
    FMT_RLEW_MASK = 0x06,   /* b2:1 */
    FMT_RLEW_SHIFT = 1,
    FMT_RLEW_NONE = 0x00,   /* bez RLE */
    FMT_RSV_B3    = 0x08,   /* rezerwa, D72 */
    FMT_NIBOFS    = 0x10,   /* rezerwa, D37 */
};

/* Szerokość wpisu tablicy offsetów w bajtach; 0 znaczy „tekstura nie jest RLE". */
constexpr int rlew_bytes(uint8_t format) {
    return (format & FMT_RLEW_MASK) >> FMT_RLEW_SHIFT;
}

/* -------------------------------------------- rekord DERIVED (§1, 64 B, SDRAM) */
/* UWAGA (D37, pułapka 11): SRC_STRIDE/SRC_W/SRC_H zaczynają się pod NIEPARZYSTYMI
 * offsetami. Nakładanie na ten rekord struktury C++ z polami 16-bitowymi daje
 * ciche śmieci — czytać i pisać bajt po bajcie (mcel_derived.cpp). */
enum : int {
    DER_XPOS = 0, DER_YPOS = 4, DER_HDX = 8, DER_HDY = 12, DER_VDX = 16, DER_VDY = 20,
    DER_IA = 24, DER_IB = 28, DER_IC = 32, DER_ID = 36,
    DER_RCP_HDY = 40, DER_RCP_VDY = 44, DER_RCP_HDX = 48,
    DER_SRC_BASE = 52,      /* uint24, A[24:1] */
    DER_SRC_STRIDE = 55,    /* uint16 — offset nieparzysty */
    DER_SRC_W = 57, DER_SRC_H = 59,
    DER_RSV_61 = 61,        /* rezerwa, pisana ZEREM (D72) */
    DER_FORMAT = 62,        /* { PAL[3:0], rezerwa, RLEW[1:0], BPP } — PAL już po XOR */
    DER_MODE = 63,
};

/* MODE — wykryta ścieżka rasteryzacji (DERIVED+63, b2:0).
 *
 * MODE_INVISIBLE jest NORMATYWNY, mimo że nie odpowiada żadnej ścieżce:
 * rekord DERIVED jest widoczny w oknie 6502 i porównywany bajt w bajt (D53),
 * więc wartość zapisywana dla sprajta odrzuconego (det == 0, nieznany opcode)
 * jest częścią kontraktu, a nie szczegółem implementacji. */
enum : uint8_t {
    MODE_LINE = 0, MODE_SCALE = 1, MODE_AFFINE = 2, MODE_QUAD = 4,
    MODE_INVISIBLE = 7,
};

/* STRIPMAP — 2 B na sprajta, rezydentne w BRAM (02-architecture §6) */
constexpr uint8_t STRIPMAP_INVISIBLE = 0x0F;   /* (s_max=0 << 4) | s_min=15 */

/* strip_of(y) = y / 24. STRIP_H nie jest potęgą dwójki — w RTL to y·683 >> 14
 * (dokładne dla y < 240) albo koder priorytetowy. Model liczy oba i sprawdza
 * zgodność w self-teście, żeby sztuczka nie została nigdzie zgadnięta. */
constexpr int strip_of(int y)     { return y / STRIP_H; }
constexpr int strip_of_rtl(int y) { return (y * 683) >> 14; }

} /* namespace mcel */

#endif /* MCEL_REGS_H */
