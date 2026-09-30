# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

This is **not** the AltirraBridge source tree — it is an unpacked **prebuilt distribution package** of AltirraBridge (see `BUILD-INFO.txt` for version/commit). The upstream source lives at `src/AltirraSDL/AltirraBridge/` in the Altirra tree; this directory contains only the shipped artifacts: the headless server binary, client SDKs, docs, examples, and a Claude Code skill.

Do not try to build the server from here. There is no CMake tree, no C++ sources for the server. If the user asks to modify server behavior, they need the upstream Altirra repo.

## Running the server

```sh
./AltirraBridgeServer --bridge=tcp:127.0.0.1:0
```

Prints listening address and a token-file path to stderr. The token file (in `/tmp/`) contains the address + a 128-bit session token; filesystem permissions gate access. Deleted on clean shutdown. Use `--bridge=tcp:127.0.0.1:<port>` for a fixed port.

## SDKs (what's editable here)

- `sdk/python/` — pure stdlib Python client. Import via `PYTHONPATH=sdk/python`, or `pip install ./sdk/python`. Examples in `sdk/python/examples/` take a token-file path as argv[1].
- `sdk/c/` — single-file C client (`altirra_bridge.h` + `.c`, libc only; ws2_32 on Windows). Standalone CMake in `sdk/c/examples/`:
  ```sh
  cmake -B build_c_examples -S sdk/c/examples && cmake --build build_c_examples -j
  ```
  Prebuilt example binaries ship in `sdk/c/examples/bin/`.
- `sdk/pascal/` — single-file Free Pascal client (stdlib only). Build examples with `fpc -Fu.. 01_ping.pas`.

All three SDKs expose the same surface: `from_token_file` / `ConnectTokenFile` / `atb_connect_token_file`, then `ping`/`pause`/`frame`/`peek`/`poke`/`regs`/`boot`/`key`/`joy`/`screenshot`. They are 1:1 ports of each other — when changing one, check whether the others need the parallel change.

## Analyzer toolkit (Python only)

The Python SDK includes `altirra_bridge.analyzer`, a built-in reverse-engineering toolkit for 6502 / Atari 8-bit binaries. It is split into focused submodules:

| Module | Purpose | Needs live emu? |
|--------|---------|:---:|
| `analyzer.hw` | Hardware register classification, PORTB decode, auto-labelling | no |
| `analyzer.disasm` | Recursive-descent disassembler (`recursive_descent`) | no |
| `analyzer.patterns` | Gap/data classification, address-table scanning | no |
| `analyzer.variables` | Variable cross-ref and reporting | no |
| `analyzer.subroutines` | Single-subroutine deep analysis, name/comment inference | no |
| `analyzer.procedures` | `build_procedures`, `call_graph_context`, `detect_subsystems`, `suggest_name_from_graph` | no |
| `analyzer.sampling` | PC sampling, DLI chain, PORTB monitor, memory diff | **yes** |
| `analyzer.adapter` | `BridgeEmu` — wraps `AltirraBridge` as the emu interface | — |

All public names re-export from `altirra_bridge.analyzer` for convenience. See `sdk/python/README.md` for usage examples. `examples/case_studies/` contains worked reverse-engineering walkthroughs that use this toolkit.

## MADS source exporter — `altirra_bridge.asm_writer`

`asm_writer.write_all(bridge, image, proj, output_dir, *, reconstructed=None, emit_procs=True)` is the top-level entry point. It writes `main.asm`, `equates.asm`, and per-segment source files. The analyzer pass runs automatically (one `recursive_descent` + one `build_procedures` over a 64 KB unified memory view) and feeds a per-segment `_build_proc_info()` filter that wraps safe procedures in MADS `.proc`/`.endp` blocks. Pass `emit_procs=False` to skip the analyzer pass (useful for speculative label sets).

The exporter has two modes:

- **Byte-exact mode** (default, or `reconstructed=False`): each XEX segment is emitted at its load address. `verify(proj, output_dir)` reassembles with MADS and compares the result to the original XEX — on success it reports `"VERIFIED: byte-exact match"`.
- **Reconstructed mode** (`reconstructed=True`, auto-enabled when `proj.copy_sources` is non-empty): re-emits copy-source byte ranges at their **runtime** addresses using `proj.copy_sources` metadata (populated via `proj.mark_copy_source(xex_start, xex_end, runtime_start, copy_routine=, runtime_entry=)`). Project labels and comments that live at runtime addresses now line up with the generated asm, so the game-code segments finally get real label definitions and inline comments instead of just `loc_XXXX:` synthetics. Bootstrap ranges (relocator body, init segments, stale INITAD vectors) can be dropped with `proj.exclude_from_reconstructed(start, end)`. The output XEX is **not** byte-identical to the original, but it boots — the RUN vector is replaced with `runtime_entry` so it enters the relocated code directly.

When choosing which mode: use byte-exact when the XEX is already laid out at runtime addresses and you want `verify()` to prove round-trip. Use reconstructed when the game relocates itself at boot (init-segment + relocator pattern) and you want the exported asm to carry project labels on the game-code segments.

## Protocol (the actual contract)

The wire protocol is **newline-delimited JSON over TCP (or Unix domain socket on Linux/macOS)**. This is the source of truth for what the SDKs do — read it before changing client code:

- `docs/PROTOCOL.md` — framing, encoding, auth handshake, semantics
- `docs/COMMANDS.md` — one-line reference for every command
- `docs/WRITING_A_CLIENT.md` — ten-bullet distillation + worked Rust and Go clients (~80 lines each)

Commands are grouped by phase (see README Status table): skeleton (`HELLO`/`PING`/`FRAME`/`PAUSE`), state read (`REGS`/`PEEK`/`ANTIC`/`GTIA`/...), state write + input (`POKE`/`JOY`/`KEY`/`BOOT`/save states), rendering (`SCREENSHOT`/`RAWSCREEN`/`RENDER_FRAME`/`VIDEO_OUTPUTS`; `output=NAME` captures a device video output such as MARIA), debugger (`DISASM`/`HISTORY`/breakpoints/watches/symbols/profiler). Phase 5c (`SIO_TRACE`, `VERIFIER_REPORT`, `SYM_FIND`, tracepoint format strings) is deferred/pending.

## Android / headless-filesystem note

On Android (and any transport where client and server don't share a filesystem), use the `inline=true` variants of `SCREENSHOT` / `RAWSCREEN` / `MEMDUMP` so payloads come back over the socket instead of via a temp file.

## GUI variant

`AltirraSDL --bridge` (from the regular AltirraSDL release, not shipped in this package) speaks the identical protocol with a visible window + audio. `--bridge --headless` gives the SDL3 frontend's deferred-action queue and settings persistence without a display. Token-file format and SDKs are unchanged.

## Case studies and the skill

- `examples/case_studies/` — reverse-engineering walkthroughs driving the bridge.
- `skills/altirra-bridge/` — Claude Code skill for AI-assisted RE workflows. If the user is doing Atari RE work through this package, that skill is the intended entry point.
