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


int
LoadObject (char *name, int depth)
{
	void *ClassHandle;
	int *ClassMsgFunc;
	char *error;
	char hereName[1024];
	char DebugMsg[1024];
        int ignore;
	//LPVCLASS pnewDevClass;

        char bundlePath[PATH_MAX];
	ignore = (int) getcwd( bundlePath, PATH_MAX );

	fflush(NULL);
	//printf("\n");
	sprintf (hereName, "%s/%s", bundlePath, name);

	ClassHandle = dlopen(hereName, RTLD_LAZY);
	if (!ClassHandle) {
		//fputs(dlerror(), stderr);
		sprintf((char *)&DebugMsg, "Failed to load %s", hereName);
		DebugPrint ( (char *)&DebugMsg, __FILE__, __LINE__, PROG_FLOW);
		return 0;
	} else {
		sprintf((char *)&DebugMsg, "Loaded %s", name);
		DebugPrint ((char *)&DebugMsg, __FILE__, __LINE__, PROG_FLOW);
	}

	ClassMsgFunc = dlsym(ClassHandle, "Handle_Message");
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


