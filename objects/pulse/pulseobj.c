
/*

Uses the schedule facilities to create a state engine to send a message to it's Out property at a regular interval.

*/

int Handle_Message(NodeObj instance, MsgId message, NodeObj data){

	DebugPrint ( "Handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

void
_init(){

	Self = Register("Pulse", "GrokThink", "2b37c4c7-54d9-47d6-95e5-2dbffa208fa3", &Handle_Message);
}

void
_fini(){

	Unregister(Self);
	Self = NULL;
}

int
Class_Start(NodeObj instance, MsgId message, NodeObj data){

	return rtrn_handled;
}

int
Class_End(NodeObj instance, MsgId message, NodeObj data){

	return rtrn_handled;
}

int
Instance_Start(NodeObj instance, MsgId message, NodeObj data){

	/* create the instance */

	/* create the properties */

	/* insert the intercepts for the instance */ 

	return rtrn_propagate;
}

int
Instance_End(NodeObj instance, MsgId message, NodeObj data){

	/* Clean up the instance */

	/* remove intercepts, delete connections to properties */
	/* deletions to properties should happen at a higher layer. */

	return rtrn_propagate;
}


