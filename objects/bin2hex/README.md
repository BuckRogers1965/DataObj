# Bin2Hex

Dumps its input as uppercase hexadecimal - two digits per byte, a space
between bytes, sixteen bytes to a line. Type or wire text (or binary) into
**Input** and the hex appears in **Output**.

Default input connection is to **Input**.
Default output connection is from **Output**.

## Controls
- **Input** - the bytes to dump. Editing it (or a wired source writing it)
  recomputes Output immediately.
- **Output** - the hex dump. Reflects the result; wire it downstream to pass
  the hex on to the next object. It is always printable ASCII.
- **Enable** - checked, the widget operates (the default). Unchecked, Output is
  left as-is until re-enabled.

## Use
Bin2Hex is one-way: it always renders bytes *to* hex, never back. It is useful
for **preventing** an automatic hex-to-text translation downstream - feed a
binary stream (e.g. serial or socket bytes) through Bin2Hex and what arrives at
a text sink is the stable hex representation, not bytes that a text control
might reinterpret.

## Notes
Text payloads only, like the rest of the fabric: a byte of `0x00` in the input
ends the string, so a NUL truncates the dump.
