
/*

Keeps track of time and caches current time / tick for the program so that all the various timers and other time tracking items don't have to make thousands of system calls everytime they need to know the high accuracy time.

Making timer system calls are very expensive and it necisary to cache the time this way.

Should only ever be updated by the main thread just before the scheduled tasks are executed.

*/

#include <time.h>

/* returns the whole-second jump since the last update when the clock     */
/* moved unsmoothly (see timer.c), 0 in normal running - long, matching   */
/* the definition (the old int declaration was an LTO type mismatch)      */
long
TimeUpdate (void);

/* microsecond resolution, not millisecond - sched.c is the only caller,   */
/* and needs real precision now that SndMsg queues every message through   */
/* it (many same-tick sends need distinguishable timestamps, and the main  */
/* loop's adaptive sleep needs a real due-time to sleep up to)             */
void
GetCurrentTime (unsigned long * seconds, unsigned long * microseconds);

char *
FormatDate(time_t theTime);

time_t
GetCurrentSeconds ();
