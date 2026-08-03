# CLAUDE.md — troika-smart-breaker-modem

Smart breaker modem firmware for the **STM32U375VETx** MCU
(ARM **Cortex-M33**, TrustZone Non-Secure, system clock 96 MHz).
The application runs on a **Contiki process kernel** and speaks
Modbus RTU, IEC 60870-5-104, SCP, GSM, RF, and a power-board bus;
it also drives a battery BMS and serves an on-chip HTTP server.

---

## Coding standards (MANDATORY — applied every session)

The project's full coding standards live under `.github` and are
imported verbatim below. They govern **all** C/C++ you generate or modify:

@.github/copilot-instructions.md
@.github/instructions/barr-c.instructions.md
@.github/instructions/cpp.instructions.md

The most load-bearing rules, restated so they are never missed:

- **No heap, ever** — no `malloc/calloc/realloc/free`, no `new/delete`,
  no heap-based STL containers (`std::vector`, `std::string`, `std::map`).
  Stack / static / fixed-size pools only.
- **C11 / Embedded C++20**, warning-free under
  `-Wall -Wextra -Werror -Wshadow -Wconversion -Wdouble-promotion`.
- **Fixed-width types** (`uint8_t`..`int32_t`); `size_t` for sizes/indices.
  **`errno` is forbidden** — use the project `status_t` enum.
- **MISRA essentials** — every `switch` has a `default`; every
  `if ... else if` ends with an `else`; no VLA, no recursion, no back-jumping `goto`.
- **BARR-C:2018 style** — Allman braces, 4 spaces, 80 columns,
  `g_/p_/s_/b_` prefixes, Yoda conditions, `for (;;)` for infinite loops,
  `/*** end of file ***/` trailer.
- **ASCII only** in code and comments — no Turkish characters
  (per copilot-instructions §Language & Locale).

---

## Toolchain & build (overrides the standards' CMake guidance)

This is an **STM32CubeIDE** project (`.cproject`, `.project`, `.ioc`,
`.launch`), **not** CMake. The CMake rules in the imported standards are
aspirational; in this repo they apply only to host-side test harnesses
(the Ceedling/Unity `test/` Makefiles).

- **MCU:** STM32U375VETx, Cortex-M33 (Non-Secure), SYSCLK 96 MHz.
- **Linker scripts are build-config specific — keep VTOR consistent:**

  | Config  | Linker script                | FLASH origin   | Use                         |
  |---------|------------------------------|----------------|-----------------------------|
  | Debug   | `STM32U375VETX_FLASH.ld`     | `0x08000000`   | Standalone flash debug      |
  | Release | `STM32U375VETX_BOOT.ld`      | `0x08014000`   | Bootloader payload (80 KB BSL) |
  | —       | `STM32U375VETX_RAM.ld`       | RAM            | Debug-in-RAM only           |

  Changing the application base address means updating both the active
  linker script **and** the VTOR offset, or the bootloader handoff faults.

---

## Architecture

```
Core/            CubeMX-generated HAL + startup (USART1/3/4/5, LPUART1,
                 I2C3, SPI2, TIM1/3, ADC1, RTC, GPDMA1)
Drivers/         STM32 HAL/LL + CMSIS  (vendor — do not hand-edit)
contiki-kernel/  Contiki process scheduler + libs (protothread concurrency)
Application/     Product code:
  bsp/           board support, LED, I2C-slave glue
  bms/           battery BMS reader process
  gsm/           GSM modem driver
  rf/            RF radio driver
  power_board/   power-board communications
  libscp/        SCP framing / packet codec
  libmodbusrtu/  Modbus RTU master + register map
  libiec104/     IEC 60870-5-104 stack
  libefw/        embedded framework
  cslog/         project logging (CSLOG / xsprintf) — use this, NOT printf
  app_ipc/       inter-process communication
  web-server/    on-chip HTTP server (+ web-page/ assets)
  libs/ test/    shared utilities / unit tests
```

**Layering rule (Application -> Service/Driver -> HAL/HW):** no direct
register access outside `Core/` or BSP. Reach hardware only through the
HAL/LL layer or `__weak` callback hooks.

---

## CubeMX regeneration discipline

`Core/` and the `.ioc` are the source of truth for pin, clock, and
peripheral configuration. CubeMX **regenerates and overwrites** the
generated regions. Edit **only** between:

```c
/* USER CODE BEGIN xxx */
...
/* USER CODE END xxx */
```

Anything outside those markers is lost on the next code generation.

---

## Contiki port caveat (latent)

This port builds with `_PLATFORM_=_WIN32_`, which stubs
`int-master` / `critical_enter` to **no-ops on ARM**. The application
deliberately does not rely on interrupt-masking for synchronization.
Do **not** introduce code that depends on `critical_enter()` actually
masking IRQs. For ISR-shared state use `volatile` for simple flags and
atomic read-modify-write where RMW races exist (see copilot-instructions
§Interrupt & Concurrency Safety).

---

## Logging

Use the **cslog** module (`CSLOG(...)`, `xsprintf`). No bare
`printf` / `sprintf` in production code. Logging must stay
compile-time removable via preprocessor switches.

---

## Working in this repo

- **Consistency first:** match the surrounding code's existing style where
  it diverges from BARR-C; for **new files**, follow the standards fully,
  including the file-header template (`barr-c.instructions.md` §9.15).
- Keep functions at **<= 100 lines**; refactor when larger.
- When you change a module's logic, run its Ceedling test under that
  module's `test/` directory.
- **Never invent hardware details** — register addresses, timing, or clock
  rates. If unclear, leave a `TODO` and ask.
