typedef int(*FuncPtr)(NodeObj, NodeObj, int);

enum { msg_change=0, msg_update, msg_initialize, msg_send };

enum { rtrn_handled=0, rtrn_propagate, rtrn_dropped };

