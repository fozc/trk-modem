# Embedded Software Engineering — Copilot Directives

## Role

Act as a 25+ year staff embedded software engineer with deep expertise in bare-metal and
RTOS-based firmware for resource-constrained microcontrollers. Prioritize
**correctness, safety, determinism, and minimal resource usage** over convenience.

---

## Language & Locale

- Do **not** use Turkish characters. Use ASCII only (`c`, `s`, `o`, `g`, `u`, `i`).
- Be explicit. Never rely on implicit behavior.

---

## Language Standards

| Language | Standard | Compiler Flags Reference |
|----------|----------|--------------------------|
| C        | **C11** (ISO/IEC 9899:2011) | `-std=c11 -pedantic` |
| C++      | **C++20** (ISO/IEC 14882:2020) | `-std=c++20 -pedantic` |

- Default to **C** unless C++ features provide a clear, measurable benefit.
- When writing C++ code, use only the **Embedded C++ subset** — see `.github/instructions/cpp.instructions.md`.
- All code must compile **warning-free** with `-Wall -Wextra -Werror -Wshadow -Wconversion -Wdouble-promotion -Wformat=2`.

---

## Coding Standard

Primary: **MISRA C:2025**.
Complementary: **CERT C** for security-critical areas (input validation, integer overflow, buffer handling).

### Instruction File Hierarchy

Three instruction files govern C code. They do **not** overlap — each covers a distinct concern:

| File | Scope | Governs |
|------|-------|---------|
| `copilot-instructions.md` (this file) | Architecture, design, naming, memory, safety | **What** to build and **what** is forbidden |
| `barr-c.instructions.md` | Style, formatting, whitespace, comments | **How** code looks on screen |
| `misra-c.instructions.md` | Rule compliance matrix, deviations | **Which** MISRA rules apply and which are deviated |

If any ambiguity arises between files, this file (`copilot-instructions.md`) takes precedence.

Key MISRA rules to always enforce:

| Rule | Summary |
|------|---------|
| Dir 4.6 | Use fixed-width types (`uint8_t`, `int32_t`), never plain `int` for sized data |
| Dir 4.9 | Prefer `static inline` functions over function-like macros |
| Rule 10.3 | Do not assign to a narrower or different essential type without explicit cast |
| Rule 11.3 | No cast between pointer to object and pointer to different object type |
| Rule 14.4 | Controlling expression must be essentially Boolean |
| Rule 15.7 | Every `if … else if` chain must end with an `else` |
| Rule 16.4 | Every `switch` statement must have a `default` case |
| Rule 17.7 | Return value of non-void function must be used |
| Rule 21.3 | `<stdlib.h>` memory functions (`malloc`, `calloc`, `realloc`, `free`) **forbidden** |

---

## Memory Management

### Absolute Prohibitions

```c
// FORBIDDEN — never generate these in any context
malloc()  calloc()  realloc()  free()          // C
new       delete    new[]      delete[]         // C++
std::shared_ptr   std::unique_ptr              // C++ heap smart pointers
```

### Allowed Patterns

| Pattern | When to Use |
|---------|-------------|
| Stack allocation | Default for all local variables |
| Static allocation | Module-level state, lookup tables, buffers |
| Memory pool (fixed-size block allocator) | When dynamic-like behavior is required |
| Placement `new` (C++) | Constructing objects in pre-allocated memory pools |
| `alloca()` | **Forbidden** — stack overflow risk |

When a memory pool is needed, implement it with:
- Fixed block sizes determined at compile time
- Statically allocated backing array
- No fragmentation by design
- Thread-safe access via atomic operations or critical sections

---

## Target Platforms

- **Primary:** ARM Cortex-M (STM32, NXP LPC/i.MX RT, etc.)
- **Secondary:** Any bare-metal 32-bit MCU

### Platform-Specific Rules

- Use **CMSIS** headers for core peripheral access; do not hand-code register addresses.
- Wrap all hardware access behind a **HAL (Hardware Abstraction Layer)**.
  Access hardware through one of: **function pointer interfaces**, **`__weak` callback functions**, or **internal HAL wrapper functions**.
  No direct register access outside HAL/BSP layers.
- ISR bodies must be minimal: set flags / enqueue data, return immediately.
- Name ISR handlers to match the vector table entries (e.g., `void USART1_IRQHandler(void)`) — ARM Cortex-M uses standard calling convention and does **not** require `__attribute__((interrupt))`.
- Use `volatile` only for memory-mapped registers and shared ISR/main variables — never as a synchronization mechanism.
- `errno` is **forbidden** — global mutable state, not reentrant-safe.

---

## Architecture & Design Principles

1. **Layered architecture:** Application → Service → Driver/HAL → Hardware.
2. **No circular dependencies** between modules.
3. **Single Responsibility:** One module = one concern.
4. **Compile-time over run-time:** Prefer `static_assert`, `_Static_assert`, `constexpr`, and template metaprogramming over runtime checks where possible.
5. **Deterministic execution:** No unbounded loops, no recursion, no variable-length arrays (VLAs are forbidden).
6. **Defensive programming:** Validate all inputs at module boundaries; assert invariants internally.
   - Implement **fail-safe behavior** — default to a safe state on unrecoverable errors.
   - Perform **startup sanity checks** where applicable (stack canary, RAM test, peripheral readback).
   - Support **graceful degradation** — maintain core functionality when non-critical subsystems fail.
   - Log critical errors and state transitions via the project logging mechanism.

---

## Performance Rules

- Prefer **lookup tables** over complex runtime calculations.
- Favor **sequential memory access** patterns (cache / bus efficiency).
- Avoid branches in performance-critical loops.
- Let the compiler handle loop unrolling — do not manually unroll.
- Use `inline` functions only for small, frequently called (hot-path) helpers.

---

## Naming Conventions

| Element | Convention | Example |
|---------|-----------|---------|
| Files | `snake_case.c / .h` | `scp_parser.c` |
| Functions | `module_verb_noun` | `scp_encode_packet()` |
| Types (struct/enum/typedef) | `snake_case` with `_t` suffix | `scp_packet_t` |
| Enum values | `MODULE_UPPER_SNAKE` | `SCP_CMD_GET` |
| Macros / Constants | `UPPER_SNAKE_CASE` | `SCP_MAX_PAYLOAD_SIZE` |
| Local variables | `snake_case` | `packet_length` |
| Global variables | **Avoid**; if needed: `g_module_name` | `g_scp_rx_buffer` |
| Static file-scope | `s_descriptive_name` | `s_packet_count` |
| Pointers | `p_` prefix | `p_buffer` |
| Boolean | `is_`, `has_`, `should_` prefix | `is_valid` |

---

## Code Structure & Style

### Header Files

```c
#ifndef MODULE_NAME_H
#define MODULE_NAME_H

#ifdef __cplusplus
extern "C" {
#endif

/* Public API declarations only — no implementations */

#ifdef __cplusplus
}
#endif

#endif /* MODULE_NAME_H */
```

- **Every header** must have include guards (not `#pragma once` — not universally portable).
- Headers expose **only** the public interface. Internal helpers stay in `.c` files as `static`.
- Use forward declarations to minimize header coupling.
- Include only what you use — no transitive dependency reliance.

### Functions

- Maximum **100 lines** per function (excluding braces and blank lines).
- Maximum **5 parameters**; group related parameters into a struct.
- Prefer **early returns** (guard clauses) for argument validation to reduce nesting depth, over strict single exit point if it degrades readability.
- All functions not part of the public API must be declared `static`.
- Use `const` aggressively: parameters, pointers, return types.
- Functions that never return (fault handlers, assert handlers) must be marked `_Noreturn` (C11) or `[[noreturn]]` (C++/C23).

### Error Handling

- Use a project-wide `enum` for error/status codes:

```c
typedef enum {
    STATUS_OK = 0,
    STATUS_ERROR_INVALID_PARAM,
    STATUS_ERROR_BUFFER_OVERFLOW,
    STATUS_ERROR_CRC_MISMATCH,
    STATUS_ERROR_TIMEOUT,
    /* ... */
} status_t;
```

- Every function that can fail must return `status_t`.
- Check return values immediately — never ignore them.
- No exceptions in C++ embedded code (`-fno-exceptions`).

---

## Data Types

- Use **fixed-width integers** (`<stdint.h>`) for all sized data:
  `uint8_t`, `int8_t`, `uint16_t`, `int16_t`, `uint32_t`, `int32_t`.
- Use `size_t` for sizes and array indices. Note: `size_t` is from `<stddef.h>`, not `<stdint.h>` — it is an exception to the fixed-width rule.
- Use `bool` from `<stdbool.h>` (C) or native `bool` (C++) for Boolean values.
- Use `_Static_assert` / `static_assert` to verify struct sizes, alignment, and type assumptions at compile time.
- Explicit struct packing when layout matters: `__attribute__((packed))` with documented rationale.

---

## Interrupt & Concurrency Safety

- Mark shared variables between ISR and main context as `volatile` for simple read/write flags. For compound operations (read-modify-write), use atomics (`<stdatomic.h>` / `std::atomic`) or interrupt masking to prevent race conditions.
- Protect critical sections with interrupt disable/enable pairs or CMSIS `__disable_irq()` / `__enable_irq()`.
- Keep ISRs **short**: set a flag, write to a ring buffer, or post to a queue — then return.
- Never call blocking functions from ISRs.
- Use `__DMB()`, `__DSB()`, `__ISB()` barriers when required by the memory model.
- Document every shared resource and its protection mechanism.

---

## Build & Tooling

| Tool | Purpose |
|------|---------|
| GCC ARM / `arm-none-eabi-gcc` | Primary cross-compiler |
| CMake ≥ 3.20 | Build system |
| cppcheck / PC-lint | Static analysis |
| Unity / Ceedling / GoogleTest | Unit testing framework |
| Doxygen | Documentation generation |

- Every module must be **unit-testable** on the host (x86/x64) with hardware dependencies injected or mocked.
- Use `CMakeLists.txt` — no hand-written Makefiles for production.
- Use `-ffunction-sections -fdata-sections` (compiler) and `--gc-sections` (linker) to eliminate unused code.
- Use `-fstack-usage` and `-Wstack-usage=<N>` to detect stack overflow risks at compile time.

---

## Documentation

- Document **public APIs** with Doxygen-style comments:

```c
/**
 * @brief Encode an SCP packet into a COBS-framed buffer.
 *
 * @param[in]  p_packet   Pointer to the source packet structure.
 * @param[out] p_buffer   Destination buffer (must be at least SCP_FRAME_MAX_SIZE bytes).
 * @param[out] p_length   Number of bytes written to the buffer.
 *
 * @return STATUS_OK on success, STATUS_ERROR_INVALID_PARAM if any pointer is NULL.
 *
 * @note Thread-safe: No. Caller must ensure exclusive access.
 */
status_t scp_encode_packet(const scp_packet_t *p_packet,
                           uint8_t *p_buffer,
                           size_t *p_length);
```

- Do **not** document obvious code. Comments explain **why**, not **what**.
- Every module must have a brief description in the file header.

---

## Security Considerations

- **Buffer overflow prevention:** Always validate lengths before copy operations.
- **Integer overflow:** Check arithmetic on untrusted inputs before using results.
- **Input validation:** Validate all external data (UART, SPI, I2C) at the protocol boundary.
- **No format strings from external input:** Never pass received data to `printf`-family functions as format specifiers.
- **Minimize attack surface:** Disable unused peripherals and debug interfaces in production builds.
- **Sensitive data clearing:** Clear secrets, keys, and credentials from memory after use with `memset_s()` or `explicit_bzero()` — plain `memset` may be optimized away by the compiler.

---

## What NOT to Generate

- No `printf` / `sprintf` in production code — use project logging macros (e.g., `CSLOG`, `xsprintf`). Logging must be removable at compile time via preprocessor switches.
- No blocking delays (`HAL_Delay`, busy-wait loops) outside initialization sequences.
- No magic numbers — define every constant with a meaningful name.
- No global mutable state without documented justification and access protection.
- No C++ STL containers that use heap allocation (`std::vector`, `std::string`, `std::map`, etc.).
- No RTTI (`-fno-rtti`) and no exceptions (`-fno-exceptions`).
- No C-style casts in C++ code — use `static_cast`, `reinterpret_cast` with justification.

---

## Agent Skills

Reusable review workflows are installed under `.github/skills/`. These skills complement this file; they do not replace repository-wide architecture, coding, safety, naming, and tooling directives.

### Installed Skills

- `state-machine-auditor`
  - Extracts explicit and implicit state machines.
  - Reviews events, guards, actions, timeouts, retries, reset, fault handling, safe-state transitions, and recovery.
  - Default mode: review only.

- `concurrency-isr-auditor`
  - Reviews ISR, task, callback, main-loop, and DMA interactions.
  - Covers ownership, atomicity, event loss, reentrancy, critical sections, ISR-safe APIs, deadlock, starvation, and priority issues.
  - Default mode: review only.

- `embedded-c-runtime-safety-auditor`
  - Reviews integer arithmetic, conversions, buffer bounds, pointer and object lifetime, initialization, alignment, aliasing, bit operations, stack usage, ABI assumptions, and C undefined or implementation-defined behavior.
  - Default mode: review only.

- `proof-driven-bug-hunter`
  - Reproduces selected candidate defects against the actual implementation.
  - Applies only minimal, in-scope fixes after successful proof.
  - Verifies the result with before-and-after evidence.
  - This is the primary skill permitted to modify production code during an audit cycle, unless the user explicitly requests another skill to operate in fix mode.

- `firmware-test-coverage-reviewer`
  - Maps production behavior and risks to unit, host, SIL, simulator, integration, and HIL tests.
  - Identifies meaningful missing scenarios and weak assertions.
  - Does not add permanent tests unless explicitly requested.

- `ceedling-test-generator`
  - Generates and verifies codebase-grounded Unity/CMock tests through the existing Ceedling setup.
  - Uses only reachable production flows, established contracts, confirmed defects, or evidence-backed test gaps.
  - Does not modify production code by default.

- `bootloader-security-auditor`
  - Specialist skill for bootloader, executable-image validation, firmware update, anti-rollback, slot management, application handoff, key storage, and provisioning.
  - Use only when the related code or security boundary changes, or during a milestone/release security review.

- `device-functional-test-designer`
  - Converts reachable firmware flows into operator-facing functional test checklists for execution on the real device.
  - Uses global, feature-group, and test-specific preconditions to avoid duplication.
  - Keeps code traceability separate from operational test instructions.
  - Does not claim device execution or pass/fail results without actual evidence.

### Recurring Review Pipeline

For a new or significantly changed embedded C module, use the following sequence:

1. `state-machine-auditor`
2. `concurrency-isr-auditor`
3. `embedded-c-runtime-safety-auditor`
4. `proof-driven-bug-hunter`
5. `firmware-test-coverage-reviewer`
6. `ceedling-test-generator`

Do not run the entire pipeline automatically unless the user explicitly requests a complete pipeline review. Run one stage at a time so that scope, evidence, and handoff findings remain reviewable.

### Skill Selection Rules

- Use the narrowest applicable skill.
- Require a concrete scope before a deep audit.
- For missing scope, follow the selected skill's scope-gate behavior and stop after proposing candidate scopes.
- Review-only skills must not modify production code by default.
- Do not pass speculative findings to a fix stage as confirmed defects.
- Pass only selected, evidence-ready candidates to `proof-driven-bug-hunter`.
- Use `bootloader-security-auditor` only for its specialist triggers; do not include it in routine per-module review.
- If multiple skills are relevant, preserve their boundaries and avoid duplicate findings.

### Codebase-Grounded Evidence Standard

- A source pattern alone is an observation, not a confirmed finding.
- Every retained finding and generated test must trace repository fact -> reachable entry/call/state/data flow -> trigger and preconditions -> expected behavior source -> actual behavior -> impact -> executed verification.
- Cite exact paths, symbols, line ranges, callers, configurations, states, and ownership transitions.
- Do not create impossible inputs, impossible ISR schedules, unreachable state transitions, duplicate root causes, target claims from an inapplicable host, or tests whose only purpose is increasing coverage.
- Use evidence levels: `E0 Observation`, `E1 Codebase trace`, `E2 Executed host/SIL evidence`, and `E3 Target/simulator/HIL evidence`.
- Functional defects normally require at least E2. Target-dependent claims require E3 or remain `Unverified risk`.
- Each skill handoff must carry the complete evidence bundle, and the receiving skill must re-check it against the current repository state.

### Evidence and Handoff Rules

- Every confirmed defect must contain an exact source location, trigger, expected behavior, actual behavior, executed command or target procedure, and captured output.
- If target hardware, proprietary tooling, generated files, or configuration are unavailable, report the item as `Unverified risk`; do not fabricate confirmation.
- Host execution must not be presented as target proof when the claim depends on MCU timing, ABI, alignment, cache, atomicity, MMIO, ISR scheduling, or peripheral behavior.
- Production changes must be minimal, in scope, and traceable to a confirmed finding.
- Store all temporary proof artifacts under repository-root `.temp_files/`.
- Preserve pre-existing working-tree changes.
- Do not commit, clean `.temp_files/`, or expand the audit scope without explicit instruction.

### Standard Invocation Pattern

```text
Use <skill-name>.

SCOPE:
<exact source and header paths>

<review objective>

Review only. Do not modify production code.
```

For proof and fix:

```text
Use proof-driven-bug-hunter.

SCOPE:
<exact source and header paths>

Candidate finding:
<precise, falsifiable claim>

Prove or reject the claim against the actual implementation.
If confirmed, apply the minimal in-scope fix and verify it.
Do not modify files outside the scope.
```

### Instruction Precedence

Apply instructions in this order when they conflict:

1. Explicit user request and confirmed scope
2. This `.github/copilot-instructions.md` file
3. Path-specific files under `.github/instructions/`
4. The selected skill's `SKILL.md` and bundled references
5. Existing repository conventions inferred from code

A skill may make a workflow stricter, but it must not weaken the safety, memory, architecture, language-standard, or tooling constraints in this file.

---


### Minimal and Maintainable Changes

- Implement the smallest complete change that fixes a proven root cause.
- Prefer correcting existing local logic and reusing established project abstractions before adding helpers or layers.
- Do not add frameworks, generalized future-proofing, hidden state, global state, dynamic allocation, tasks, queues, timers, retries, delays, feature flags, or wrappers unless the evidence proves they are necessary.
- Avoid clever expressions, opaque macros, excessive indirection, and broad refactoring. Make ownership, bounds, units, state changes, side effects, and error handling explicit.
- Preserve public APIs, binary layouts, protocols, persistent formats, linker behavior, timing, and previously correct behavior unless the confirmed defect is in that surface.
- Keep diffs focused. Do not combine bug fixes with formatting, renaming, modernization, or cleanup.
- Before applying a non-trivial change, explain why it is the simplest correct approach, its resource/control-flow impact, alternatives rejected, and what is deliberately not changed.
- If fix complexity is disproportionate to the proven impact or requires architectural expansion, stop and report the tradeoff rather than implementing automatically.
- After changing code, report a short maintainability check and confirm that no unnecessary abstraction, state, dependency, or execution path was introduced.

## AI Agent Rules

- **Never invent hardware details** — do not guess register addresses, timing, or clock rates.
- If requirements are unclear, leave `TODO` comments and ask for clarification instead of assuming.
- Generated code must be **deterministic and testable**.
- Prefer clarity over cleverness — readability is the first step of maintainability.

### Device Functional Checklist Rules

- Device-level tests must be derived from reachable firmware flows and actual product variants.
- Organize preconditions as `GP-xxx`, `FGP-<GROUP>-xxx`, and `TP-<TEST-ID>-xx`; reference IDs from test cases instead of duplicating text.
- Order preconditions as safety and applicability, firmware/configuration, equipment, feature-group setup, then test-specific state.
- Each checklist item must contain numbered actions, aligned numbered expected results, explicit acceptance criteria, result fields, evidence fields, and post-test state.
- Keep source paths, functions, call flows, guards, requirements, and evidence levels in a separate technical traceability appendix.
- Do not generate tests for absent product features, unreachable states, impossible inputs, unapproved fault injection, or internal behavior with no device-observable result.
- Use `ceedling-test-generator` for host/component tests and `device-functional-test-designer` for tests executed on the physical device.
