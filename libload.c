#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// needed for the dynamic loading
#include <dlfcn.h>

// the following is needed for the PATH_MAX
#include <dirent.h>

//following is included because of the chdir
#include <unistd.h>

#include "DebugPrint.h"

#define BUFFLEN 10024


int
LoadObject (char *name, int depth)
{
	void *ClassHandle;
	int *ClassMsgFunc;
	char *error;
	char hereName[BUFFLEN];
	char DebugMsg[BUFFLEN];
	//LPVCLASS pnewDevClass;

        char bundlePath[PATH_MAX];
	char * ignore = getcwd( bundlePath, PATH_MAX );
        if (ignore == NULL) ;

	fflush(NULL);
	//printf("\n");
	snprintf (hereName, BUFFLEN-10, "%s/%s", bundlePath, name);

	ClassHandle = dlopen(hereName, RTLD_LAZY);
	if (!ClassHandle) {
		//fputs(dlerror(), stderr);
		snprintf((char *)&DebugMsg, BUFFLEN-10, "Failed to load %s", hereName);
		DebugPrint ( (char *)&DebugMsg, __FILE__, __LINE__, PROG_FLOW);
		return 0;
	} else {
		snprintf((char *)&DebugMsg, BUFFLEN-1, "Loaded %s", name);
		DebugPrint ((char *)&DebugMsg, __FILE__, __LINE__, PROG_FLOW);
	}

	ClassMsgFunc = dlsym(ClassHandle, "Handle_Message");
        if (ClassMsgFunc == NULL) ;
	if ((error = dlerror()) != NULL) {

		// couldn't find the proper start point, unload library
		dlclose(ClassHandle);

		//fputs(error, stderr);
		DebugPrint ( "Failed to find HandleMessage in Library.", __FILE__, __LINE__, ERROR);
		return 0;
	} else
		DebugPrint ( "Found HandleMessage in Library.", __FILE__, __LINE__, PROG_FLOW);

	return 0;
}


