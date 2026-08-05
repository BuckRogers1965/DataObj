# UDP Port

A single port speaking the User Datagram Protocol (UDP), for transmitting
and receiving datagrams. UDP is connectionless: unlike TCP it offers almost
no error recovery, and there is no guarantee a datagram ever arrives. It is
packet-based rather than stream-based — blocks of data are sent between
ports — and it adds just two things to IP: port numbers, and an optional
checksum over the packet. That is enough whenever an unreliable
connectionless datagram service will do, and it is what broadcasting over a
network is built on.

This panel holds no socket. It creates a **UDP** engine of its own — private
state, with no location and nothing on the canvas — and drives it: Start/Stop drive the engine's `Enable`, sending delivers a
message to its `Send`, and the panel subscribes to its `Received` to learn
what arrived. The engine can be driven the same way by anything else — a
script, a Pulse, another flow — and this panel can be swapped without the
engine changing.

**Default input connection** is to **Send Packet**.
**Default output connection** is from **Receive Packet**.

## Options

**Enable**
- *Checked:* prepares the port to send and receive. This is the default.
- *Unchecked:* stops any operation in progress and performs no operation,
  even if Start is pressed.

**Any Port**
- *Checked:* the port to communicate on is chosen when Start is pressed — a
  different one each time.
- *Unchecked:* the port named in Port is used.

If no port is specified when Start is pressed, one is chosen anyway, even
with Any Port unchecked.

**Port**
The specific port number to send and receive on. With Any Port checked,
Port displays the port that was actually taken when Start was pressed.

**Auto Start**
- *Checked:* starts listening as soon as the panel is placed, cloned,
  imported, or re-enabled — on Port, or on a free port if Any Port is
  checked. Content sitting in Send Packet is not sent by an Auto Start.
- *Unchecked:* Start must be pressed. This is the default.

**Start**
Takes the port named in Port, or any free port with Any Port checked.
Pressing Start when already started does nothing. Anything already in
Send Packet is not sent by a Start.

If the requested port is already in use it cannot be taken; the failure is
logged and the On light stays dark. `netstat -an` lists the ports in use.

**Stop**
Stops sending and listening and gives the port back. Pressing Stop when
already stopped does nothing.

**On** — green once Start has been pressed and the port was taken.
**Off** — green once Stop has been pressed.

### Send

**Send Host** — where datagrams go. A dotted quad never blocks; a name is
resolved, which blocks the flow until it answers.

**Send Port** — the port to send to.

**Send Packet** — the content to send. Everything written here is sent, one
datagram per write: feed it a train of 1s and it transmits a 1 each time.
Typing in the box and a wire feeding it transmit the same way.

### Receive

**Receive Host** — the address the last datagram came from.
**Receive Port** — the port the last datagram came from.
**Receive Packet** — the content received. A wire out of this box carries
every datagram onward.

**Ready** — green once a datagram has been received on the listening port.
