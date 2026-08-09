# Forth

A Forth language host for ScriptBox and anything else that owns a script.
Like the other hosts it is opaque — no properties, no name, no path — and its
whole interface is `src/script.h`.

## One shared dictionary, on purpose

Lua and JSScript each get a private interpreter per instance. Forth does not,
because in Forth the dictionary *is* the system: words defined in one script
are available to the next, so a ScriptBox can be a vocabulary that other
ScriptBoxes use.

What follows from that:

- The verb table is bound once per process, not per instance.
- `CurHost` is set around every eval so a running word knows whose owner
  `sibget`/`sibset` resolve against — saved and restored, the same discipline
  `MsgFromNode` uses, and sufficient for the same reason.
- Each instance marks the dictionary when its source is set and unwinds to
  that mark before recompiling, so re-running does not append a second copy
  of its own definitions.

## The two host-specific words

- `"NAME" oninput` — names the word to run when data arrives on the owner's
  In. Which word handles input is lifecycle, not a verb. The arriving value is
  pushed and the word executed directly, so a value containing a quote cannot
  break out into code.
- Verbs from `script.object` appear under their own names, taking and
  returning strings: `"/Root/MyBox/Output" "hello" sibset`.

Strings are atlast's C-like literals — plain `"text"`, not ANS Forth's `s"`,
which does not exist here. Word names are stored upper case and lookups are
upper-cased on the way in, so any case works in source.

Callback-taking verbs (`connect`) are not offered here — a Forth word is not
a value that can be handed across the binding and called back. Naming a
handler word to `0ONINPUT` is how a script receives data instead.

## Runaway guard

Atlast's inner loop already polls `Keybreak()` once per word when built with
`-DBREAK`. The Makefile points that at `Forth_PollBudget`, which asks
`ScriptOverBudget` and calls `atl_break()`. The vendored source needed no
modification.

## Attribution

`atlast/` is **Atlast 1.2** — *Autodesk Threaded Language Application System
Toolkit* — designed and implemented by **John Walker** in January 1990.
Developed at Autodesk, Inc.; Autodesk returned the rights to the author in
1991, and he placed it in the public domain. See `atlast/COPYING`.

Home page: <https://www.fourmilab.ch/atlast/>

Vendored files: `atlast.c`, `atlast.h`, `atldef.h`, `COPYING`. The standalone
REPL (`atlmain.c`) and the documentation set are not included. The source is
unmodified.
