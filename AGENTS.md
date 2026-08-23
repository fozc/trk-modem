# AGENTS.md — troika-smart-breaker-modem

This file is the agent entry point for this repository. It mirrors CLAUDE.md
and the directives under `.github/`. Full standards live in:

- `CLAUDE.md` — project overview, architecture, toolchain, repo workflow
- `.github/copilot-instructions.md` — embedded engineering directives
- `.github/instructions/barr-c.instructions.md` — BARR-C:2018 C style standard
- `.github/instructions/cpp.instructions.md` — Embedded C++20 subset rules

Read those files when writing or reviewing C/C++ in this repo. The
non-negotiable rules are restated here so they are never missed.

## Project

Smart breaker modem firmware for the **STM32U375VETx** MCU (Cortex-M33,
TrustZone Non-Secure, 96 MHz). Contiki process kernel; Modbus RTU,
IEC 60870-5-104, SCP, GSM, RF, power-board bus, battery BMS, on-chip HTTP
server. STM32CubeIDE project (`.cproject`, `.ioc`) — **not** CMake.

## Mandatory coding rules

- **No heap, ever** — no `malloc/calloc/realloc/free`, no `new/delete`,
  no heap-based STL containers, no `alloca()`.
- **C11 / Embedded C++20**, warning-free under
  `-Wall -Wextra -Werror -Wshadow -Wconversion -Wdouble-promotion -Wformat=2`.
- **Fixed-width types** (`uint8_t`..`int32_t`); `size_t` for sizes/indices.
  **`errno` is forbidden** — use the project `status_t` enum.
- **MISRA essentials** — every `switch` has a `default`; every
  `if ... else if` ends with an `else`; no VLA, no recursion, no
  back-jumping `goto`, no commented-out code.
- **BARR-C:2018 style** — Allman braces, 4 spaces, 80 columns, LF line
  endings, no tabs, Yoda conditions
  (`NULL == obj`), `for (;;)` for infinite loops, signed/unsigned never
  mixed, unsigned constants suffixed (`6U`), `/*** end of file ***/`
  trailer, new files use the §9.15 header template.
- **Repo deviation (decided 2026-08-20):** do NOT use the `g_` / `s_`
  variable prefixes or the `p_` pointer prefix — use plain descriptive
  snake_case names for globals, statics, pointers, and parameters.
  (Yoda conditions still apply.)
- **Naming discipline:** before introducing a function or variable,
  THINK FIRST and pick the widely accepted, conventional name from
  general programming practice that reads naturally in context
  (e.g. `buffer_len`, `frame_len`, `is_valid`, `has_pending`,
  `parse_frame`, `send_request`, `handle_response`). Prefer the most
  common, meaningful term; avoid invented synonyms, cute names, or
  unnecessary abbreviations. Names are written for the next reader,
  not the author.
- **ASCII only** in code and comments — no Turkish characters.
- Functions <= 100 lines, <= 5 parameters; private functions `static`;
  include order: own header, project, HAL, standard.
- **Logging:** use cslog (`CSLOG(...)`, `xsprintf`) — no bare
  `printf`/`sprintf`. Logging compile-time removable.

## Repo-specific constraints

- **Layering:** Application -> Service/Driver -> HAL/HW. No direct register
  access outside `Core/` or BSP.
- **CubeMX:** edit only between `/* USER CODE BEGIN/END */` markers;
  everything else is regenerated.
- **Linker configs:** Debug `STM32U375VETX_FLASH.ld` (0x08000000),
  Release `STM32U375VETX_BOOT.ld` (0x08014000, 80 KB BSL). Changing the
  base address requires updating VTOR too.
- **Contiki port caveat:** `critical_enter()` is a no-op on ARM
  (`_PLATFORM_=_WIN32_` build). Never depend on it masking IRQs; use
  `volatile` flags or atomics for ISR-shared state.
- Match surrounding code style where it diverges from BARR-C; follow the
  standards fully for new files.
- When changing a module's logic, run its Ceedling test under that
  module's `test/` directory.
- **Never invent hardware details** (registers, timing, clocks) — leave a
  `TODO` and ask.

## Language & wording directive (docs, comments, commit messages)

- **ASCII-only rule applies to code and code comments.** This section
  governs natural-language text the agent produces (documentation, README,
  explanations, commit messages).
- **Document creation guide:** for any technical document the agent
  writes or reformats (guides, specs, protocol docs), follow
  `doc/belge_yazim_rehberi.md` — plain everyday Turkish, English terms
  kept with parenthetical Turkish on first use, "-malıdır" rule form,
  fixed heading labels (Amaç/Kullanım yeri/...), one rule in one place
  with cross-references, and the recommended document skeleton
  (protocol docs: title page, glossary, roles, common rules, command
  pages, flows, timing table, examples, changelog).
- When writing Turkish text, prefer the **most common, widely-used word
  or expression** for a concept — avoid rare, dated, or overly literary
  synonyms. For example prefer "kullanici girisi" over obscure variants.
- For technical terms, keep the established **English term** (or add it in
  parentheses after the Turkish wording) so meaning is never lost:
  e.g. "bellegi tukendi (out of memory)", "gonderici (transmitter)".
  When no well-established Turkish equivalent exists, use the English
  term directly rather than a confusing literal translation.
- The goal is **semantic fidelity**: never let word choice obscure or
  change the technical meaning. If a Turkish rendering could be
  misinterpreted, state the English term alongside it.
