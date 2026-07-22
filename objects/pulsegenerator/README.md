# Pulse Generator

The Pulse Generator sends an event as defined by specified options. Pulse
Generators are often used in conjunction with a Counter and Comparator to
repeat some action a specific number of times or for a specific duration.
For example, you could hook a Pulse Generator to a Counter and record
occurrences of an event. You could then use the Comparator to compare the
Counter total and a number of your choosing; when the Counter exceeded the
number, the Comparator's `A >= B` LED could be activated to effect another
action.

**Default input connection** is to the **Start** button.
**Default output connection** is from the **Out** LED.

## Options

**Enable**
- *Checked:* enables operations. This is the default.
- *Unchecked:* no operations occur, even if Start is pressed or Run when
  Enabled is checked.

**Start**
Starts the Pulse Generator. The state of the Retriggerable checkbox
determines what happens if Start is pressed while an operation is
occurring: checked, the operation restarts from that point; unchecked,
nothing happens.

**Stop**
Halts the Pulse Generator. Nothing happens if Stop is pressed when no
operation is occurring.

**Out**
Lights up when a 1 is being sent and turns off when a 0 is being sent.

**~Out**
The inverse of Out: lights up when a 0 is being sent from Out and turns off
when a 1 is being sent. ~Out sends a 1 when it turns on and a 0 when it
turns off, like all LEDs.

**TimeBase (msecs)**
The duration used when determining how to send the pulse, in milliseconds.
1000 is the default.

**Duty Cycle**
The percent (0% to 100%) of the TimeBase during which the pulse is active.
50 is the default. Accepts only positive whole numbers between 0 and 100;
out-of-range or non-numeric entries are clamped.

**Single Shot**
- *Checked:* the operation completes one cycle and then halts.
- *Unchecked:* runs continuously. Unchecking Single Shot during an
  operation continues running once the current operation completes.

**Active**
Lights up while a cycle is occurring and turns off when the cycle is
complete. If Single Shot is not checked, Active remains lit until Stop is
pressed.

**Retriggerable**
- *Checked:* triggering events during a running operation restart it.
- *Unchecked:* triggering events during a running operation are ignored.

**Run when Enabled**
- *Checked:* operations begin when the containing flow loads, as long as
  Enable is also checked.
- *Unchecked:* operations begin when Start is pressed.
