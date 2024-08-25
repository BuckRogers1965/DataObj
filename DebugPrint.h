

#ifndef Debug_Print_H_
#define Debug_Print_H_


enum {PROG_FLOW=0, ERROR, CMDLINEOPTS, REGISTER, OBJMSGHANDLING};

void
DebugPrint(char * report, char * file, int line, int type);


#endif