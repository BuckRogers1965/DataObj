# Base64

Encodes or decodes text using Base64 (RFC 1341). Type or wire text into
**Input** and the result appears in **Output**; tick **Decode** to run the
transform the other way.

Default input connection is to **Input**.
Default output connection is from **Output**.

## Controls
- **Input** - the text to transform. Editing it (or a wired source writing it)
  recomputes Output immediately.
- **Decode** - unchecked, Input is *encoded* to Base64 (the default); checked,
  Input is *decoded* from Base64. On decode, any character outside the Base64
  alphabet (whitespace, line breaks, `=` padding) is ignored, so wrapped or
  padded input decodes cleanly.
- **Output** - the transformed text. Reflects the result; wire it downstream to
  pipe encoded/decoded text on to the next object.
- **Enable** - checked, the widget transforms (the default). Unchecked, Output
  is left as-is until re-enabled.

## Notes
Text payloads only, like the rest of the fabric: a decoded byte of `0x00` ends
the string, so this decodes Base64 of text, not of arbitrary binary.
