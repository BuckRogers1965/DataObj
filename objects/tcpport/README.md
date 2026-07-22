# TCP Port

The TCP Port is a standard TCP (Transmission Control Protocol) object for
transmitting and receiving TCP data. It enables communication on the
configured port, including those associated with HTTP, FTP, SMTP, Telnet,
POP, and HTTPS.

**Default input connection** is to **Transmit Data**.
**Default output connection** is from **Receive Data**.

## Options

**Enable**
- *Checked:* prepares the TCP Port to send and receive data. This is the
  default.
- *Unchecked:* performs no operation and does not transmit or receive,
  even if Auto Open / Auto Listen / Auto Send is set or Open / Listen /
  Send is pressed.

**Host Name**
The URL (e.g. `www.hostname.com`) or IP address (e.g. `101.102.103.104`)
of a server. A URL should not contain the protocol prefix — use
`www.hostname.com`, not `http://www.hostname.com`.

**Standard Ports**
Selects the port to transmit and receive on. Picking a protocol writes the
Port box with that protocol's default number:

- **FTP** — 21
- **Telnet** — 23
- **SMTP** — 25
- **HTTP** — 80
- **POP** — 110
- **HTTPS** — 443

**Port**
The numeric value for the port. Selecting a protocol from Standard Ports
changes this to that protocol's default.

**Stream State**
Reports the current state of the connection: Disabled, Idling, Listen,
Opening, Connected, or Closing.

**Auto Open / Auto Listen / Auto Close**
- *Auto Open, checked:* opens a connection to Host Name when the flow
  loads; unchecked requires the Open button (the default).
- *Auto Listen, checked:* listens on Port when the flow loads; unchecked
  requires the Listen button (the default).
- *Auto Close, checked:* closes the port when the flow opens, regardless of
  saved state (the default); unchecked requires the Close button.

These three are mutually exclusive.

**Open / Listen / Close** (buttons)
Open opens the connection to Host Name; Listen serves on Port; Close closes
the connection.

**Status LEDs**
Disabled, Idling, Connected, and Listening report the port's state. The
finer-grained Opening / Open / Listening / Listen / Closing / Closed LEDs
appear on the **TCP Debug** panel.

**Secure**
Opens the **TCP SSL** panel (below).

**Debug**
Opens the **TCP Debug** panel (below).

### Transmit

**Transmit Data**
The commands and data to transmit to the connection.

**Tx Ready**
Lights when the port is ready to accept data into Transmit Data; unlights
when it is ready to send the box's contents.

**Send** — sends the contents of Transmit Data.
**Clear Tx** — clears Transmit Data.

**Binary (Transmit Data)**
- *Checked:* transmits in binary format.
- *Unchecked:* transmits as ASCII text (the default).

**Fix Line Ends (Transmit Data)**
- *Checked:* converts line ends to the platform standard when sending.
- *Unchecked:* sends content unchanged.

**Clear On Send**
- *Checked:* clears Transmit Data once its contents are copied to the send
  buffer.
- *Unchecked:* leaves the contents after transmission (the default).

**Auto Send**
- *Checked:* transmits the Transmit box whenever it changes while
  connected (the default).
- *Unchecked:* requires a press of Send.

### Receive

**Receive Data**
Commands and data received from the connection.

**Rx Ready**
Lights when Receive Data has received content; unlights when empty.

**Rx Bytes Ready**
The number of bytes received in Receive Data ready to be read.

**Clear Rx** — clears Receive Data.

**Binary (Receive Data)**
- *Checked:* receives in binary format.
- *Unchecked:* receives as ASCII text (the default).

**Fix Line Ends (Receive Data)**
- *Checked:* converts received line ends to the platform standard.
- *Unchecked:* receives content unchanged.

**Accumulate**
- *Checked:* adds newly received content to what is already in Receive Data
  (the default).
- *Unchecked:* replaces Receive Data with each new set of data received —
  useful when feeding a text box that should only see each chunk once.

---

## TCP SSL

Configures SSL (Secure Sockets Layer) for the TCP Port, reached by the
**Secure** button. Configuring security is required only when using a TCP
Port as a server.

**Enable**
- *Checked:* enables the security configuration.
- *Unchecked:* no security (the default).

**Status**
Black when idle, green when functioning, yellow when enabled but no secure
connection is established, and red when a problem is present.

**Certificate**
The certificate for the associated key. A certificate must be paired with a
key for security to function.

**Key**
The key associated with the certificate. A key must be paired with a
certificate for security to function.

**Pass Phrase**
The pass phrase for the certificate/key pair.

---

## TCP Debug

Views errors, state messages, and traffic during operation, reached by the
**Debug** button. There is no Enable checkbox: with no message-type boxes
checked, Debug is off; with one or more checked, it is on.

**Status lights**
Open, Opening, Listen, Listening, Close, and Closing each light for the
matching connection state. On this panel the LEDs show all four states
(black, green, yellow, red); on the main panel they show two (black,
green).

**Debug Messages**
The log, filtered by the message-type checkboxes below.

**Message Types**
- **Errors** — errors during transmission or receipt, prefixed `Err:`.
- **Progress** — progress messages, prefixed `Prog:`.
- **Tx Data** — content sent from Transmit Data, prefixed `Tx:`.
- **Rx Data** — content received in Receive Data, prefixed `Rx:`.
- **Messages** — general debug messages, prefixed `Dbg:`.

All default to off.

**Clear** — clears Debug Messages.
