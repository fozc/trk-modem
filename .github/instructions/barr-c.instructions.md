---
description: "BARR-C:2018 Embedded C Coding Standard — comprehensive style, safety, and defect-prevention rules for all C source and header files. Complements MISRA C:2012 and the project c.instructions.md."
applyTo: "**/*.{c,h}"
---

# BARR-C:2018 — Embedded C Coding Standard Rules

> This file captures the full set of enforceable rules from the Barr Group Embedded C
> Coding Standard (2018 edition). Every rule is tagged with its original section number
> for traceability.

---

## 1  General Rules

### 1.1  Language Baseline

- All code shall comply with **C11** (ISO/IEC 9899:2011).
- When a C++ compiler is used, compiler options shall restrict the language to the
  selected version of ISO C.
- Proprietary keyword extensions, `#pragma`, and inline assembly shall be kept to the
  minimum necessary and localized to device driver / BSP modules.
- `#define` shall **never** be used to alter or rename any C keyword or language construct.

```c
/* FORBIDDEN */
#define begin  {
#define end    }
```

### 1.2  Line Width

- Maximum **80 characters** per line (all source files).
- Lines exceeding this limit shall be wrapped at a logical break point and the
  continuation indented for readability.

### 1.3  Braces

- Braces `{ }` shall **always** surround the body of `if`, `else`, `switch`, `while`,
  `do`, and `for` — even for single or empty statements.
- The opening brace `{` shall appear **on its own line**, directly below the controlling
  statement, at the same indentation level.
- The closing brace `}` shall appear on its own line, aligned with the opening brace.

```c
if (depth_in_ft > MAX_DEPTH)
{
    dive_stage = DIVE_DEEP;
}
else
{
    dive_stage = DIVE_SURFACE;
}
```

### 1.4  Parentheses

- Do **not** rely on C operator precedence — use explicit parentheses to make
  evaluation order obvious.
- Each operand of `&&` and `||` shall be individually parenthesized, unless it is a
  single identifier or constant.

```c
if ((depth_in_cm > 0) && (depth_in_cm < MAX_DEPTH))
{
    depth_in_ft = convert_depth_to_ft(depth_in_cm);
}
```

### 1.5  Abbreviations

- Avoid abbreviations and acronyms unless their meanings are widely understood in
  the engineering community.
- A project-level **abbreviation table** shall be maintained under version control (see
  Appendix A of the standard for accepted abbreviations).
- Accepted common abbreviations include: `adc`, `avg`, `buf`, `cfg`, `curr`, `dac`,
  `ee`, `err`, `gpio`, `init`, `io`, `isr`, `lcd`, `led`, `max`, `mbox`, `mgr`, `min`,
  `msec`, `msg`, `next`, `nsec`, `num`, `prev`, `prio`, `pwm`, `q`, `reg`, `rx`, `sem`,
  `str`, `sync`, `temp`, `tmp`, `tx`, `usec`.

### 1.6  Casts

- Every cast shall have an **associated comment** explaining how correctness is
  ensured across the full range of values.

```c
uint16_t sample = adc_read(ADC_CHANNEL_1);
result = abs((int) sample);  /* Safe: 32-bit int holds full uint16_t range. */
```

### 1.7  Keywords to Avoid

| Keyword    | Rule |
|------------|------|
| `auto`     | **Forbidden** — unnecessary historical keyword. |
| `register` | **Forbidden** — compiler knows better than the programmer. |
| `goto`     | **Avoid**. If used, it shall only jump **forward** to a label in the same or enclosing block. |
| `continue` | **Avoid**. Prefer restructuring the loop body. |

### 1.8  Keywords to Use Frequently

- **`static`**: Declare all module-internal functions and file-scope variables `static`
  to minimize coupling.
- **`const`**: Use aggressively on variables, parameters, pointer targets, return types,
  and struct fields where data is read-only.
- **`volatile`**: Use on:
  - Global variables shared with ISRs or other threads.
  - Pointers to memory-mapped I/O registers.
  - Delay loop counters (if delay loops are absolutely necessary).

```c
timer_reg_t volatile * const p_timer = (timer_reg_t *) HW_TIMER_ADDR;
```

---

## 2  Comment Rules

### 2.1  Acceptable Formats

- Both `/* ... */` (C style) and `//` (C++ style) comments are acceptable.
- Comments shall **never** contain the tokens `/*`, `//`, or `\` inside their body
  (avoids accidental nesting).
- Code shall **never** be commented out. Use `#if 0 ... #endif` for temporary
  disabling. Use `#ifndef NDEBUG ... #endif` for debug-only code.

```c
/* FORBIDDEN — commented-out code */
// safety_checker();

/* CORRECT — preprocessor disabling */
#if 0
safety_checker();
#endif
```

### 2.2  Locations and Content

- All comments shall be complete sentences with proper spelling, grammar, and
  punctuation.
- Comments shall precede the block of code they describe, at the same indentation
  level, with a blank line after the block.
- **Do not** explain the obvious — assume the reader knows C. End-of-line comments
  are only for lines whose intent is not clear from names and operations alone.
- Comment length shall be proportional to code complexity.
- External references (specs, patents, datasheets) shall be cited when the algorithm
  originates there.
- All assumptions shall be spelled out in comments.
- Every module and every public function shall have Doxygen-style documentation.
- Use the following **capitalized markers** consistently:

| Marker      | Usage |
|-------------|-------|
| `WARNING:`  | Risk if this code is changed (e.g., empirically-tuned value). |
| `NOTE:`     | Explains **why** (not how) — e.g., errata workaround. |
| `TODO:`     | Area still under construction, with a description of remaining work. Optionally prefix with author initials. |

```c
/* Step 1: Batten down the hatches. */
for (int32_t hatch = 0; hatch < NUM_HATCHES; hatch++)
{
    if (hatch_is_open(hatches[hatch]))
    {
        hatch_close(hatches[hatch]);
    }
}

/* Step 2: Raise the mizzenmast. */
/* TODO: Define mizzenmast driver API. */
```

---

## 3  White Space Rules

### 3.1  Spaces

| Context | Rule |
|---------|------|
| Keywords (`if`, `while`, `for`, `switch`, `return`) | One space after keyword when followed by text on the same line. |
| Assignment operators (`=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `\|=`, `^=`, `~=`, `!=`) | One space before **and** after. |
| Binary operators (`+`, `-`, `*`, `/`, `%`, `<`, `<=`, `>`, `>=`, `==`, `!=`, `<<`, `>>`, `&`, `\|`, `^`, `&&`, `\|\|`) | One space before **and** after. |
| Unary operators (`+`, `-`, `++`, `--`, `!`, `~`) | **No** space on the operand side. |
| Pointer operators (`*`, `&`) in declarations | Space on **both** sides. Otherwise, no space on operand side. |
| Ternary (`? :`) | One space before and after each of `?` and `:`. |
| Structure/member operators (`->`, `.`) | **No** surrounding spaces. |
| Array subscript (`[]`) | **No** surrounding spaces. |
| Parentheses (expression grouping) | **No** spaces adjacent to inner `(` or `)`. |
| Function declaration | One space between function name and `(` in the **declaration only**. |
| Function call | **No** space between function name and `(`. |
| Comma in parameter lists | One space **after** each comma. |
| Semicolons in `for` statements | One space **after** each semicolon. |
| Semicolons (statement terminators) | **No** preceding space. |

### 3.2  Alignment

- Variable names in a series of declarations shall be **vertically aligned**.
- `struct` / `union` member names shall be **vertically aligned**.
- Assignment operators (`=`) in adjacent assignment statements shall be aligned.
- `#` in preprocessor directives is always at column 1; nested directives indent the
  directive keyword.

```c
#ifdef USE_UNICODE_STRINGS
#   define BUFFER_BYTES     128
#else
#   define BUFFER_BYTES     64
#endif

typedef struct
{
    uint8_t   buffer[BUFFER_BYTES];
    uint8_t   checksum;
} string_t;
```

### 3.3  Blank Lines

- **One statement per line** — no multiple statements on a single line.
- Insert a blank line **before and after** each natural block (loops, conditionals,
  switch statements, consecutive declarations).
- Every source file shall end with a comment `/*** end of file ***/` followed by
  one blank line.

### 3.4  Indentation

- Each indentation level is **4 spaces**.
- `case` labels in a `switch` are indented one level from `switch`; case body is
  indented one level from `case`.
- Long lines shall be wrapped and continuation lines indented for maximum
  readability.

```c
sys_error_handler(int32_t err)
{
    switch (err)
    {
        case ERR_THE_FIRST:
            /* ... */
        break;

        default:
            /* ... */
        break;
    }
}
```

### 3.5  Tabs

- The **tab character** (ASCII `0x09`) shall **never** appear in any source file.
- Configure editors to insert spaces when the Tab key is pressed.
- Use `'\t'` inside string literals when a tab is needed at runtime.

### 3.6  Non-Printing Characters

- Line endings shall be **LF** (ASCII `0x0A`) only — not CR-LF.
- The only other permitted non-printing character is **FF** (form feed, `0x0C`).

---

## 4  Module Rules

### 4.1  Module Naming

- File names shall consist entirely of **lowercase letters, numbers, and underscores**.
  No spaces.
- File names shall be unique within the first **8 characters**.
- File names shall **not** collide with C or C++ Standard Library header names
  (`stdio`, `math`, `string`, etc.).
- Any file containing `main()` shall have `"main"` in its file name.

### 4.2  Header Files

- Each `.c` file shall have exactly **one** corresponding `.h` file with the same root
  name.
- Every header file shall have an **include guard** (not `#pragma once`):

```c
#ifndef MODULE_NAME_H
#define MODULE_NAME_H

#ifdef __cplusplus
extern "C" {
#endif

/* Public API only */

#ifdef __cplusplus
}
#endif

#endif /* MODULE_NAME_H */
```

- Headers shall expose **only** the public interface (prototypes, macros, typedefs).
- Variables shall **not** be declared `extern` in headers (preferred practice).
- No storage shall be allocated in a header file.
- Public headers shall **not** `#include` private headers.

### 4.3  Source Files

- Each source file shall encapsulate **one entity** (data type, peripheral driver,
  protocol layer, etc.).
- Source file section order:
  1. File header comment block
  2. `#include` statements
  3. Data type, constant, and macro definitions
  4. Static data declarations
  5. Private function prototypes
  6. Public function bodies
  7. Private function bodies
- Every `.c` file shall `#include` its own `.h` header first, so the compiler can verify
  prototype / definition consistency.
- `#include` paths shall be **relative** — no absolute paths.
- Remove unused `#include` directives.
- Never `#include` a `.c` file from another `.c` file.

### 4.4  File Templates

- Header and source file templates shall be maintained at the project level.
- Every new file shall start from the appropriate template to ensure consistent
  header comments and copyright notices.

**Header template:**

```c
/** @file module.h
 *
 *  @brief A description of the module's purpose.
 *
 *  COPYRIGHT NOTICE: (c) 2025 Company. All rights reserved.
 */

#ifndef MODULE_H
#define MODULE_H

/* Public API declarations */

#endif /* MODULE_H */

/*** end of file ***/
```

**Source template:**

```c
/** @file module.c
 *
 *  @brief A description of the module's purpose.
 *
 *  COPYRIGHT NOTICE: (c) 2025 Company. All rights reserved.
 */

#include "module.h"

/*** end of file ***/
```

---

## 5  Data Type Rules

### 5.1  Type Naming

- All new type names (structs, unions, enums) shall be **lowercase with underscores**
  and end with `_t`.
- All structs, unions, and enums shall be wrapped in a `typedef`.
- Public type names shall be prefixed with their module name: `module_type_t`.

```c
typedef struct
{
    uint16_t  count;
    uint16_t  max_count;
    uint16_t  _unused;
    uint16_t  control;
} timer_reg_t;
```

### 5.2  Fixed-Width Integers

- Use `<stdint.h>` fixed-width types whenever the bit width matters:
  `uint8_t`, `int8_t`, `uint16_t`, `int16_t`, `uint32_t`, `int32_t`, `uint64_t`, `int64_t`.
- Keywords `short` and `long` are **forbidden**.
- `char` shall only be used for string declarations and operations.

### 5.3  Signed vs. Unsigned Integers

- Bit-fields shall **not** be defined within signed integer types.
- Bitwise operators (`&`, `|`, `~`, `^`, `<<`, `>>`) shall **not** be applied to signed
  integers.
- **Never mix** signed and unsigned integers in comparisons or expressions.
- Unsigned decimal constants shall have a `u` or `U` suffix: `6U`.

```c
uint16_t unsigned_a = 6U;
int16_t  signed_b   = -9;

/* FORBIDDEN — signed/unsigned mix */
if (unsigned_a + signed_b < 4)
{
    /* ... */
}
```

### 5.4  Floating Point

- **Avoid** floating point when fixed-point math is a viable alternative.
- When floating point is necessary:
  - Use C99 type names: `float32_t`, `float64_t`, `float128_t`.
  - Append `f` to single-precision constants: `3.141592f`.
  - Verify double-precision compiler support with a compile-time check.
  - **Never** test floating-point values for equality or inequality.
  - Always call `isfinite()` after calculations to guard against `INFINITY` / `NAN`.

```c
#include <limits.h>
#if (DBL_DIG < 10)
#   error "Double precision is not available!"
#endif
```

### 5.5  Structures and Unions

- Prevent compiler-inserted **padding** in structs used for hardware register overlays,
  network protocols, or inter-processor communication. Use `__attribute__((packed))`
  with documented rationale.
- Prevent compiler reordering of **bit-field** bits — verify layout with compile-time
  assertions.
- Always validate struct size at compile time:

```c
_Static_assert(8U == sizeof(timer_reg_t), "timer_reg_t size mismatch");
```

### 5.6  Booleans

- Boolean variables shall be declared as `bool` (from `<stdbool.h>`).
- Non-Boolean values shall be converted to `bool` via relational operators, **not** via
  casts.

```c
#include <stdbool.h>

bool b_in_motion = (0 != speed_in_mph);
```

---

## 6  Procedure Rules

### 6.1  Procedure Naming

- Procedure names shall **not** collide with any C/C++ keyword (`interrupt`, `inline`,
  `class`, `true`, `false`, `public`, `private`, `protected`, `friend`, etc.).
- Procedure names shall **not** collide with C Standard Library functions (`strlen`,
  `atoi`, `memset`, etc.).
- Procedure names shall **not** begin with an underscore.
- Maximum **31 characters** per name.
- Function names: **all lowercase**, words separated by underscores.
- Macro names: **ALL UPPERCASE**, words separated by underscores.
- Names shall be descriptive of purpose. Use **noun-verb** ordering:
  `adc_read()`, `led_is_on()`.
- Public functions shall be prefixed with their module name: `sensor_read()`.

### 6.2  Functions

- Maximum **100 lines** per function (excluding braces and blank lines).
- Prefer functions that fit on a single printed page.
- Prefer a **single exit point** via `return` at the bottom. Multiple returns are
  acceptable only when they genuinely improve readability (e.g., guard clauses).
- Every public function shall have a **prototype** declared in the module header.
- All private (module-internal) functions shall be declared `static`.
- Every parameter shall be explicitly typed and meaningfully named.

```c
int32_t
state_change (int32_t event)
{
    int32_t result = ERROR;

    if (EVENT_A == event)
    {
        result = STATE_A;
    }
    else
    {
        result = STATE_B;
    }

    return (result);
}
```

### 6.3  Function-Like Macros

- **Prefer `static inline` functions** over parameterized macros (MISRA Dir 4.9).
- If a macro is unavoidable:
  1. Surround the **entire macro body** with parentheses.
  2. Surround **each parameter use** with parentheses.
  3. Use each parameter **at most once** to avoid side effects.
  4. **Never** include a transfer of control (`return`, `goto`, `break`) in a macro.

```c
/* AVOID this */
#define MAX(A, B)   ((A) > (B) ? (A) : (B))

/* PREFER this */
static inline int32_t max_i32(int32_t num1, int32_t num2)
{
    return ((num1 > num2) ? num1 : num2);
}
```

### 6.4  Threads of Execution

- All functions that encapsulate RTOS tasks / threads / processes shall be named
  with a suffix: `_thread`, `_task`, or `_process`.

```c
void
alarm_thread (void * p_data)
{
    for (;;)
    {
        /* Wait for event and process. */
    }
}
```

### 6.5  Interrupt Service Routines

- ISRs shall be declared using the compiler-specific keyword or `#pragma` (e.g.,
  `__interrupt`, `#pragma irq_entry`). On ARM Cortex-M, standard calling convention
  is sufficient — `__attribute__((interrupt))` is **not** required.
- ISR function names shall end with `_isr` (or match the vector table entry, e.g.,
  `USART1_IRQHandler`).
- ISRs shall be declared `static` and/or located at the end of the driver module to
  prevent inadvertent calls from application code.
- A **default / stub ISR** shall be installed for every unused vector table entry. The
  stub should disable further interrupts of the same type and invoke the assert
  handler.

```c
#pragma irq_entry
static void
timer_isr (void)
{
    static uint8_t prev = 0x00U;
    uint8_t        curr = *gp_button_reg;

    g_debounced |= (prev & curr);
    g_debounced &= (prev | curr);

    prev = curr;
    /* Acknowledge interrupt at hardware level. */
}
```

---

## 7  Variable Rules

### 7.1  Variable Naming

- Variable names shall **not** collide with C/C++ keywords or C Standard Library
  names (e.g., `errno`).
- Variable names shall **not** begin with an underscore.
- Maximum **31 characters**; minimum **3 characters** (including loop counters).
- **All lowercase**, words separated by underscores.
- No embedded magic numbers in variable names (e.g., do not name a variable
  `array16` for a 16-element array).
- Names shall be descriptive of purpose.

#### Prefix Convention

| Prefix     | Meaning | Example |
|------------|---------|---------|
| `g_`       | Global variable | `g_zero_offset` |
| `p_`       | Pointer | `p_led_reg` |
| `pp_`      | Pointer to pointer | `pp_vector_table` |
| `b_`       | Boolean (phrased as question) | `b_is_buffer_full` |
| `h_`       | Handle (non-pointer) | `h_input_file` |

- When multiple prefixes apply, order is: `[g_][p_ | pp_][b_ | h_]`.
  Example: `gp_timer_reg` — global pointer to timer register.

### 7.2  Variable Initialization

- **All** variables shall be initialized before use.
- Prefer declaring local variables close to their first use (C99+), not all at the top
  of the function.
- File-global and project-global variables shall be grouped at the top of the source
  file.
- Any pointer lacking an initial address shall be initialized to `NULL`.

```c
uint32_t g_array[NUM_ROWS][NUM_COLS] = { 0 };

for (int32_t col = 0; col < NUM_COLS; col++)
{
    g_array[row][col] = compute_value(row, col);
}
```

---

## 8  Statement Rules

### 8.1  Variable Declarations

- The comma operator shall **not** be used in variable declarations.
  Declare each variable on its own line.

```c
/* FORBIDDEN */
char * x, y;   /* Was y intended to be a pointer? Ambiguous. */

/* CORRECT */
char * x;
char   y;
```

### 8.2  Conditional Statements

- The **shortest clause** (in lines of code) of `if` / `else if` should be placed first.
- Nested `if...else` shall not exceed **two levels** of depth. Refactor deeper nesting
  into helper functions or `switch` statements.
- **No assignments** within `if` or `else if` test expressions.
- Every `if` statement with an `else if` shall end with an `else` clause (MISRA 15.7).

```c
if (NULL == p_object)
{
    result = ERR_NULL_PTR;
}
else if (count > MAX_COUNT)
{
    result = ERR_OVERFLOW;
}
else
{
    /* Normal processing. */
}
```

### 8.3  Switch Statements

- `break` shall be **aligned with its corresponding `case`**, not with the case body.
- Every `switch` shall have a `default` block (MISRA 16.4).
- Intentional fall-through shall be **explicitly commented**.

```c
switch (err)
{
    case ERR_A:
        handle_err_a();
    break;

    case ERR_B:
        handle_err_b();
        /* Fall through — also perform ERR_C handling. */
    case ERR_C:
        handle_err_c();
    break;

    default:
        handle_unknown_err();
    break;
}
```

### 8.4  Loops

- **No magic numbers** in loop initialization or endpoint tests — use named constants.
- No assignments in loop controlling expressions (except the `for` initializer and
  iterator clauses).
- Infinite loops shall use `for (;;)` — not `while (1)` or `while (true)`.
- Empty loop bodies shall contain braces with a comment explaining why the body
  is empty.

```c
for (int32_t row = 0; row < NUM_ROWS; row++)
{
    for (int32_t col = 0; col < NUM_COLS; col++)
    {
        /* ... */
    }
}
```

### 8.5  Jumps

- `goto` usage restricted per Rule 1.7.c (forward jumps only, same or enclosing
  block).
- C Standard Library functions `abort()`, `exit()`, `setjmp()`, and `longjmp()` are
  **forbidden**.

### 8.6  Equivalence Tests (Yoda Conditions)

- When comparing a variable against a constant, place the **constant on the left**
  side of `==`.
- This catches accidental assignment (`=` instead of `==`) at compile time.

```c
if (NULL == p_object)
{
    return ERR_NULL_PTR;
}

if (STATE_IDLE == current_state)
{
    /* ... */
}
```

---

## 9  Project-Specific Extensions

> The following rules originate from the project's C11 coding guidelines and complement
> the BARR-C:2018 standard with embedded-specific safety patterns.

### 9.1  Compiler Flags

All code shall compile **warning-free** with the following minimum set of flags:

```
-std=c11 -pedantic -Wall -Wextra -Werror -Wshadow -Wconversion -Wdouble-promotion -Wformat=2
```

### 9.2  `size_t` Usage

- Use `size_t` (from `<stddef.h>`) for all sizes and array indices.
- `size_t` is the one exception to the fixed-width integer rule — it is the
  language-defined type for object sizes and is required by Standard Library
  interfaces.

### 9.3  `errno` Prohibition

- `errno` is **forbidden** — it is global mutable state and not reentrant-safe.
- Use the project-wide `status_t` enum for error reporting instead.

### 9.4  `volatile` vs. Atomics

- `volatile` does **not** provide atomicity.
- For simple read/write flags shared with ISRs, `volatile` is sufficient.
- For **read-modify-write** operations across concurrency boundaries, use
  `<stdatomic.h>` (e.g., `atomic_bool`, `atomic_uint32_t`, `atomic_fetch_add()`)
  or hardware-specific exclusive access instructions (`LDREX`/`STREX`).
- Never rely on `volatile` alone for synchronization.

### 9.5  VLA Prohibition

- **Variable Length Arrays** (VLAs) are **forbidden**.
- Always use fixed-size arrays with compile-time constants.
- Use `_Static_assert` to validate array sizing assumptions.

### 9.6  Recursion Prohibition

- **Recursion** is **forbidden** — stack usage must be bounded and deterministic.
- Always use iterative algorithms with bounded loop counts.
- All loops shall have a provably finite upper bound.

### 9.7  Dynamic Memory Prohibition

- The following functions are **forbidden** in all contexts:
  `malloc()`, `calloc()`, `realloc()`, `free()`.
- Use stack allocation (default), static allocation (module-level state), or
  fixed-size memory pools for dynamic-like behavior.
- `alloca()` is also **forbidden** due to stack overflow risk.

### 9.8  Alignment

- Use `_Alignas` / `_Alignof` (C11) for DMA buffers and peripheral data
  structures.
- Verify alignment at compile time with `_Static_assert`.

```c
_Alignas(32) static uint8_t s_dma_buffer[256];
_Static_assert(_Alignof(s_dma_buffer) >= 32U, "DMA buffer alignment");
```

### 9.9  Include Order

Include statements shall follow this **4-layer order**, with a blank line between
each group:

```c
/* 1. Own header (for .c files — enables compiler prototype checking) */
#include "this_module.h"

/* 2. Project headers */
#include "scp_types.h"
#include "scp_crc.h"

/* 3. Platform / HAL headers */
#include "stm32u3xx_hal.h"

/* 4. Standard library headers */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
```

### 9.10  Struct Initialization Patterns

- Use **designated initializers** (C99/C11) — explicit, order-independent, and
  zero-fills omitted members.
- **Zero-initialize** all structs before use with `= {0}`.

```c
/* Designated initializer */
static const scp_packet_t s_default_packet = {
    .address = 0x00U,
    .type    = SCP_CMD_GET,
    .length  = 0U,
    .data    = {0U},
};

/* Zero-initialize before use */
scp_packet_t packet = {0};
```

### 9.11  Bit Manipulation

- Use `UINT32_C()` (or `UINT16_C()`, `UINT8_C()`) to guarantee the width of
  shift operands — prevents undefined behavior when `BIT_POS >= 16` on
  platforms where `int` is 16-bit.
- `BIT_POS` must be less than the bit-width of the target type (MISRA Rule 12.2).
- Bit-test results shall always be assigned to `bool`.

```c
reg |= (UINT32_C(1) << BIT_POS);            /* Set bit   */
reg &= ~(UINT32_C(1) << BIT_POS);           /* Clear bit */

/* Test bit — result must be Boolean */
bool is_set = ((reg & (UINT32_C(1) << BIT_POS)) != 0U);
```

### 9.12  Endianness / Byte-Order

- Always serialize multi-byte fields **explicitly** — never cast a struct pointer to
  a byte pointer for transmission or storage.
- `memcpy` is the only standard-conforming method for type punning in C11.

```c
static inline void uint16_to_be(uint16_t value, uint8_t *p_buf)
{
    p_buf[0] = (uint8_t)(((uint32_t)value >> 8U) & 0xFFU);
    p_buf[1] = (uint8_t)((uint32_t)value & 0xFFU);
}

static inline uint16_t be_to_uint16(const uint8_t *p_buf)
{
    return (uint16_t)(((uint32_t)p_buf[0] << 8U) | (uint32_t)p_buf[1]);
}
```

### 9.13  Array & Buffer Safety

- Always pass **buffer size alongside pointer**:
  `func(uint8_t *p_buf, size_t buf_size)`.
- **Bounds-check** before every indexed access.
- Prefer `memcpy` / `memset` with explicit size over manual loops; validate size
  first.
- Use `sizeof` on the **array**, not on a pointer — `sizeof(ptr)` returns pointer
  size, not array size.
- `memcpy` is also the only standard-conforming method for type punning in
  C11 — avoid pointer casts or union tricks.

### 9.14  Project Assert Macro

- Do **not** use `<assert.h>` — `abort()` is not suitable for embedded targets.
- Define a project-specific assert macro that invokes a fault handler.
- The handler must be declared `_Noreturn` — it shall never return to the caller.
- Use for **internal invariants only** — never for validating external input.

```c
_Noreturn void assert_handler(const char *p_file, int line);

#define PROJECT_ASSERT(expr)                              \
    do {                                                  \
        if (!(expr))                                      \
        {                                                 \
            assert_handler(__FILE__, __LINE__);            \
        }                                                 \
    } while (0)
```

### 9.15  File Header Format

Every new `.c` and `.h` file shall begin with the following comment block.
No deviations in spacing or field names.

```c
/*
 * module_name.c          <- exact filename
 *
 *  Created on: MMM DD, YYYY
 *      Author: Name Surname
 *              email@example.com
 *
 * Brief description of what this module does.
 */
```

- `Created on` date shall reflect the actual creation date.
- The description line states the module's single responsibility.

---

## Appendix: Quick Reference — Naming Summary

| Element | Convention | Example |
|---------|-----------|---------|
| Files | `snake_case.c` / `.h` | `scp_parser.c` |
| Functions | `module_verb_noun`, all lowercase | `scp_encode_packet()` |
| Macros / Constants | `UPPER_SNAKE_CASE` | `SCP_MAX_PAYLOAD_SIZE` |
| Types (struct/enum/typedef) | `snake_case_t` | `scp_packet_t` |
| Enum values | `MODULE_UPPER_SNAKE` | `SCP_CMD_GET` |
| Local variables | `snake_case` | `packet_length` |
| Global variables | `g_` prefix | `g_scp_rx_buffer` |
| Static file-scope variables | `s_` prefix | `s_packet_count` |
| Pointers | `p_` prefix | `p_buffer` |
| Pointer-to-pointer | `pp_` prefix | `pp_vector_table` |
| Booleans | `b_` prefix (phrased as question) | `b_is_valid` |
| Handles | `h_` prefix | `h_input_file` |

---

## Appendix: Accepted Abbreviations

The following abbreviations may be used **without** local explanation:

| Abbrev | Meaning | Abbrev | Meaning |
|--------|---------|--------|---------|
| `adc` | analog-to-digital converter | `msec` | millisecond |
| `avg` | average | `msg` | message |
| `b_` | Boolean prefix | `next` | next (item in list) |
| `buf` | buffer | `nsec` | nanosecond |
| `cfg` | configuration | `num` | number (of) |
| `curr` | current (item in list) | `p_` | pointer (to) |
| `dac` | digital-to-analog converter | `pp_` | pointer to pointer |
| `ee` | EEPROM | `prev` | previous (item in list) |
| `err` | error | `prio` | priority |
| `g_` | global prefix | `pwm` | pulse width modulation |
| `gpio` | general-purpose I/O | `q` | queue |
| `h_` | handle (to) | `reg` | register |
| `init` | initialize | `rx` | receive |
| `io` | input/output | `sem` | semaphore |
| `isr` | interrupt service routine | `str` | string (null terminated) |
| `lcd` | liquid crystal display | `sync` | synchronize |
| `led` | light-emitting diode | `temp` | temperature |
| `max` | maximum | `tmp` | temporary |
| `mbox` | mailbox | `tx` | transmit |
| `mgr` | manager | `usec` | microsecond |
| `min` | minimum | | |

> **Note:** `second`, `minute`, `hour`, `day`, `week`, `month`, and `year` shall
> **never** be abbreviated (avoids conflict between `minute` and `minimum`).

---

## Appendix: Enforcement Summary

| Rule Area | Primary Enforcement | Secondary Enforcement |
|-----------|--------------------|-----------------------|
| Language baseline (1.1) | Compiler flags | Code review |
| Line width (1.2) | Automated scan at build | — |
| Braces (1.3) | Automated tool / beautifier | Code review |
| Parentheses (1.4) | Code review | Static analysis |
| Abbreviations (1.5) | Code review | — |
| Casts (1.6) | Code review | — |
| Forbidden keywords (1.7) | Automated scan at build | Code review |
| `static` / `const` / `volatile` (1.8) | Code review | Static analysis |
| Comments (2.x) | Code review | Doxygen build |
| White space / tabs (3.x) | Code beautifier | Automated scan at build |
| Module naming (4.1) | Automated tool | — |
| Header guards (4.2) | Compiler / static analysis | Code review |
| Source file layout (4.3) | Code review | Static analysis |
| Fixed-width types (5.2) | Automated scan at build | Code review |
| Signed/unsigned mixing (5.3) | Static analysis | Code review |
| Struct layout (5.5) | `_Static_assert` | Code review |
| Function length (6.2) | Automated scan | Code review |
| Macro vs. inline (6.3) | Code review | — |
| Variable naming (7.1) | Code review | — |
| Initialization (7.2) | Static analysis | Code review |
| Comma declarations (8.1) | Code review | — |
| Nesting depth (8.2) | Static analysis | Code review |
| Switch/default (8.3) | Static analysis / compiler | Code review |
| Magic numbers (8.4) | Code review | Static analysis |
| Yoda conditions (8.6) | Compiler warning (`-Wparentheses`) | Code review |

/*** end of file ***/
