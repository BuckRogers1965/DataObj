# Logic Gate

The Logic Gate compares one or more input values and returns a result. Any
number of inputs may be used in the comparison. Logic Gates are valuable
for controlling the output of events; Logic Gates, Comparators, and
Counters are often used in combination.

**Default input and output connections** are In and Out.

As a single-input **OR** with **Invert** checked, the Logic Gate is a NOT
gate — feed a Pulse Generator's Out into In and Out becomes the pulse's
inverse. Time that with a second Stop Watch to read the OFF duty cycle
while a first Stop Watch (on the pulse itself) reads the ON.

## Options

**Enable**
- *Checked:* enables Logic Gate operations. This is the default.
- *Unchecked:* no operations occur.

### Mode

Specifies the comparison mode:
- **OR Gate:** returns 1 if any input is 1; returns 0 when all inputs are 0.
  This is the default.
- **AND Gate:** returns 1 only if all inputs are 1; returns 0 otherwise.
- **XOR Gate:** returns 1 when one and only one input is 1; returns 0
  otherwise.
- **Parity Gate:** returns 1 on even-numbered events and 0 on
  odd-numbered events (the first and third events light the output; the
  second and fourth turn it off, and so on).

**Invert**
- *Checked:* toggles the value of the result (1 becomes 0, 0 becomes 1).
- *Unchecked:* leaves the result unchanged. This is the default.

**Changes Only**
- *Checked:* sends the result only when it differs from the previous
  result. This is the default.
- *Unchecked:* sends the result as each input arrives.

**Out**
Returns and displays the result of the comparison logic; its LED lights on
1. All inputs to be compared should be connected to In.

**Auto Interpret**
- *Checked:* automatically recomputes when any connected input changes.
  This is the default.
- *Unchecked:* requires a press of the Interpret button to recompute.

**Interpret**
Initiates a comparison operation now.

---

> **Note on multiple inputs.** VNOS combined every source wired to a single
> Input/Output by walking the port's source list (`wgv->Sources`). This
> framework delivers one value at a time and does not yet expose a port's
> sources, so OR/AND/XOR here operate on the single arriving value
> (identity for one input) and Parity toggles per event — faithful for the
> inverter/buffer use above. True N-input combination waits on the
> source-enumeration primitive (see ROADMAP.md, Phase 8).
