# Serializer

A **task-driven node-tree walker**. Point `Root` at a node by path and, when
activated, it walks that subtree and emits the **portable state** as JSON chunks
out its `Out` port. Each node is written as
`{"name":..,"type":..,"value":..,"props":[..],"children":[..]}`; the runtime
pointer properties (`local`, `Activate`, `OnMsg`, task handles - all LONG-typed,
per-process addresses) are **skipped**, since a load re-creates each instance
(its `InstanceStart` re-stamps those pointers) and restores only the data.

The walk is its own scheduler task - an explicit STACK of frames, not C
recursion, advanced a batch of steps per tick - so serializing a huge tree never
blocks the fabric, and it drains and quiesces like every other task-driven
object (a final chunk, then `msg_eof` out `Out`).

Wire `Out` into a `Writer` to save state to a file: **Serializer → Writer** is
Save the same way **Reader → Writer** is cat. Placing a Serializer does not walk
anything; it walks only when a flow activates it.

## Controls

- **Root** - the path of the node to walk (default `/Root`).
- **Enable** - gates the walk.
- **State** - the lifecycle LED.
- **Out** - the emit port; the panel shows the last chunk sent.
