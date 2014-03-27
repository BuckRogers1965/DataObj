
#include <stdio.h>

#include "node.h"
#include "object.h"
#include "DebugPrint.h"
#include "callback.h"

//#include "framework.h"


/*

 // on activate open specified file for writing
 // messages in goto file.

Have a property to turn on and off the writing.

Turning the object off disables any output.

*/

static NodeObj Self;

int Handle_Message(NodeObj instance, MsgId message, NodeObj data){

	DebugPrint ( "Handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);

	return rtrn_handled;
}

void _init()
{

	Self = Register("Writer", "GrokThink", "8da17004-242c-4f21-a77e-6a823a52c640", &Handle_Message);
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

	if (GetInt(GetPropNode(instance, "Active"))){
		//SetValue(property, data);
		//Propagate(property, data, "Out");
	}
	return rtrn_propagate;
}

int
Activate_Intercept(NodeObj instance, MsgId message, NodeObj data){

	/* If this property is off, then nothing is copied from in to out */


	return rtrn_propagate;
}

int
Echo_Intercept(NodeObj instance, MsgId message, NodeObj data){

	/* If this property is off, then nothing is copied to Standard out */

	/* If you become a deamon then the standard out needs to be sent to a log file somewhere */

	return rtrn_propagate;

}
