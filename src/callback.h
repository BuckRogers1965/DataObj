
#ifndef Callback_H_
#define Callback_H_

// the function parameter for message passing
// instance, data, and msg_id
typedef int (*FuncPtr)(NodeObj, NodeObj, int);

// the message_id that is sent in
// msg_eof marks the end of a stream, it travels the same path as the data
/* APPENDED, never inserted - these are stored in flow files and compiled
   into every loaded module, so an existing value must not shift.
   msg_serialize: write yourself out; the answer comes back on the data
   node's "Text". msg_deserialize: read yourself back from that same Text.
   An object that keeps state where a property walk cannot see it - a
   private object it points at with a LONG - answers these; one that does
   not says so and the caller's ordinary walk applies. */
enum { msg_change=0, msg_update, msg_initialize, msg_send, msg_eof,
	   msg_serialize, msg_deserialize };

// where an object's OWN message ids start, so they never collide with the
// framework's. An object declares its verbs and vars as an enum from here,
// and a driver reaches them with DeliverMsg (see objects/udp/udp.h).
#define USER_MESSAGE_BASE 100

// The return values from these functions
//
//  rtrn_handled 
//  msg was taken care of do no forward
//
// rtrn_propagate
// send this message to all subscribers
//
// rtrn_dropped
// message was not handled and do not propagates
//

// rtrn_unhandled
// I DID NOT HANDLE THIS. Distinct from rtrn_dropped, which is a handled
// verdict - the handler recognised the message and deliberately consumed it
// without forwarding. Only rtrn_unhandled moves the class walk on, so a
// class that refuses something is not overridden by its parent.
// APPENDED: existing values must not shift.

enum { rtrn_handled=0, rtrn_propagate, rtrn_dropped, rtrn_unhandled };

#endif

