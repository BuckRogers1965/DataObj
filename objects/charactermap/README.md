# CharacterMap

Replaces one byte with another, byte by byte, as **Input** flows to **Output**.
The transform can remap or remove bytes, but never add them. Useful for case
folding, swapping delimiters, stripping a character, or converting line ends.

Default input connection is to **Input**.
Default output connection is from **Output**.

## Controls
- **Input** - the text to map. Editing it (or a wired source writing it)
  recomputes Output immediately.
- **Map** - the mapping. One rule per line; recomputes Output when it changes.
- **Output** - the mapped result. An empty Map is identity, so nothing changes.
- **Enable** - checked, the widget operates (the default). Unchecked, Output is
  left as-is until re-enabled.

## Map syntax
One rule per line, tokens separated by a space:

    from to     map every `from` byte to `to`
    from        delete every `from` byte

A token is one of:

| Form        | Meaning                          | Example        |
|-------------|----------------------------------|----------------|
| a lone char | that character                   | `A`            |
| decimal     | a byte value                     | `65`           |
| `0x` / `%` / `\x` hex | a byte value           | `0x41` `%41` `\x41` |
| `\0`-prefixed octal | a byte value             | `\101`         |
| `\`-escape  | `\a \b \f \n \r \s \t \v` (`\s` = space) | `\n`   |

### Examples
Uppercase A to lowercase a (any of these):

    A a
    65 a
    %41 0x61

Delete every `A`:

    A

Swap double and single quotes both ways:

    " '
    ' "

Windows line ends to Unix (drop the carriage return):

    \r

## Notes
Text payloads only: a byte mapped to `0x00` ends the string, so mapping *to* NUL
truncates. Mapping is one-to-one or fewer - it cannot lengthen the stream, so
Unix-to-Windows line ends (which would add a byte) is not possible here.
