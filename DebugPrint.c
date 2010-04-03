

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

void
DebugPrint(char * report, char * file, int line, int type) {

	printf("%s	%s %d	%s	%s\n", FormatDate(0), file, line, GetTypeStr(type), report);

}
