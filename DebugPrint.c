

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "DebugPrint.h"
#include "timer.h"

/*

Tasks needed done:

O  Tie date string to timer formating

O  Allow different output formats for the the date string

O  Tie time to cached time in timer.c

*/

char *
GetTypeStr(int type){

	switch(type){

	case PROG_FLOW:
		return "Program Flow";

	case ERROR:
		return "Error";

	case CMDLINEOPTS:
		return "Command Line";

	case REGISTER:
		return "Register";

	case OBJMSGHANDLING:
		return "Object Message Handling";

	default:
		return "Needs Finished";
	}
}

char *
GetDateStr(){

	return "200812202325";

}

/* the current verbose level, 0 to 9, set from the command line */
static int debug_level = 1;

/* the minimum verbose level each message type needs to print

	0	errors only
	1	normal running: program flow and object messages (the default)
	2	adds registration traffic and command line parsing
	3+	adds the node dumps (see UnregisterLibrary in object.c)
*/
int
TypeThreshold(int type){

	switch(type){

	case ERROR:
		return 0;

	case PROG_FLOW:
	case OBJMSGHANDLING:
		return 1;

	case CMDLINEOPTS:
	case REGISTER:
		return 2;

	default:
		return 1;
	}
}

void
DebugPrintSetLevel(int level){
	debug_level = level;
}

int
DebugPrintGetLevel(){
	return debug_level;
}

void
DebugPrint(char * report, char * file, int line, int type) {

	if (debug_level < TypeThreshold(type))
		return;

	printf("%s	%s %d	%s	%s\n", FormatDate(0), file, line, GetTypeStr(type), report);

}
