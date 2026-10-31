/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM1_INLINE: register helpers that must inline into their caller, so the
 * SFR access sequence (and its timing, in ISRs and IRQ-off sections) is the
 * one written at the call site; -Os would otherwise out-line a helper with
 * several callers. RAM-resident code (.ram_text) may only use these, never
 * a called helper (build.py checks .ram_text for calls). */
#pragma once

#define FM1_INLINE static inline __attribute__((always_inline))
/* FM1_OPAQUE(v): v as the compiler cannot see through it (no code: an empty asm that may change it), so it keeps an
 * expression of v as written (src/fx_eq.c: a 32 x 32-bit product it would otherwise widen to 64 x 64) */
#define FM1_OPAQUE(v) __asm__("" : "+r"(v))
