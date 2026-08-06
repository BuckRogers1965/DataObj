
#ifndef Callback_H_
#define Callback_H_

// the function parameter for message passing
// instance, data, and msg_id
typedef int (*FuncPtr)(NodeObj, NodeObj, int);

// the message_id that is sent in
// msg_eof marks the end of a stream, it travels the same path as the data
enum { msg_change=0, msg_update, msg_initialize, msg_send, msg_eof };

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

enum { rtrn_handled=0, rtrn_propagate, rtrn_dropped };

#endif

