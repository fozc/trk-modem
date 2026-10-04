# AGENTS.md — troika-smart-breaker-modem

This file is the agent entry point for this repository. It mirrors CLAUDE.md
and the directives under `.github/`. Full standards live in:

- `CLAUDE.md` — project overview, architecture, toolchain, repo workflow
- `.github/copilot-instructions.md` — embedded engineering directives
- `.github/instructions/barr-c.instructions.md` — BARR-C:2018 C style standard
- `.github/instructions/cpp.instructions.md` — Embedded C++20 subset rules
- `.github/instructions/cortex-m-atomic-isr.instructions.md` — ISR/main-context
  shared state: synchronization policy (mandatory reference for ISR sharing)

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
- **Integer type policy:** use `<stdint.h>` fixed-width types
  (`uint8_t`..`uint64_t`, `int8_t`..`int64_t`) for integer data, counters,
  IDs, protocol fields, and stored values. Use `size_t` for buffer sizes,
  lengths, and array indices; `ptrdiff_t` for pointer differences; `bool`
  for Boolean state; enums for named status/state codes. `char` for text
  and `float`/`double` for required
  real-valued measurements remain allowed. Do not introduce plain
  `int`, `unsigned int`, `short`, or `long` for application data.
  Standard-library, HAL, and existing public API signatures may require
  these types: preserve their contracts and convert explicitly only at
  the boundary after proving the value range. Do not bulk-replace types
  or add casts merely to silence warnings. Format arguments must match
  the formatter's actual supported formats and expected argument types.
  **`errno` is forbidden** — use the project `status_t` enum.
- **MISRA essentials** — every `switch` has a `default`; every
  `if ... else if` ends with an `else`; no VLA, no recursion, no
  back-jumping `goto`, no commented-out code.
- **ISR-shared state & atomics:** follow
  `.github/instructions/cortex-m-atomic-isr.instructions.md` section 2.1:
  prove actual access paths, ownership, protocol role and existing
  protection before choosing synchronization. Use the simplest adequate
  mechanism. If all competing accesses are excluded by a short targeted
  IRQ critical section, do not add redundant atomics. Preserve the prior
  IRQ state and verify compiler/CMSIS barriers; masking one IRQ does not
  exclude other ISRs, tasks or DMA. For remaining atomic semantics, use
  C11 `<stdatomic.h>` with explicit operations and memory orders
  (`atomic_fetch_or` to set flags, `atomic_exchange` for atomic
  read-and-clear, release/acquire for publication, `relaxed` for
  independent counters). `volatile` alone is **not** a synchronization
  primitive (MMIO / simple single-writer flags, or access visibility
  under proven IRQ exclusion). Multi-field transactions use short
  critical sections; prefer the relevant IRQ over global PRIMASK when
  sufficient. Enforce lock-free for
  ISR-path atomics with
  `_Static_assert(__atomic_always_lock_free(sizeof(T), 0), ...)`
  next to the definition (verified on GCC 14.3.rel1 / Cortex-M33:
  byte and word RMW inline as LDREXB/STREXB retry loops, no library
  calls; acquire/release compile to instruction variants, no DMB).
  Exact-width atomic
  typedefs (`atomic_uint8_t` etc.) are C11-optional — use `_Atomic T`
  or the mandatory `atomic_uint_leastN_t`/`_fastN_t` family.
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
- **File header author:** every new or edited C/C++ file header must use
  `Author: Fatih Ozcan` with `fatihozcan@gmail.com` on the next aligned
  line. No other author name or email is allowed. Use the BARR-C header
  template above the code; this spelling preserves the ASCII-only rule.
- Functions <= 100 lines, <= 5 parameters; private functions `static`;
  include order: own header, project, HAL, standard.
- **Logging:** use cslog (`CSLOG(...)`, `xsprintf`) — no bare
  `printf`/`sprintf`. Logging compile-time removable.

## Evidence and simplicity before implementation

- Before proposing or coding a fix, verify that the reported condition
  exists in the current code and is reachable in the actual call path.
  State the trigger and evidence (source lines, a reproduction, or tests).
  Separate confirmed behavior from assumptions and untested hardware.
- Inspect the exact versions of the existing HAL, drivers, libraries,
  and frameworks first. Check whether they already handle the condition,
  including their recovery paths, limits, and return values. Use local
  source and authoritative documentation; do not invent probabilities,
  hardware behavior, or requirements. If evidence is missing, investigate
  or report the uncertainty before designing a workaround.
- Implement only after presenting the evidence and the smallest adequate
  solution. Reuse existing infrastructure. Prefer simple control flow
  and minimal state; do not add speculative fallback algorithms, retry
  layers, background processes, resets, or abstractions without a
  demonstrated need. Complexity must have a concrete, explained benefit.
- Keep verification proportional to the change and test the actual failure
  path. These rules do not replace the user approval requirements below.
- Regression tests belong in the repository's existing test infrastructure,
  not only in temporary scripts. Exercise actual production entry points and
  observable behavior; mock hardware/transport boundaries, not the algorithm
  being verified. Include invalid-input behavior and preservation of existing
  valid state where relevant. Host tests do not prove physical timing.
- Keep the production audit report current when handling its findings:
  record evidence, changes, test scope and remaining limits. An explicit
  deferral is not a fix. Recheck current sources before reopening an old
  finding; do not treat historical line numbers or test counts as current.

## Repo-specific constraints

- **NVRAM layout changes need discipline:** bump
  `NVRAM_SCHEMA_VERSION`, add fields only at the tail (before `crc`),
  update the layout asserts in `types.h` (`sizeof`/`offsetof`, measured
  on the project toolchain), and — once devices exist in the field —
  write a migration plus a host test. The image header
  (magic/version/length/sequence) is self-describing and the dual
  copies are arbitrated by `sequence`; keep both properties intact.

- **Critical decisions require user approval:** never make or commit a
  decision that directly affects the user without asking first. This
  includes: git commits, git operations (push/merge/rebase/reset),
  architecture choices, API design changes, protocol behavior decisions,
  data model changes, linker/memory layout changes, and any destructive
  file operation. Present the options, state a recommendation, and wait
  for explicit approval when the decision is not already authorized.
  Existing user authorization persists; do not ask again for routine work
  within it. Read-only git inspection does not require mutation approval.
- **Layering:** Application -> Service/Driver -> HAL/HW. No direct register
  access outside `Core/` or BSP.
- **Shell command output channel:** shell command handlers must print their
  response with `SHELL_LOG`/`SHELL_CLOG` (routed to the active terminal,
  web included). `CSLOG` is for background/process logging only — output
  printed with `CSLOG` is invisible to the web terminal.
- **CubeMX:** edit only between `/* USER CODE BEGIN/END */` markers;
  everything else is regenerated.
- **Linker configs:** Debug `STM32U375VETX_FLASH.ld` (0x08000000),
  Release `STM32U375VETX_BOOT.ld` (0x08014000, 80 KB BSL). Changing the
  base address requires updating VTOR too.
- **Contiki port caveat:** `critical_enter()` is a no-op on ARM
  (`_PLATFORM_=_WIN32_` build). Never depend on it masking IRQs; use
  verified IRQ exclusion or appropriate atomics for ISR-shared state;
  `volatile` alone is not protection.
- Preserve code disabled with `#if 0`. Do not remove these blocks unless
  the user explicitly requests their removal. This is an exception to
  the commented-out-code cleanup rule.
- Match surrounding code style where it diverges from BARR-C; follow the
  standards fully for new files.
- When changing a module's logic, add/run the relevant Ceedling tests in
  the central `test/<module>/` infrastructure. Follow `test/README.md`;
  use existing integration packages for web and cross-module behavior.
  When a commit is requested, include the relevant regression tests.
- **Never invent hardware details** (registers, timing, clocks) — leave a
  `TODO` and ask.

## Existing project decisions

The current project decisions and their sources are recorded in
`engineering-guidelines/architecture.md` sections A08-A11. Review them
before changing the affected behavior. In particular:

- Preserve dummy producers until real data and infrastructure are available;
  do not remove or activate subsystems to close an audit finding by appearance.
- Respect explicit deferrals and scope exclusions. Contiki kernel and the
  excluded ST/CubeMX warning fixes remain out of scope unless reopened by
  the user. BMS reader fixes do not authorize enabling the reader.
- Lifetime remains in the existing NVRAM field. Do not move it to a separate
  Flash area or migrate it without a new user decision. Account for Flash
  endurance when choosing persistence frequency.
- Preserve the distinction between the BSP availability random fallback and
  secure random APIs. HAL startup belongs to main/CubeMX; do not repeat it
  in `bsp_random_init()`. Reuse existing BSP, reset and retry infrastructure.
- Development keys are intentionally tracked for now; follow `keys/README.md`.
  Do not remove, rotate, archive or rewrite their Git history automatically.
  This decision does not approve them as production keys.

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
