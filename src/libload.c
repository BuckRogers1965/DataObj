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
#include "node.h"
#include "object.h"

#define BUFFLEN 10024


int
LoadObject (char *name, int depth)
{
	void *ClassHandle;
	char *error;
	char hereName[BUFFLEN];
	char DebugMsg[BUFFLEN];
	//LPVCLASS pnewDevClass;

        char bundlePath[PATH_MAX];

	/* part of the FoundFile callback signature (ScanDir) */
	(void) depth;

	if (getcwd( bundlePath, PATH_MAX ) == NULL)
		bundlePath[0] = '\0';

	fflush(NULL);
	//printf("\n");
	snprintf (hereName, BUFFLEN-10, "%s/%s", bundlePath, name);

	/* clear the hand-back first: RegisterLibrary sets LastLibrary from inside
	   the module's _init(), so an unset value after dlopen means the module
	   registered no library of its own */
	SetPropLong(GetRegObjList(), "LastLibrary", 0);

	ClassHandle = dlopen(hereName, RTLD_LAZY);
	if (!ClassHandle) {
		//fputs(dlerror(), stderr);
		/* hereName is as big as DebugMsg, so the prefix cannot also fit -
		   bound the path explicitly rather than let it truncate blind */
		snprintf((char *)&DebugMsg, BUFFLEN-10, "Failed to load %.*s",
				 (int)(BUFFLEN - 30), hereName);
		DebugPrint ( (char *)&DebugMsg, __FILE__, __LINE__, PROG_FLOW);
		return 0;
	} else {
		snprintf((char *)&DebugMsg, BUFFLEN-1, "Loaded %s", name);
		DebugPrint ((char *)&DebugMsg, __FILE__, __LINE__, PROG_FLOW);
	}

	/* clear any stale error first - a NULL result alone is not proof the  */
	/* symbol is missing (a present symbol can legitimately be NULL); the  */
	/* dlerror() check below is the real test                              */
	dlerror();
	(void) dlsym(ClassHandle, "Handle_Message");
	if ((error = dlerror()) != NULL) {

		// couldn't find the proper start point, unload library
		dlclose(ClassHandle);

		//fputs(error, stderr);
		DebugPrint ( "Failed to find HandleMessage in Library.", __FILE__, __LINE__, ERROR);
		return 0;
	} else
		DebugPrint ( "Found HandleMessage in Library.", __FILE__, __LINE__, PROG_FLOW);

	/* stamp the file this library came from - a dependency names a file, and
	   the library node cannot know its own: it was built in _init(), which
	   never sees the path. Basename only, so a dependency names what you drop
	   in the scan path rather than where it sits today. */
	{
		NodeObj registered = (NodeObj) GetPropLong(GetRegObjList(), "LastLibrary");
		char *base = strrchr(name, '/');

		if (registered)
			SetPropStr(registered, "File", base ? base + 1 : name);
	}

	return 0;
}


