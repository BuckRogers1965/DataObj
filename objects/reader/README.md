# Reader

A data **source**. It opens the file named by `Filename` and pushes its contents
out the `Out` port one chunk at a time, driven by its own task. At end of file it
sends `msg_eof` out `Out` and stops - the flow drains and the system can quiesce.

Wire `Out` into a sink (a Writer, a Filter, an Out probe). With `Enable` off it
opens nothing.

## Controls

- **Filename** - the file to read.
- **Enable** - gates the reader; any source can drive it.
- **State** - the lifecycle LED (Running while pumping).
- **Out** - the output port; the panel shows the last chunk emitted.
