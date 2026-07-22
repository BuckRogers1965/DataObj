# Stop Watch

The Stop Watch measures and reports the amount of time between specified
events.

**Default input connection** is to **Run**.
**Default output connection** is from **Duration**.

Wire a Pulse Generator's **Out** to **Run** to time its pulses: with Run
Edge = Positive and Stop Edge = Run Ends, Duration reads the high width of
each pulse. Add a second Stop Watch fed through an inverting Logic Gate to
read the low width — the on and off duty cycles at once.

## Options

**Enable**
- *Checked:* enables timing operations. This is the default.
- *Unchecked:* no operations occur, even if Run is pressed.

**Run**
Starts and stops timing operations.

### Run Edge

Specifies how to start measuring time:
- **Positive:** timing starts the instant Run goes to 1. If Run Ends is the
  Stop Edge, timing stops the instant Run goes to 0.
- **Negative:** timing starts the instant Run goes to 0. If Run Ends is the
  Stop Edge, timing stops the instant Run goes to 1.

**Stop**
Stops (and starts) timing operations.

### Stop Edge

Specifies how to stop measuring time:
- **Run Ends:** the Stop line is irrelevant; timing is controlled entirely
  by the Run line.
- **Positive:** timing stops the instant Stop goes to 1.
- **Negative:** timing stops the instant Stop goes to 0.

**On**
Lights up while a timing operation is occurring.

**Off**
Lights up while a timing operation is not occurring.

### Time Scale

Determines the format for reporting the length of the timing operation:
- **msecs:** Duration is reported in milliseconds.
- **secs:** Duration is reported in seconds.

**Duration**
The length of time the Stop Watch was active, in the format specified by
Time Scale.
