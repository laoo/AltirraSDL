# MariaCEL reference model (vendored)

Reference C++ model of the MariaCEL sprite blitter — one module of the
MARIA FPGA cartridge for Atari 8-bit — vendored from the MariaCEL project
(`model/` directory; commit `6f5f42f5cb05b8211d675d1a3fbae9c81a9c5fe2` of
2026-09-08 plus the uncommitted D86 change of 2026-09-09 that adds the
CEL palette through `PBIRAMBANK` — update this note with the commit hash
once that change lands).

The model is the executable specification of the hardware: the 6502 bus
(`Mcel::wr()` / `Mcel::rd()`) is its only input, and the strip buffers read
by `Mcel::scanout()` are its only output. The Altirra device wrapper
(`src/Altirra/source/maria.cpp`) drives it from the emulated bus and the
frame timing of ANTIC.

Local modifications are limited to `friend` declarations that give the
Altirra device wrapper access to the private BRAM/latch state needed for
save states. They are marked with `ALTIRRA:` comments. Everything else is
byte-identical to upstream so the directory can be refreshed by copying.
