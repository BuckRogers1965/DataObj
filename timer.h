
/*

Keeps track of time and caches current time / tick for the program so that all the various timers and other time tracking items don't have to make thousands of system calls everytime they need to know the high accuracy time.

Making timer system calls are very expensive and it necisary to cache the time this way.

Should only ever be updated by the main thread just before the scheduled tasks are executed.

*/

#include <time.h>

int
TimeUpdate ();

void
GetCurrentTime (unsigned long * seconds, unsigned long * milliseconds);

char *
FormatDate(time_t theTime);

time_t
GetCurrentSeconds ();
