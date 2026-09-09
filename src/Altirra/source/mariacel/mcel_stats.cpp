/* mcel_stats.cpp — artefakty przebiegu: stats.json i trace.bin (E0.5).
 *
 * Test w RTL = odtworzenie trace.bin + porównanie z out.raw i stats.json.
 *
 * Format trace.bin (little endian, bez wyrównania):
 *
 *   nagłówek: "MCELTRC1"                                        8 B
 *   rekordy:  u8 typ, potem ładunek
 *       0x01 WR         u16 addr, u8 val        zapis 6502
 *       0x02 RD         u16 addr, u8 val        odczyt 6502 (val = co oddał model)
 *       0x03 UPLOAD     u32 addr, u32 len, dane ścieżka wgrywania URZĄDZENIA
 *       0x04 VBLANK     —
 *       0x05 STRIP      u8 s
 *       0x06 SCANOUT    u8 s
 *       0x07 FRAME_DONE —
 *       0x08 PALETTE    768 B: R,G,B wpisu 0, R,G,B wpisu 1, ... (D86)
 *                       ścieżka URZĄDZENIA — paleta CEL ze sceny YAML
 *
 * Typy 0x04–0x07 to znaczniki napędu czasu: testbench ma po nich wiedzieć,
 * w którym momencie klatki wykonać porównanie. Program 6502 palety tędy nie
 * wgrywa — idzie przez PBIRAMBANK i stronę $DFxx, czyli zwykłymi WR.
 */
#include "mcel.h"

#include <cstdio>
#include <string>

namespace mcel {

bool Mcel::trace_write(const std::string &path) const {
    if (trace_.empty()) return false;
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const size_t n = std::fwrite(trace_.data(), 1, trace_.size(), f);
    std::fclose(f);
    return n == trace_.size();
}

namespace {

std::string esc(const std::string &s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o;
}

} /* anon */

std::string stats_json(const Mcel &m, const std::vector<uint32_t> &write_hist) {
    const ModelStats &st = m.stats();
    const SdramStats &sd = m.sdram().stats();
    const SpanStats  &sp = m.writer().stats();
    char buf[512];
    std::string j = "{\n";

    j += "  \"prologue\": {";
    std::snprintf(buf, sizeof buf,
        "\"cycles\": %lld, \"window_cycles\": %lld, \"sprites\": %d, "
        "\"invisible\": %d, \"degenerate\": %d, \"unsupported_opcode\": %d",
        (long long)st.prologue_cycles, (long long)m.config().vblank_window_cycles,
        st.prologue_sprites, st.invisible, st.degenerate, st.unsupported_opcode);
    j += buf; j += "},\n";

    std::snprintf(buf, sizeof buf,
        "  \"inverse_residual\": {\"min\": %d, \"max\": %d, \"exact_sprites\": %d, "
        "\"unit\": \"2^-FRAC_BITS teksela; <0 = nachodzenie, >0 = szczelina\"},\n",
        st.inv_residual_min, st.inv_residual_max, st.inv_exact_sprites);
    j += buf;

    std::snprintf(buf, sizeof buf,
        "  \"mode_count\": {\"line\": %d, \"scale\": %d, \"affine\": %d, \"quad\": %d},\n",
        st.mode_count[MODE_LINE], st.mode_count[MODE_SCALE],
        st.mode_count[MODE_AFFINE], st.mode_count[MODE_QUAD]);
    j += buf;

    j += "  \"strips\": [\n";
    for (int s = 0; s < STRIPS; ++s) {
        const StripStat &x = st.strip[s];
        const double util = x.cycles_window ? 100.0 * double(x.cycles) / double(x.cycles_window) : 0.0;
        std::snprintf(buf, sizeof buf,
            "    {\"s\": %d, \"cycles\": %lld, \"window\": %lld, \"util_pct\": %.2f, "
            "\"scanned\": %d, \"hit\": %d, \"culled_pat\": %d, \"drawn\": %d, "
            "\"overrun\": %s}%s\n",
            s, (long long)x.cycles, (long long)x.cycles_window, util,
            x.sprites_scanned, x.sprites_hit, x.sprites_culled_pat, x.sprites_drawn,
            x.overrun ? "true" : "false", s + 1 < STRIPS ? "," : "");
        j += buf;
    }
    j += "  ],\n";

    j += "  \"sdram\": {";
    std::snprintf(buf, sizeof buf,
        "\"activates\": %llu, \"activates_hidden\": %llu, \"row_hits\": %llu, "
        "\"precharges\": %llu, \"refreshes\": %llu, \"turnarounds\": %llu, "
        "\"bursts_rd\": %llu, \"bursts_wr\": %llu, \"bytes_rd\": %llu, "
        "\"bytes_wr\": %llu, \"cycles_data\": %llu, \"cycles_overhead\": %llu, "
        "\"cycles_refresh\": %llu, \"cycles_cpu6502\": %llu",
        (unsigned long long)sd.activates, (unsigned long long)sd.activates_hidden,
        (unsigned long long)sd.row_hits, (unsigned long long)sd.precharges,
        (unsigned long long)sd.refreshes, (unsigned long long)sd.turnarounds,
        (unsigned long long)sd.bursts_rd, (unsigned long long)sd.bursts_wr,
        (unsigned long long)sd.bytes_rd, (unsigned long long)sd.bytes_wr,
        (unsigned long long)sd.cycles_data, (unsigned long long)sd.cycles_overhead,
        (unsigned long long)sd.cycles_refresh, (unsigned long long)sd.cycles_cpu6502);
    j += buf; j += "},\n";

    j += "  \"spans\": {";
    std::snprintf(buf, sizeof buf,
        "\"spans\": %llu, \"pixels_covered\": %llu, \"pixels_rle_skip\": %llu, "
        "\"pixels_transp\": %llu, \"pixels_masked\": %llu, \"pixels_written\": %llu, "
        "\"cover_max_per_sprite\": %u, \"cover_violations\": %zu",
        (unsigned long long)sp.spans, (unsigned long long)sp.pixels_covered,
        (unsigned long long)sp.pixels_rle_skip,
        (unsigned long long)sp.pixels_transp, (unsigned long long)sp.pixels_masked,
        (unsigned long long)sp.pixels_written, sp.cover_max, sp.violations.size());
    j += buf; j += "},\n";

    /* Histogram liczby ZAPISÓW na piksel — overdraw całej sceny.
     * Niezmiennik D14 ("dokładnie 1") dotyczy pokrycia JEDNEGO sprajta
     * i siedzi w "spans.cover_max_per_sprite", nie tutaj. */
    j += "  \"write_histogram\": [";
    for (size_t i = 0; i < write_hist.size(); ++i) {
        std::snprintf(buf, sizeof buf, "%s%u", i ? ", " : "", write_hist[i]);
        j += buf;
    }
    j += "],\n";

    std::snprintf(buf, sizeof buf, "  \"overrun\": %s,\n", st.overrun ? "true" : "false");
    j += buf;

    j += "  \"warnings\": [";
    const auto &w = m.warnings();
    for (size_t i = 0; i < w.size(); ++i) {
        j += (i ? ", \"" : "\"");
        j += esc(w[i]);
        j += "\"";
    }
    j += "]\n}\n";
    return j;
}

} /* namespace mcel */
