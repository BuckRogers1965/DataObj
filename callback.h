

// the function parameter for message passing
// instance, data, and msg_id
typedef int (*FuncPtr)(NodeObj, NodeObj, int);

// the message_id that is sent in
enum { msg_change=0, msg_update, msg_initialize, msg_send };

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

