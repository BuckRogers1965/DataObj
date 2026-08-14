# Rest

A REST translator. It answers one request at a time and remembers nothing
between them, which is what makes it a sibling of the Bridge rather than a
layer on it: the Bridge maintains a live model of the tree inside a browser,
this does not.

Wire it the way Http is wired - it never touches a socket:

    Connect(Tcp, "Out", Rest, "In")
    Connect(Rest, "Out", Tcp, "In")

## What it publishes

**ManifestView** names a container, `/Root/mcp` by default. Its members are
the manifest. Containment is the publication: drag an object into that view
and it is published, drag it out and it is not. There is no exported flag and
no category.

    GET /

is the list - one entry per member, its name and its class:

    {"view":"/Root/mcp","members":[
      {"name":"bob","class":"Checkbox"},
      {"name":"temp","class":"Textbox"}]}

The name is the `Name` property, which is what the member is addressed by
and what a rename changes - not the birth name its class gave it.

    GET /manifest

is the same members fully described, which is what a client reads once to
learn how to drive them. Each entry has:

- `name`, `class`
- `help` - the member's own README, the file its Help panel already reads
- `face` - how to drive it
- `properties` - the class's published interface: every property, the control
  type that renders it, and the class default

## Driving what is in the view

The published view is the root of the URL space. A member is addressed by
its own name, and nothing in a URL says where the view lives - move
`ManifestView` and every one of these follows it.

    GET  /                 the list - one line per member
    GET  /manifest         the same members, fully described
    GET  /Textbox          the member's value, through its face
    PUT  /Button           write the member's face input - the press
    GET  /Textbox/Value    one named property
    PUT  /Textbox/Value    write one named property

Drop a Button and a Textbox into the view, then press the button and read
the box as two separate requests:

    curl -X PUT -d 1 http://localhost:8483/bob
    curl http://localhost:8483/temp

**Every answer says whether it worked**, in one field, in parsable JSON.
The caller already knows what it sent; what it cannot know is whether the
write landed and why not.

    PUT   {"status":"Success"}
    GET   {"status":"Success","value":"batten down the hatches!"}
    any   {"status":"Error","error":"no such property: Valu on temp"}

A read adds `value`, the one thing it asked for and did not already have.
A write adds nothing - it either worked or it says why not.

Reading a **container** lists what is in it instead, because a View has
no `In`, no `Out` and no `Value` and its contents are what reading it
means. Judged on whether the face's property is actually there, never on
what class the member is - so a sub-view lists, and a widget with a real
output still reads that output.

    curl http://localhost:8483/View_1
    {"view":"/View_1","members":[{"name":"knob","class":"Knob"}]}

Names are matched exactly: `/View_1`, not `/view_1`.

The HTTP status agrees with the body: **200** it was there, **201** the
write created it (late binding is legitimate, and this is how a script
tells a real write from a typo), **404** unknown member or property,
**400** a body shorter than its own `Content-Length`, refused rather than
written truncated.

There is no vocabulary here and no per-object code. `SetOrDeliverProp`
decides for itself whether a name resolves to a port - in which case the
port's handler runs, exactly as wired traffic would - or a plain data
property, which is a direct write. Pressing a button and typing into a
textbox are the same call, so a widget that grows a new command grows a
new endpoint with nothing to change here.

## The face, and what a bare member means

A bare member goes through its face: a `GET` reads what it produces, a
`PUT` writes what it takes. The default resolves against what the
instance actually has, never against what class it is - a declared
`ReservedIn`/`ReservedOut` wins, then `In`/`Out` if the object is on the
plain dataflow shape, and otherwise `Value`, which is all there is to
write to on a control. A class nobody has written yet resolves the same
way.

    Button      in Value    out Value
    Textbox     in Value    out Value
    Filter      in In       out Out
    TCPPort     in TxData   out RxData

Naming a property addresses that property instead, and the two grammars
are the same one: the member name is joined to the view's path and
resolved, so the longest prefix that names an instance is the instance
and whatever is left is walked as properties. A member nested in a
sub-view therefore needs no special case, and `/Box/Value/W` is legal and
means what it looks like.

A `GET` never conjures: an unresolved path is a 404 and creates nothing.
The body of a `PUT` is taken raw and is not URL-decoded.

## Controls

- **Enable** - 0 drops every request; the socket is the TCP's business
- **ManifestView** - the published container
- **State** - lit once active
- **Requests** - answered so far
- **In** / **Out** - the last request and response, as readouts

## Not yet

`POST` - feed the inputs, wait for the result, answer - which needs the
pending record (Conn, output, deadline) and the completion rules beyond
`eof`. Also the `show/rest` face file, and `?depth=` / `?raw=` on a GET.
