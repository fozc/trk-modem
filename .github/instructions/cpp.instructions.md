---
description: "Use when writing or reviewing C++ source/header files. Covers Embedded C++20 subset, no heap allocation, no RTTI, no exceptions, and safe C++ patterns for firmware."
applyTo: "**/*.{cpp,hpp,cxx,hxx}"
---

# Embedded C++20 Rules

## Compiler Configuration

```
-std=c++20 -pedantic -Wall -Wextra -Werror -Wshadow -Wconversion -Wdouble-promotion -Wformat=2
-fno-exceptions -fno-rtti -fno-threadsafe-statics
```

**Note on `-fno-threadsafe-statics`:** Disables C++ local static thread-safe initialization (Meyers' singleton guard). Safe only in single-threaded contexts or when all static locals are initialized before the RTOS scheduler starts.

## Allowed C++ Features

| Feature | Status | Notes |
|---------|--------|-------|
| `constexpr` | **Use** | Compile-time computation preferred |
| `static_assert` | **Use** | Validate assumptions at compile time |
| Templates | **Use carefully** | Bounded instantiation, no deep recursion |
| `enum class` | **Use** | Type-safe enumerations |
| `std::array` | **Use** | Fixed-size, stack-allocated, no overhead |
| `std::optional` | **Use** | Stack-allocated nullable value (no heap). Use `has_value()` + `operator*` — never `.value()` (calls `std::terminate` with `-fno-exceptions`) |
| `std::string_view` | **Use** | Non-owning string reference. **Warning**: Not guaranteed to be null-terminated. Do not pass `.data()` directly to C-APIs expecting a null-terminated string. |
| Lambdas | **Use** | Non-capturing or with known capture set |
| References | **Use** | Prefer over raw pointers for non-nullable params |
| Placement `new` | **Use** | Only in pre-allocated memory pools. Note: May require overriding global inline `operator new` if `-nostdlib` is used without `<new>` header. |
| Namespaces | **Use** | Module isolation |
| `auto` | **Limited** | Only when type is obvious from initializer |
| `inline` variables | **Use** | For header-defined constants |
| Structured bindings | **Use** | When destructuring simple aggregates |
| `if constexpr` | **Use** | Compile-time branching — prefer Concepts for template constraints |
| `alignas` / `alignof` | **Use** | DMA buffers, peripheral registers, struct layout |
| `[[nodiscard]]` | **Use** | On `status_t` return types — prevents ignored errors |
| `[[maybe_unused]]` | **Use** | ISR parameters, debug-only variables |
| `[[fallthrough]]` | **Use** | Explicit switch-case fallthrough (MISRA compliance) |
| `noexcept` | **Use** | Document no-throw intent, aids compiler optimization |
| `consteval` | **Use** | Immediate functions — guaranteed compile-time evaluation |
| `std::span` | **Use** | Non-owning view of contiguous data — replaces raw pointer + size pairs |
| Concepts | **Use** | Template constraints — cleaner and more readable than SFINAE or `static_assert` |
| `std::variant` | **Use carefully** | Stack-allocated type-safe tagged union — useful for state machines. Avoid complex `std::visit` visitors (code bloat) |
| Designated initializers | **Use** | Aggregate initialization with named fields (standardized in C++20) |
| `[[likely]]` / `[[unlikely]]` | **Use** | Branch prediction hints for hot paths |

## Forbidden C++ Features

| Feature | Reason |
|---------|--------|
| `new` / `delete` | Heap allocation forbidden |
| `std::vector`, `std::string`, `std::map` | Heap-based STL containers |
| `std::shared_ptr`, `std::unique_ptr` | Heap smart pointers |
| `std::function` | May allocate heap for large captures |
| Exceptions (`throw`, `try/catch`) | Non-deterministic, code bloat |
| RTTI (`dynamic_cast`, `typeid`) | Runtime overhead |
| Virtual functions | **Avoid unless justified** — prefer CRTP for compile-time polymorphism. Cost: vptr (4 bytes/object in RAM) + vtable (in Flash) per class |
| `std::iostream` | Code size explosion |
| Multiple inheritance | Complexity, diamond problem |

## Class Pattern

```cpp
namespace scp {

class PacketEncoder final {
public:
    explicit PacketEncoder(uint8_t device_address) noexcept;
    ~PacketEncoder() = default;

    [[nodiscard]] status_t encode(const scp_packet_t& packet,
                                  std::array<uint8_t, SCP_FRAME_MAX_SIZE>& buffer,
                                  size_t& out_length) const noexcept;

    /* Delete copy and move — non-copyable, non-movable */
    PacketEncoder(const PacketEncoder&) = delete;
    PacketEncoder& operator=(const PacketEncoder&) = delete;
    PacketEncoder(PacketEncoder&&) = delete;
    PacketEncoder& operator=(PacketEncoder&&) = delete;

private:
    uint8_t m_device_address;
};

}  /* namespace scp */
```

## Key Rules

- No C-style casts — use `static_cast<>`, `reinterpret_cast<>` with documented rationale.
- Prefer `const` and `constexpr` everywhere possible.
- Mark classes `final` unless designed for inheritance.
- Use `= delete` to prevent unwanted implicit operations.
- Member variables use `m_` prefix: `m_packet_count`.
- Use `extern "C"` blocks for C-compatible interfaces (ISR handlers, HAL callbacks).
- Keep translation unit sizes small — one class per file.
- **ISR Integration:** For hardware interrupt integration with class methods, expose C-compatible wrapper functions via `extern "C"` blocks. Use static or singleton pointers to route the ISR call to the correct class instance method. Ensure shared primitive types use `std::atomic` instead of just `volatile` for correct thread-safe/ISR-safe read-modify-write semantics.

## CRTP — Zero-Cost Polymorphism

```cpp
/* Use instead of virtual functions when polymorphism is compile-time resolvable */
template <typename Derived>
class DriverBase {
public:
    status_t init() noexcept {
        return static_cast<Derived*>(this)->do_init();
    }
};

class UartDriver final : public DriverBase<UartDriver> {
    friend class DriverBase<UartDriver>;
    status_t do_init() noexcept { /* ... */ return STATUS_OK; }
};
```

## Alignment

```cpp
/* DMA buffers must be aligned to cache line or DMA requirement */
alignas(32) static std::array<uint8_t, 256> s_dma_buffer{};

/* Verify alignment at compile time */
static_assert(alignof(decltype(s_dma_buffer)) >= 32U, "DMA buffer alignment");
```

## Template Constraints

```cpp
/* Use static_assert to bound template parameters */
template <size_t BufferSize>
class RingBuffer final {
    static_assert(BufferSize > 0U, "BufferSize must be > 0");
    static_assert((BufferSize & (BufferSize - 1U)) == 0U,
                  "BufferSize must be a power of 2");

    std::array<uint8_t, BufferSize> m_data{};
    /* ... */
};
```


