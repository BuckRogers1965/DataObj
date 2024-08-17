
#include <stdio.h>

#include "node.h"
#include "object.h"
#include "DebugPrint.h"
#include "callback.h"

//#include "framework.h"


/*

Msg objects have a source and destination and data objects.  This object mediates between other objects.


*/

static NodeObj Self;

int Handle_Message(NodeObj instance, MsgId message, NodeObj data){

	DebugPrint ( "Handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);

	// This is the message dispatch handler.  
	// The message has the message ID that is being changed.
	


	return rtrn_handled;
}

void _init()
{

	Self = Register("Msg", "GrokThink", "8da17004-242d-4f21-a77e-6a823a52c601", &Handle_Message);
}

void _fini()
{
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

int
In_Intercept(NodeObj instance, NodeObj property, MsgId message, NodeObj data){

	return rtrn_propagate;
}

int
Activate_Intercept(NodeObj instance, MsgId message, NodeObj data){

	/* If this property is off, then nothing is copied from in to out */

	return rtrn_propagate;
}

