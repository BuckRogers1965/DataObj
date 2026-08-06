

#ifndef Debug_Print_H_
#define Debug_Print_H_


enum {PROG_FLOW=0, ERROR, CMDLINEOPTS, REGISTER, OBJMSGHANDLING, CLONE, WIRE, PLACE, IMPORT};

void
DebugPrint(char * report, char * file, int line, int type);

/* the verbose level from the command line gates what prints, */
/* each message type has a minimum level it needs             */
void
DebugPrintSetLevel(int level);

int
DebugPrintGetLevel(void);


#endif