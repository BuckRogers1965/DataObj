# Writer

A data **sink**. It subscribes to a source through its `In` port, buffers the
chunks that arrive, and drains them to the file named by `Filename` via a task.
When it has received `msg_eof` and drained its buffer it closes the file and
stops.

Wire a source's output (a Reader, a Filter) into `In`. With `Enable` off it
opens nothing.

## Controls

- **Filename** - the file to write.
- **Enable** - gates the writer; any source can drive it.
- **State** - the lifecycle LED (Running once open).
- **In** - the input port; the panel shows the last chunk received.
