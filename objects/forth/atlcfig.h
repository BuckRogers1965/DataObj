#ifndef ATLCFIG_H
#define ATLCFIG_H

/*
 * Atlast subpackage selection, included by the vendored atlast.c because the
 * Makefile builds it -DCUSTOM. This is atlast's own extension point, so the
 * public-domain source stays byte-identical to the release.
 *
 * The list below is atlast's default set with ONE omission: WORDSUSED.
 *
 * WORDSUSED logs which words have been used, and it records that by writing a
 * flag bit into the first byte of the word's own name:
 *
 *     *(dw->wname) |= WORDUSED;            (atlast.c, lookup())
 *
 * A primitive's name is a string literal, so in 1990 that was a write to
 * writable static data and today it is a write to .rodata - it segfaults on
 * the first lookup inside atl_init(), before any script exists. The feature is
 * a diagnostic nobody here asks for, so it is simply left out.
 *
 * Not turned off with NOMEMCHECK, which would have been the one-flag fix:
 * that also compiles out Sl()/So(), the stack underflow and overflow checks
 * (atldef.h). Those are what turn a bad script into an error instead of a
 * corrupted heap, and this interpreter runs whatever a user types.
 */

#define INDIVIDUALLY		/* pick subpackages explicitly, below */

#define ARRAY				/* array subscripting words */
#define BREAK				/* asynchronous break - the runaway guard */
#define COMPILERW			/* compiler-writing words */
#define CONIO				/* interactive console I/O */
#define DEFFIELDS			/* definition field access */
#define DOUBLE				/* double word primitives (2DUP) */
#define EVALUATE			/* the EVALUATE primitive */
#define FILEIO				/* file I/O - parity with Lua's io library */
#define MATH				/* math functions */
#define MEMMESSAGE			/* print a message on stack/heap errors */
#define PROLOGUE			/* prologue processing and auto-init */
#define REAL				/* floating point */
#define SHORTCUTA			/* shortcut integer arithmetic */
#define SHORTCUTC			/* shortcut integer comparison */
#define STRING				/* string functions */
#define SYSTEM				/* the SYSTEM word - parity with Lua's os */
#define TRACE				/* execution tracing */
#define WALKBACK			/* walkback trace on error */

/* deliberately NOT defined: WORDSUSED - see above */

#endif
