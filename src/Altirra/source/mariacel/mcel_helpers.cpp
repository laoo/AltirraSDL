#include "mcel_helpers.h"

namespace mcel {
namespace {

/* SPRLIST musi byc WPIETE w okno, zanim cokolwiek do niego napiszemy — po
 * resecie zadne okno nie jest wpiete (D54). Sprawdzamy i wpinamy dopiero gdy
 * trzeba, wiec scena wypuszcza dwa zapisy RAMMAP raz, a nie przy kazdym
 * bajcie: dokladnie to, co robi program na Atari ("okno $4000 trzyma SPRLIST
 * przez cala klatke — zero przelaczen w goracej petli"). */
inline void ensure_sprwin(mcel_t *m) {
    if (m->bram_block_at(m->config().win_base[0]) != WIN_SPRLIST)
        win_map(m, 0, WIN_SPRLIST);
}

/* Jedyny sposób, w jaki ta warstwa dotyka sprajta. */
inline void spw(mcel_t *m, uint8_t page, uint8_t idx, uint8_t v) {
    ensure_sprwin(m);
    m->wr(uint16_t(m->config().win_base[0] + (uint16_t(page) << 8) + idx), v);
}

/* TEXTAB idzie SLOTEM 1, zeby nie wypychac SPRLIST-u ze slotu 0. Oba okna moga
 * pokazywac BRAM naraz (D54), wiec deskryptory i lista sa dostepne rownoczesnie
 * — i tak wlasnie wyglada loader na Atari (D58). */
inline void ensure_texwin(mcel_t *m) {
    if (m->bram_block_at(m->config().win_base[1]) != WIN_TEXTAB)
        win_map(m, 1, WIN_TEXTAB);
}

/* Adres deskryptora w oknie to (TEXID << 4) | offset — czyste ciecie bitow. */
inline void texw(mcel_t *m, uint8_t id, const uint8_t rec[TEX_REC_SZ]) {
    ensure_texwin(m);
    const uint16_t base = uint16_t(m->config().win_base[1] + (uint16_t(id) << 4));
    for (int k = 0; k < TEX_REC_SZ; ++k) m->wr(uint16_t(base + k), rec[k]);
}

} /* anon */

/* Wpiecie bloku BRAM w okno — dwa zapisy do RAMMAP, prawdziwa sciezka 6502.
 * Starszy bajt z b7 przelacza okno z SDRAM na BRAM, mlodszy wybiera blok. */
void win_map(mcel_t *m, int slot, uint8_t block) {
    m->wr(m->config().rammap[slot][0], block);
    m->wr(m->config().rammap[slot][1], RAMMAP_BRAM);
}

/* Odpiecie: okno wraca na stock RAM Atari (RAMMAP = 0). */
void win_unmap(mcel_t *m, int slot) {
    m->wr(m->config().rammap[slot][0], 0);
    m->wr(m->config().rammap[slot][1], 0);
}

/* Zapis PRAWDZIWA sciezka 6502 (D53), nie skrotem — jeden `sta` pod adres
 * bezposredni i nic wiecej (D60). Adresowania posredniego nie dotykamy, bo blitter
 * nie ma tam ani jednego rejestru; razem z bankiem znikla regula
 * "konfiguracje blittera pisac przy SEI". */
void ctl_wr(mcel_t *m, uint8_t v)   { m->wr(m->config().regbase, v); }
uint8_t status_rd(mcel_t *m)        { return m->rd(m->config().regbase); }

/* Bity POZIOMOWE CTL, odczytane TA SAMA DROGA, ktora ma 6502: przez STATUS.
 * b1:0 STATUS-u to te same pozycje co w CTL (D60), wiec caly odczyt to jedno
 * `lda` i jedno `and` — kopii w RAM-ie nie trzeba.
 *
 * MASKA JEST OBOWIAZKOWA i to jest ostrzejsza regula niz "nigdy RMW":
 * SNAP siedzi w b7 STATUS-u, a b7 CTL-a to RESET. Goly odczyt doklejony do
 * stroba zresetowalby blitter w kazdej klatce, w ktorej SNAP = 1. */
uint8_t ctl_levels(mcel_t *m) {
    return uint8_t(status_rd(m) & (CTL_ENABLE | CTL_NODETECT));
}

void set_enable(mcel_t *m, bool on) {
    ctl_wr(m, uint8_t((ctl_levels(m) & ~CTL_ENABLE) | (on ? CTL_ENABLE : 0)));
}
void set_nodetect(mcel_t *m, bool on) {
    ctl_wr(m, uint8_t((ctl_levels(m) & ~CTL_NODETECT) | (on ? CTL_NODETECT : 0)));
}

void clear_overrun(mcel_t *m) { ctl_wr(m, uint8_t(ctl_levels(m) | CTL_OVR_CLR)); }
void device_reset(mcel_t *m)  { ctl_wr(m, CTL_RESET); }

/* PALXOR siedzi w GÓRNYM NIBBLU OPCODE (D39). Opcode jest stałą sprajta znaną
 * w czasie asemblacji, więc na 6502 to jeden literał i jedno `sta`:
 *      lda #(PAL_RED << 4) | OPC_BLIT
 * Reguła: pole ciepłe wolno dokleić do zimnej stałej, nigdy do drugiego ciepłego. */
void spr_opcode(mcel_t *m, uint8_t i, uint8_t op, uint8_t palxor) {
    spw(m, SPR_OPCODE, i, uint8_t(((palxor & 0x0F) << 4) | (op & 0x0F)));
}
void spr_texid(mcel_t *m, uint8_t i, uint8_t t) { spw(m, SPR_TEXID, i, t); }

void spr_pos(mcel_t *m, uint8_t i, int16_t x, int16_t y) {
    spw(m, SPR_DSTX_LO, i, uint8_t(uint16_t(x)));
    spw(m, SPR_DSTX_HI, i, uint8_t(uint16_t(x) >> 8));
    spw(m, SPR_DSTY_LO, i, uint8_t(uint16_t(y)));
    spw(m, SPR_DSTY_HI, i, uint8_t(uint16_t(y) >> 8));
}
void spr_flip(mcel_t *m, uint8_t i, bool h, bool v) {
    spw(m, SPR_FLIP, i, uint8_t((h ? FLIP_H : 0) | (v ? FLIP_V : 0)));   /* D41 */
}
void spr_draw(mcel_t *m, uint8_t i, uint8_t draw) { spw(m, SPR_DRAW, i, draw); }

/* D40 odwróciło polaryzację: zera znaczą "nie rysuj". Zostawione dla zgodności
 * z listą helperów z model/README.md, rozdz. API. */
void spr_skip(mcel_t *m, uint8_t i, bool skip) { spr_draw(m, i, skip ? 0x00 : 0x0F); }

void spr_scale(mcel_t *m, uint8_t i, uint16_t w, uint16_t h) {
    spw(m, SPR_DSTW_LO, i, uint8_t(w));  spw(m, SPR_DSTW_HI, i, uint8_t(w >> 8));
    spw(m, SPR_DSTH_LO, i, uint8_t(h));  spw(m, SPR_DSTH_HI, i, uint8_t(h >> 8));
}
/* pivx/pivy to PRZESUNIECIE OD SRODKA tekstury, int16 12.4 (D82). */
void spr_rotscale(mcel_t *m, uint8_t i, uint8_t ang,
                  uint16_t sx, uint16_t sy, int16_t pivx, int16_t pivy) {
    spw(m, SPR_ANGLE, i, ang);
    spw(m, SPR_SCALEX_LO, i, uint8_t(sx)); spw(m, SPR_SCALEX_HI, i, uint8_t(sx >> 8));
    spw(m, SPR_SCALEY_LO, i, uint8_t(sy)); spw(m, SPR_SCALEY_HI, i, uint8_t(sy >> 8));
    spw(m, SPR_PIVX_LO, i, uint8_t(uint16_t(pivx)));
    spw(m, SPR_PIVX_HI, i, uint8_t(uint16_t(pivx) >> 8));
    spw(m, SPR_PIVY_LO, i, uint8_t(uint16_t(pivy)));
    spw(m, SPR_PIVY_HI, i, uint8_t(uint16_t(pivy) >> 8));
}
void spr_affine(mcel_t *m, uint8_t i, int16_t eux, int16_t euy,
                int16_t evx, int16_t evy) {
    spw(m, SPR_EUX_LO, i, uint8_t(uint16_t(eux))); spw(m, SPR_EUX_HI, i, uint8_t(uint16_t(eux) >> 8));
    spw(m, SPR_EUY_LO, i, uint8_t(uint16_t(euy))); spw(m, SPR_EUY_HI, i, uint8_t(uint16_t(euy) >> 8));
    spw(m, SPR_EVX_LO, i, uint8_t(uint16_t(evx))); spw(m, SPR_EVX_HI, i, uint8_t(uint16_t(evx) >> 8));
    spw(m, SPR_EVY_LO, i, uint8_t(uint16_t(evy))); spw(m, SPR_EVY_HI, i, uint8_t(uint16_t(evy) >> 8));
}

void spr_quad(mcel_t *m, uint8_t i, int16_t ewx, int16_t ewy) {
    spw(m, SPR_EWX_LO, i, uint8_t(uint16_t(ewx)));  spw(m, SPR_EWX_HI, i, uint8_t(uint16_t(ewx) >> 8));
    spw(m, SPR_EWY_LO, i, uint8_t(uint16_t(ewy)));  spw(m, SPR_EWY_HI, i, uint8_t(uint16_t(ewy) >> 8));
}

/* Hit-box (D85) — strony $0C-$13, wolne przy BLIT i SCALE. Przesuniecia
 * wlasnych rogow sprajta, nie wspolrzedne: zera daja prostokat rowny
 * sprajtowi. */
void spr_hitbox(mcel_t *m, uint8_t i, int16_t dx0, int16_t dy0,
                int16_t dx1, int16_t dy1) {
    spw(m, SPR_HBX0_LO, i, uint8_t(uint16_t(dx0))); spw(m, SPR_HBX0_HI, i, uint8_t(uint16_t(dx0) >> 8));
    spw(m, SPR_HBY0_LO, i, uint8_t(uint16_t(dy0))); spw(m, SPR_HBY0_HI, i, uint8_t(uint16_t(dy0) >> 8));
    spw(m, SPR_HBX1_LO, i, uint8_t(uint16_t(dx1))); spw(m, SPR_HBX1_HI, i, uint8_t(uint16_t(dx1) >> 8));
    spw(m, SPR_HBY1_LO, i, uint8_t(uint16_t(dy1))); spw(m, SPR_HBY1_HI, i, uint8_t(uint16_t(dy1) >> 8));
}

/* --- strona $14 okna SPRLIST: viewport i kolizje ------------------------- */
/* Ta sama droga co pola sprajta — okno SPRLIST — tylko strona jest stala,
 * a offsetu w stronie nie indeksuje zaden sprajt. */
namespace {
inline void sprwin_wr8(mcel_t *m, uint16_t off, uint8_t v) {
    if (m->bram_block_at(m->config().win_base[0]) != WIN_SPRLIST)
        win_map(m, 0, WIN_SPRLIST);
    m->wr(uint16_t(m->config().win_base[0] + off), v);
}
inline uint8_t sprwin_rd8(mcel_t *m, uint16_t off) {
    if (m->bram_block_at(m->config().win_base[0]) != WIN_SPRLIST)
        win_map(m, 0, WIN_SPRLIST);
    return m->rd(uint16_t(m->config().win_base[0] + off));
}
} /* anon */

void set_viewport(mcel_t *m, int16_t vpx, int16_t vpy) {
    sprwin_wr8(m, SPRWIN_VPX_LO, uint8_t(uint16_t(vpx)));
    sprwin_wr8(m, SPRWIN_VPX_HI, uint8_t(uint16_t(vpx) >> 8));
    sprwin_wr8(m, SPRWIN_VPY_LO, uint8_t(uint16_t(vpy)));
    sprwin_wr8(m, SPRWIN_VPY_HI, uint8_t(uint16_t(vpy) >> 8));
}

/* Kolejnosc jest CZESCIA KONTRAKTU (D85): COLA i COLBMAX sa zatrzaskami,
 * a skan startuje wylacznie zapis COLB — wiec on idzie ostatni. */
uint8_t collide(mcel_t *m, uint8_t a, uint8_t b, uint8_t bmax) {
    sprwin_wr8(m, SPRWIN_COLA,    a);
    sprwin_wr8(m, SPRWIN_COLBMAX, bmax);
    sprwin_wr8(m, SPRWIN_COLB,    b);
    return sprwin_rd8(m, SPRWIN_COLA);
}
uint8_t collide_index(mcel_t *m) { return sprwin_rd8(m, SPRWIN_COLB); }

void tex_write(mcel_t *m, uint8_t id, const TexDesc &t) {
    uint8_t r[TEX_REC_SZ] = { 0 };
    r[TEX_FORMAT] = uint8_t((t.bpp_code & FMT_BPP_MASK) |
                            ((uint8_t(t.rle_width) << FMT_RLEW_SHIFT) & FMT_RLEW_MASK));
    /* BASE to A[24:1] — adres PARZYSTY (D37). Adres bajtowy = BASE << 1. */
    const uint32_t base = t.base_byte >> 1;
    r[TEX_BASE_0] = uint8_t(base);
    r[TEX_BASE_1] = uint8_t(base >> 8);
    r[TEX_BASE_2] = uint8_t(base >> 16);
    r[TEX_STRIDE_LO] = uint8_t(t.stride);  r[TEX_STRIDE_HI] = uint8_t(t.stride >> 8);
    const uint16_t w1 = uint16_t(t.width  - 1), h1 = uint16_t(t.height - 1);
    r[TEX_WIDTH_LO]  = uint8_t(w1);        r[TEX_WIDTH_HI]  = uint8_t(w1 >> 8);
    r[TEX_HEIGHT_LO] = uint8_t(h1);        r[TEX_HEIGHT_HI] = uint8_t(h1 >> 8);
    r[TEX_RSV_10] = 0;          /* rezerwa — jawnie zero (D72) */
    r[TEX_PALOFS] = uint8_t(t.palofs & 0x0F);
    /* TEXTAB leży w BRAM (D58) — jedynym pisarzem jest 6502 przez okno. */
    texw(m, id, r);
}

void tex_write_raw(mcel_t *m, uint8_t id, const uint8_t rec[TEX_REC_SZ]) {
    texw(m, id, rec);   /* ta sama droga, co tex_write */
}

void mcel_commit(mcel_t *m, int max_vblanks) {
    /* ENABLE dokladamy jawnie (commit ma ruszyc blitter), NODETECT przenosimy
     * z kopii — inaczej ten commit zgasilby tryb diagnostyczny (D60). */
    ctl_wr(m, uint8_t(ctl_levels(m) | CTL_ENABLE | CTL_SNAP_CLR));
    for (int k = 0; k < max_vblanks && !m->snap(); ++k) m->vblank();
}

void render_frame(mcel_t *m, uint8_t *out) {
    m->vblank();
    for (unsigned s = 0; s < (unsigned)STRIPS; ++s) {
        m->strip(s);
        m->scanout(s, out ? out + size_t(s) * STRIP_H * SCREEN_W : nullptr);
    }
    m->frame_done();
}

} /* namespace mcel */
