enum { Stop = 0, Continue, ContinueNoDescend, ReturnThenContinue };
enum { MatchDir = 0, DoNotMatchDir };
enum { PreOrder = 0, PostOrder };

typedef void FoundFile (char *name, int depth);

int
LoadObject (char *name, int depth);

int
ScanDir (char *name, char *extension, FoundFile * function, int maxDepth,
	 int includeDir, int SearchDirection);

void InstallObjects(void);
