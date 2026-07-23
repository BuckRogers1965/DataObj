# RegExp

Matches and rewrites text line by line with a POSIX extended regular
expression. Each line of **Input** is tested against **Expression**, and
**Action** decides what reaches **Output**.

Default input connection is to **Input**.
Default output connection is from **Output**.

## Controls
- **Expression** - the pattern (POSIX ERE: `.` `*` `+` `?` `[...]` `(...)`
  `|` `^` `$` `{m,n}`). Recomputes on change.
- **Substitution** - the replacement for the Replace actions. `$0` is the whole
  match, `$1`-`$9` are capture groups, `$$` is a literal `$`.
- **Action** - what to do with each line:
  - **Match** - keep lines that match (like `grep`).
  - **Reject** - keep lines that do *not* match (like `grep -v`).
  - **Replace** - every line passes through; each match is replaced with the
    substitution (like `sed s///g`).
  - **Replace Line** - a matching line is replaced whole by the substitution;
    non-matching lines pass through.
- **CaseSensitive** - checked (default), the match respects case; unchecked, it
  ignores case.
- **Input** / **Output** - the text in and the result out.
- **Error** - the regex compile error, when Expression is malformed. While an
  error is showing, Output is left as it was.
- **Enable** - checked, the widget operates (the default).

## Notes
An empty Expression passes Input straight through. Matching is per line, so `^`
and `$` anchor to line starts and ends. Text payloads only: a `\0` byte ends the
string.
