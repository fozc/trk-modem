/*
 * ring_buf.h - C11 SPSC byte queue (local integration requirements).
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */
//============================================================================
// Lock-Free Ring Buffer (LFRB) for embedded systems
// GitHub: https://github.com/QuantumLeaps/lock-free-ring-buffer
//
//                    Q u a n t u m  L e a P s
//                    ------------------------
//                    Modern Embedded Software
//
// Copyright (C) 2005 Quantum Leaps, <state-machine.com>.
//
// SPDX-License-Identifier: MIT
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//============================================================================
#ifndef RING_BUF_H
#define RING_BUF_H

#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 201112L)
#error "RingBuf requires C11 atomics; compile as C11 or later"
#elif defined(__STDC_NO_ATOMICS__) && (__STDC_NO_ATOMICS__ != 0)
#error "RingBuf requires <stdatomic.h>; atomics are unavailable"
#else
#if defined(__has_include)
#if !__has_include(<stdatomic.h>)
#error "RingBuf requires the <stdatomic.h> header"
#endif
#endif
#include <stdatomic.h>
#endif
#include <stdint.h>
#include <stdbool.h>

/* SPSC only: one producer calls put; one consumer calls get/process_all.
 * num_free is a consumer-side advisory snapshot, not a reservation.
 * ctor runs with both sides stopped. The instance, storage and callback
 * must be valid; zero-length storage and packed instances are forbidden.
 * Data slots are ordinary memory; release/acquire indices transfer their
 * ownership. Extra IRQ masking or atomic data slots are not required. */
typedef uint16_t RingBufCtr;

#if defined(__GNUC__) || defined(__clang__)
__extension__ _Static_assert(
    __atomic_always_lock_free(sizeof(_Atomic(RingBufCtr)), 0),
    "RingBuf requires lock-free counters (check CPU/toolchain)");
#else
_Static_assert(_Generic((RingBufCtr)0,
    unsigned short: ATOMIC_SHORT_LOCK_FREE,
    unsigned int: ATOMIC_INT_LOCK_FREE,
    default: 0) == 2,
    "RingBuf requires always-lock-free counters");
#endif

//! Ring buffer element type
//
// @details
// The type of the ring buffer elements is not critical for the lock-free
// operation and does not need to be "atomic". For example, it can be
// an integer type (uint16_t, uint32_t, uint64_t), a pointer type,
// or even a struct type. However, the bigger the type the more RAM is
// needed for the ring buffer and more CPU cycles are needed to copy it
// into and out of the buffer memory.
//
typedef uint8_t RingBufElement;

//! Ring buffer struct
typedef struct {
    RingBufElement *buf; //!< pointer to the start of the ring buffer
    RingBufCtr end;      //!< index of the end of the ring buffer

    //! atomic index to where next element will be inserted
    _Atomic(RingBufCtr) head;

    //! atomic index to where next element will be removed
    _Atomic(RingBufCtr) tail;
} RingBuf;

void RingBuf_ctor(RingBuf * const me,
                  RingBufElement sto[], RingBufCtr sto_len);
RingBufCtr RingBuf_num_free(RingBuf * const me);
bool RingBuf_put(RingBuf * const me, RingBufElement const el);
bool RingBuf_get(RingBuf * const me, RingBufElement *pel);

//! Ring buffer callback function for RingBuf_process_all()
//
// @details
// The callback processes one element and runs in the context of
// RingBuf_process_all().
//
typedef void (*RingBufHandler)(RingBufElement const el);

void RingBuf_process_all(RingBuf * const me, RingBufHandler handler);

#endif // RING_BUF_H

/*** end of file ***/
