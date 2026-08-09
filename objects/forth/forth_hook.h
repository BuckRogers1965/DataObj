#ifndef FORTH_HOOK_H
#define FORTH_HOOK_H

/*
 * Force-included into the vendored atlast.c (see the Makefile), purely to
 * declare the hook its inner loop calls. atlast polls Keybreak() once per
 * word when BREAK is compiled in - which it defines for itself - and the
 * Makefile points Keybreak at Forth_PollBudget. Without a prototype in scope
 * that is an implicit declaration, so it is declared here rather than by
 * editing a public-domain source file we would rather keep pristine.
 */
void Forth_PollBudget(void);

#endif
