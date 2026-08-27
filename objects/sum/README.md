# Sum

Adds up whatever is wired into it.

Connect any number of things to **Input** and press **Sum**. It reads what
each of them holds at that moment and writes the total to **Output**.

Nothing declares how many inputs there are and nothing keeps a list. A wire
records itself on both ends, so this widget's inputs *are* its
subscriptions - pressing Sum walks them and reads.

With nothing wired in the total is 0 - the sum of no inputs - and it says
so as soon as the last wire is cut.

## Options

**Enable**
- *Checked:* Sum does the work. This is the default.
- *Unchecked:* the button is ignored.

**Input**
Where things connect. You can also type into it.

**Output**
The total from the last press. An ordinary property, so wire it onward.
