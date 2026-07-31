# TPLink

Controls a TP-Link "smart plug" style device (HS100/HS110 and similar) over
the local network. It contains a TCP object, in client mode, and drives it -
it never opens a socket itself.

## Options

**Enable**
- *Checked:* the widget is live. This is the default.
- *Unchecked:* performs no operation - On/Off/Toggle/Refresh are ignored,
  and any request already in flight is abandoned.

**Status**
Lit when the plug's relay is on. Set from the device's own reply, never
guessed - a fresh widget shows it unlit until On/Off/Toggle/Refresh
actually asks the device.

**NetStatus**
What the network side of the widget is doing right now: Idle, Connecting,
Waiting for reply, or an Error line naming what went wrong (a bad address,
a refused connection, a timeout, or the device refusing the command).

**On / Off / Toggle**
Sends the plug the matching command. Toggle sends the opposite of the last
known Status. Every press is also an ordinary property, so a Pulse or a
script can drive the plug the same way the buttons do.

**Refresh**
Asks the device for its current state without changing it. Only ever runs
when pressed - nothing here checks on its own.

## Settings

**Host Name**
The plug's IP address on the local network.

**Port**
The plug's control port. 9999 on every model that speaks this protocol -
change it only if you know it differs.
