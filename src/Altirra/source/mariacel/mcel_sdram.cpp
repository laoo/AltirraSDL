#include "mcel_sdram.h"

#include <cstring>
#include <algorithm>

namespace mcel {

Sdram::Sdram() : mem_(SIZE, 0) {}

void Sdram::poke(uint32_t addr, const void *src, size_t n) {
    if (addr >= SIZE) return;
    n = std::min<size_t>(n, SIZE - addr);
    std::memcpy(&mem_[addr], src, n);
}

void Sdram::peek(uint32_t addr, void *dst, size_t n) const {
    std::memset(dst, 0, n);
    if (addr >= SIZE) return;
    n = std::min<size_t>(n, SIZE - addr);
    std::memcpy(dst, &mem_[addr], n);
}

void Sdram::reset_banks() {
    for (int i = 0; i < 4; ++i) open_row_[i] = -1;
    have_direction_ = false;
}

/* Otwarcie wiersza dla podanego adresu słowa.
 * prev_data_cycles — ile taktów danych trwa właśnie obsługiwany kawałek
 * poprzedniego banku. ACTIVATE kolejnego banku wystawia się w trakcie tamtego
 * bursta, więc gdy poprzedni kawałek był dostatecznie długi, narzut ZNIKA
 * (02-architecture §7: "dla transferów >= 1 KB narzut aktywacji znika"). */
int Sdram::open_row_for(uint32_t word_addr, int prev_data_cycles) {
    const int      bank = (int)((word_addr >> 9) & 0x3);
    const uint32_t row  = word_addr >> 11;

    if (open_row_[bank] == (int)row) { st_.row_hits++; return 0; }

    int cost = 0;
    if (open_row_[bank] >= 0) { st_.precharges++; cost += p_.t_rp; }
    st_.activates++;
    cost += p_.t_rcd;
    open_row_[bank] = (int)row;

    if (prev_data_cycles >= cost) {          /* schowane w trwającym burscie */
        st_.activates_hidden++;
        st_.cycles_overhead += 0;
        return 0;
    }
    cost -= std::max(0, prev_data_cycles);
    st_.cycles_overhead += cost;
    return cost;
}

int Sdram::burst(uint32_t addr, size_t n, bool write) {
    if (n == 0) return 0;

    int cycles = 0;

    if (have_direction_ && last_was_write_ != write) {
        st_.turnarounds++;
        st_.cycles_overhead += p_.turnaround;
        cycles += p_.turnaround;
    }
    have_direction_ = true;
    last_was_write_ = write;

    if (write) { st_.bursts_wr++; st_.bytes_wr += n; }
    else       { st_.bursts_rd++; st_.bytes_rd += n; }

    /* Rozbicie na kawałki nieprzekraczające granicy banku (512 słów = 1024 B). */
    uint32_t a         = addr;
    size_t   left      = n;
    int      prev_data = 0;
    bool     first     = true;

    while (left) {
        const uint32_t word = a >> 1;
        const uint32_t bank_end = ((word >> 9) + 1) << 10;      /* bajtowo */
        const size_t   chunk = std::min<size_t>(left, bank_end - a);

        cycles += open_row_for(word, prev_data);
        if (first) { cycles += p_.cl; st_.cycles_overhead += p_.cl; first = false; }

        const int data = (int)((chunk + p_.bytes_per_cycle - 1) / p_.bytes_per_cycle);
        cycles += data;
        st_.cycles_data += data;

        prev_data = data;
        a        += (uint32_t)chunk;
        left     -= chunk;
    }
    return cycles;
}

int Sdram::burst_read(uint32_t addr, void *dst, size_t n) {
    peek(addr, dst, n);
    return burst(addr, n, /*write=*/false);
}

int Sdram::burst_write(uint32_t addr, const void *src, size_t n) {
    poke(addr, src, n);
    return burst(addr, n, /*write=*/true);
}

int Sdram::cpu_access(uint32_t addr, bool write) {
    /* Port 6502 ma bezwzględny priorytet (D16) i przerywa strumień blittera:
     * trafia w inny wiersz, więc kolejny dostęp blittera zapłaci za ACTIVATE.
     * Modelujemy to jako pełny cykl wiersza w jednym banku. */
    const uint32_t word = addr >> 1;
    const int bank = (int)((word >> 9) & 0x3);
    int cost = 0;
    if (open_row_[bank] >= 0) { st_.precharges++; cost += p_.t_rp; }
    st_.activates++;
    cost += p_.t_rcd + p_.cl + 1;
    open_row_[bank] = (int)(word >> 11);
    if (have_direction_ && last_was_write_ != write) {
        st_.turnarounds++; cost += p_.turnaround;
    }
    have_direction_ = true;
    last_was_write_ = write;
    st_.cycles_cpu6502 += cost;
    return cost;
}

int Sdram::refresh() {
    /* Odświeżanie wymaga PRECHARGE ALL, więc zamyka wszystkie wiersze
     * (02-architecture §7). Kontroler odtworzy wiersz przy kolejnym żądaniu —
     * w modelu widać to jako ACTIVATE policzony przez następny burst. */
    st_.refreshes++;
    int cost = p_.t_rp + p_.t_rc;
    for (int i = 0; i < 4; ++i) open_row_[i] = -1;
    st_.cycles_refresh += cost;
    return cost;
}

} /* namespace mcel */
