
/*
  This module is designed to
  recursively scan directories
  and call a function for each matching filename.
*/


// the following is needed for the dirent and scandir
#include <dirent.h>

// needed for the printf
#include <stdio.h>

//following is included because of the chdir
#include <unistd.h>

// the following is needed for the lstat
#include <sys/types.h>
#include <sys/stat.h>


#include <string.h>
#include <stdlib.h>

enum { Stop = 0, Continue, ContinueNoDescend, ReturnThenContinue };
enum { MatchDir = 0, DoNotMatchDir };
enum { PreOrder = 0, PostOrder };

typedef void FoundFile (char *name, int depth);

typedef struct MatchCall
{
  char Match[NAME_MAX];
  FoundFile *FunctionToCall;
} MatchCall;

void
PrintFileName (char *name, int depth)
{
  printf ("%s %d\n", name, depth);
}

#define NO_MATCH 0
#define MATCH    1

int extMatch(char * name, char * extension){

	int nameLen,
	    extLen;

	// if either is NULL, return not match
	if (!name || !extension)
		return NO_MATCH;

	nameLen = strlen(name);
	extLen  = strlen(extension);

	if (extLen > nameLen)
		return NO_MATCH;

	nameLen = nameLen - extLen;

	for (; extLen && extension[extLen-1] == name[nameLen+extLen-1]; extLen--)
		;

	if (extLen)
		return NO_MATCH;
	else
		return MATCH;
}

void
ScanRegularFiles (struct dirent **namelist, int n, char *extension,
		  int currentDepth, FoundFile * function)
{

  // allocate space to hold the lstat value
  struct stat sb;

  //int length;

  while (n--)
    {
      // find the length of the name
      //length = strlen (namelist[n]->d_name);

      // get the information about the file
      // notice that we use lstat() here and not stat()
      // this is because for symbolic links stat()
      // returns the info for the file the symbolic
      // link points to
      if (lstat (namelist[n]->d_name, &sb) != -1)
	{
	  // bit mask out the correct flags
	  switch (sb.st_mode & S_IFMT)
	    {
	    case S_IFREG:	/* regular file */
	      // if we have a match
	      if (extMatch(namelist[n]->d_name, extension))
		// call the function we were passed in
		(*function) (namelist[n]->d_name, currentDepth);
	      break;
	    }			// switch
	}			// if
    }				// while
}

int
PrivateScanDir (char *name, char *extension, FoundFile * function,
		int maxDepth, int currentDepth, int includeDir,
		int SearchDirection);
void
ScanDirectoryFiles (char *name, char *extension, FoundFile * function,
		    int maxDepth, int currentDepth, int includeDir,
		    int SearchDirection, struct dirent **namelist, int n)
{

  // allocate space to hold the lstat value
  struct stat sb;

  int length;

  // for each name
  while (n-- > 0)
    {
      // find the length of the name
      length = strlen (namelist[n]->d_name);

      // get the file info for the name, load it into sb
      if (lstat (namelist[n]->d_name, &sb) != -1)
	{
	  // bit mask out the correct flags
	  switch (sb.st_mode & S_IFMT)
	    {
	    case S_IFDIR:	/* directory */

	      // if we are matching for names of directories too, 
	      // then see if it matches
	      if (SearchDirection == PreOrder)
		//if (includeDir
		 //   && namelist[n]->d_name[length - 1] == extension[0])
		if (includeDir && extMatch(namelist[n]->d_name, extension))
		  (*function) (namelist[n]->d_name, currentDepth);

	      // eliminate the . from consideration
	      if (!(length = 1 && namelist[n]->d_name[0] == '.'))
		// eliminate the .. from consideration
		if (!(length = 2 && namelist[n]->d_name[0] == '.'
		      && namelist[n]->d_name[1] == '.'))
		  // don't recurse downwards more than the desired depth
		  if (maxDepth >= currentDepth)
		    // call self recursively
		    PrivateScanDir (namelist[n]->d_name, extension, function,
				    maxDepth, currentDepth, includeDir,
				    SearchDirection);

	      // if we are matching for names of directories too, then see if it matches
	      if (SearchDirection == PostOrder)
		//if (includeDir
		  //  && namelist[n]->d_name[length - 1] == extension[0])
		if (includeDir && extMatch(namelist[n]->d_name, extension))
		  (*function) (namelist[n]->d_name, currentDepth);

	      break;
	    }			// switch
	}			// if
    }				// while
}

int
PrivateScanDir (char *name, char *extension, FoundFile * function,
		int maxDepth, int currentDepth, int includeDir,
		int SearchDirection)
{
  struct dirent **namelist;
  int n, ignoreint;
  char oldpath[PATH_MAX];

  // store the current directory path to restore later
  char * ignore = getcwd (oldpath, PATH_MAX);
  if (ignore == NULL) ;

  // change to the new directory path
  if (chdir (name) == -1)
    return Continue;

	/*
	ENOTDIR: A component of the path prefix is not a directory.
	ENAMETOOLONG: A component of a pathname exceeded {NAME_MAX} characters,
		 or an entire path name exceeded {PATH_MAX} characters.
	ENOENT: The named directory does not exist.
	ELOOP: Too many symbolic links were encountered in translating the pathname.
	EACCES: Search permission is denied for any component of the path name.  
	EFAULT: Path points outside the process's allocated address space.
	EIO: An I/O error occurred while reading from or writing to the file system.
	*/

  // scan the current directory
  n = scandir (".", &namelist, 0, alphasort);

  // if we got an error back then handle the error
  if (n < 0)
    {
      perror ("scandir");
       ignoreint = chdir (oldpath);
       if (ignoreint == 0) ;
       return 0;
    }
  else
    {

      // ok, we got a valid list of the files in a directory.

      // increment the depth each time you go down a level.
      // the first level you are on is 1
      currentDepth++;

      // choose here if you are going to put out the files going down, or coming back up.
      switch (SearchDirection)
	{

	case PreOrder:
	  // output all the matching files in the directory
	  // then descend into the directory recursively
	  ScanRegularFiles (namelist, n, extension, currentDepth, function);
	  ScanDirectoryFiles (name, extension, function, maxDepth,
			      currentDepth, includeDir, SearchDirection,
			      namelist, n);
	  break;

	case PostOrder:
	  // descend into the directory recursively
	  // then output all the matching files in the directory
	  ScanDirectoryFiles (name, extension, function, maxDepth,
			      currentDepth, includeDir, SearchDirection,
			      namelist, n);
	  ScanRegularFiles (namelist, n, extension, currentDepth, function);
	  break;
	}			// switch, the order things happen in
    }				// else
  while (n-- > 0){
	  free(namelist[n]);
  }
  free(namelist);

  ignoreint = chdir (oldpath);
  return 1;
}

/*
  This function is just a public wrapper to check a few values
  and to keep people from hosing up the current depth.
*/

int
ScanDir (char *name, char *extension, FoundFile * function, int maxDepth,
	 int includeDir, int SearchDirection)
{
  return PrivateScanDir (name, extension, function, --maxDepth, 0, includeDir,
			 SearchDirection);
}

