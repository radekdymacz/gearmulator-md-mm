# Future emulation ideas

Written 2026-09-28, from a discussion with Radek. These are ideas only; nothing is started. Before any of it: finish P6 (the simple foundation), decide the own-engine phase, then a hammock session (VISION.md + DESIGN.md).

## 1. Table as data (the core idea)

Describe a CPU or DSP as a table: one row per instruction.

```
name:    ADD #xx,D
bits:    0000000101iiiiii10ood000
reads:   D, i
writes:  D, flags(C V Z N E U)
does:    D = D + (i << 16)
cycles:  1
```

Small generic programs read the table and generate:
- the decoder;
- the interpreter;
- the x64 and ARM64 JITs;
- the disassembler;
- the tests.

There is one source of truth: a fix in a row fixes everything. A new chip is a new table plus a few rules. AI is good at turning a chip manual into rows. Differential testing against a reference catches its mistakes.

**Gearmulator today** (`source/dsp56300/source/dsp56kEmu`):
- **Decode is a table.** `opcodeinfo.h` has bit patterns with field letters, parsed at compile time.
- **Semantics are hand-written three times:**
  - the interpreter (`dsp_ops_*.inl`);
  - x64 (`jitops_*_x64.cpp`);
  - ARM64 (`jitops_*_aarch64.cpp`).
  The ALU alone is about 1,350 + 1,450 + 830 + 780 lines.
- **It already has:**
  - lazy CCR flags (`jitops_ccr.cpp`);
  - a block JIT with chaining;
  - a register pool and an optimiser;
  - asmjit for x64 and ARM64;
  - unit tests.

The expected gain from the table approach is mainly effort and correctness, not raw speed. Speed has to be earned with the tricks below and measured on real firmware.

## 2. Speed tricks (biggest first)

1. **AOT translation.** The firmware is fixed and rarely self-modifying. Translate the whole DSP program once with a full compiler (LLVM), and cache the result.
2. **Hot loops.** Profile, unroll, keep values in host registers, and map to host SIMD. The SHARC's dual PEx/PEy units map naturally onto it.
3. **Skip work:**
   - dead flags;
   - dead register writes;
   - idle "wait for interrupt" loops.
4. **Fewer inter-chip syncs.** Sync only at real communication points, with one host core per emulated chip and lock-free queues between them.
5. **HLE of known kernels.** Replace a recognised routine (a reverb, a filter) with native code, proven bit-exact by tests.
6. **Per-ROM profiles.** Record the hot blocks per firmware and compile them first.

## 3. Browser build (WASM)

- **Build:** Emscripten compiles the emulator's C++ to WebAssembly.
- **Audio:** an AudioWorklet, with WASM threads and SIMD128. This needs the COOP/COEP headers for SharedArrayBuffer.
- **JIT:** a browser cannot write native machine code. Instead:
  - JIT to WASM modules at runtime (as v86 does); or
  - better, AOT-translate the firmware to WASM (trick 1).
- **Speed:** roughly 1.5–3× slower than native. The Machinedrum is about 0.5 core native, so about 1–1.5 cores in a browser. That is OK on laptops; phones will struggle.
- **Fit:** the editor is already HTML and JS, so the same page runs in the browser. The user loads their own ROM locally; it is never uploaded.

## 4. Devices surveyed

| Device | What is inside | Public work | Chance |
|---|---|---|---|
| Machinedrum / Monomachine | ColdFire + DSP563xx | gearmulator, this fork | done |
| Digitone 1 | 2× ColdFire MCF5441x (sound likely on ColdFire — not verified) | digikit ColdFire emulator | possible |
| Digitakt II / Digitone II | ColdFire MCF54415 + SHARC+ ADSP-21569 (up to 1 GHz) | `m-dwyer/digikit` (GPL-2: boots the OS, no SHARC audio); `angellinares/dn2_firmware_explore` (AGPL-3: OS mods, offline SHARC runner) | hard: needs a real-time SHARC+ emulator |
| Nord Drum 2 | unknown. The OS v3.00 updater holds a 480 KB image at 0x8000 (a 512 KB flash layout, 32 KB bootloader), encrypted. This points to a flash MCU, probably not a 56k | editors only (Momo), no emulator | unknown, and the encryption is a legal red line |

I found no public real-time SHARC+ emulator. That covers public sources only; private work, for example on Discord, cannot be ruled out. UA may have an internal SHARC-on-x86 emulator (a Reddit PSA; not read, because Reddit was blocked for my tools).

## 5. Legal and monetisation (not legal advice — get an IP lawyer)

- **Allowed:** emulator code for interoperability (Sega v. Accolade; Sony v. Connectix; EU 2009/24/EC arts. 5–6), cores written from public manuals, and editors for real hardware.
- **Not allowed:** shipping firmware; using the makers' trademarks as a brand ("compatible with" only).
- **Red line:** circumventing encrypted or signed firmware (DMCA §1201 / EU rules).
- **GPL code cannot be closed.** gearmulator is GPL-3, digikit GPL-2, dn2 AGPL-3. A closed product needs a clean room:
  - a "dirty" agent reads the GPL code and writes behaviour notes only;
  - a "clean" agent works from the manual plus those notes, and never gets GPL source in its context;
  - code-similarity checks and provenance logs back it up.
  AI training-data leakage is a real risk to manage.
- **Best business:**
  - license a clean-room SHARC+ core to companies with legacy SHARC code, so they can run it natively without a rewrite;
  - sell fast SHARC developer tools (simulator, CI, time-travel debugging).
  Open instrument emulations are the free showcase.
- **Clean-room DSP56300:** legal and doable, but a smaller market (legacy 56k makers such as Access, Waldorf, Clavia). Be open about the process, to protect our reputation with the gearmulator community.

## 6. First steps, when the time comes (read-only research, no code)

1. Study and profile gearmulator's DSP56300 JIT on the Machinedrum; write a report of ideas and numbers, with no code copied.
2. Check the ADI CrossCore Embedded Studio simulator: does it support the ADSP-21569, what is its licence, does it run on macOS or only Windows?
3. Verify where the Digitone 1's audio runs.
4. Get a Nord Drum 2 board photo.
5. Hammock: the table format, the milestone plan (a correct offline sound first, then measure the speed gap, then the JIT/AOT decision).
