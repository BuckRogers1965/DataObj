#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "node.h"
#include "list.h"
#include "timer.h"
#include "callback.h"


/*

This is the scheduler/state machine engine.  This allows arbitrary execution of a function bundled with a data object

Check for this already done in svn project.  NOPE!  Must have been on the laptop hard drive when that ate itself.  Note to self, start backing up my work on hard drive to thumb drive.

This object tracks lists of nodes that are scheduled to run at some point.

Items are added to the list to be executed with various similar function calls that add with a 0 delay, a millisecond delay, or a second delay, all unsigned.  You cannot add an item scheduled to run before now.

There is a single list.  There is a pointer to the end of the "now" items on the front of the list to allow you to easily add more 0 second delay items to the list.  

The items that are scheduled to run now, including scheduled items that have expired are moved to another list.  This is so that you don't confuse items that are being rescheduled to run with the items in your current list.

The item itself has a function to call, a message id, and a data object that is the data.

The receiving object can then process the event and perhaps send more messages to other items.

*/


enum {task_callback, task_deactivate};

typedef struct task_entry * TaskPtr;
typedef struct task_list * TaskList;

//typedef int(*FuncPtr)(TaskPtr, NodeObj, int);


struct task_entry {
   TaskPtr  next;        /* for task lists */
   TaskPtr  prev;        /* for task lists */

   TaskList owner;	 /* which list is this entry in? */

			 /* Scheduled time to run - micros, not millisecs: */
			 /* SndMsg queues every message through here now,  */
			 /* millisecond resolution was too coarse to tell   */
			 /* same-tick sends apart or to size an adaptive    */
			 /* main-loop sleep accurately (see AddTaskDelay,   */
			 /* SchedNextWakeMicros)                             */
   unsigned long seconds;
   unsigned long micros;

			 /* Start Time */
   unsigned long start_seconds;
   unsigned long start_micros;

   FuncPtr  callback;    /* Task active function */
   NodeObj  data;       /* passed to func as fData */

   int      trace;	 /* Turn on extra debugging output for this task */
   DataObj  name;	 /* task name */

   int      linked;	 /* currently spliced into owner's head/tail chain? */
			 /* guards RemoveTaskFromList against unlinking a   */
			 /* task that isn't actually in the list right now  */
			 /* (already fired-and-not-rescheduled, or never     */
			 /* armed) - without this a delete of such a task    */
			 /* would still zero out owner's head/tail as if it  */
			 /* were the last entry, wiping out unrelated tasks  */
   } task_entry;

struct task_list {

	TaskPtr head;
	TaskPtr tail;

	unsigned long entry_count;

	/* reusable due-task bucket for ExecTasks - always empty at rest,   */
	/* swapped in and drained back to empty every call, so ExecTasks    */
	/* never has to malloc a scratch list per tick                      */
	TaskList runnow;

	/* freelist of spare, currently-unowned task_entries (linked via    */
	/* ->next, same as runnow's chain) - GetTask/RemoveTask recycle     */
	/* through here so short-lived one-shot tasks (like queued message  */
	/* dispatch) don't malloc/free a task_entry per use                 */
	TaskPtr pool;

	/* last task successfully inserted by AddTaskDelay - lets a burst of  */
	/* same-timestamp inserts (0-delay messages queued in the same tick,  */
	/* now the common case since SndMsg routes every send this way) find  */
	/* their spot in O(1) amortized instead of walking the whole run from */
	/* head every time; see AddTaskDelay for the correctness argument     */
	TaskPtr insertHint;

} task_list;


   # ifndef S_SPLINT_S
/* create and initialize a new task list object that is empty */
TaskList
CreateList(){

	TaskList list = malloc(sizeof(task_list));

	list->head = NULL;
	list->tail = NULL;

	list->entry_count = 0;

	list->runnow = NULL;
	list->pool = NULL;
	list->insertHint = NULL;

	return list;
}

TaskPtr
CreateTask(TaskList list){

	TaskPtr task = malloc(sizeof(task_entry));

	task->next = NULL;
	task->prev = NULL;

	task->owner = list;

	task->callback = NULL;
	task->data = NULL;
	task->name = NULL;
	task->trace = 0;
	task->linked = 0;

	return task;


	return NULL;
}

void
RemoveTaskFromList(TaskPtr task){

	TaskList list = task->owner;

	/* remove a scheduled task from the list of things to execute. */
	/* this is needed when an item that is running is being deleted. */
	/* Or when it is being moved to another list */

	/* not currently spliced into any chain (already fired and never  */
	/* rescheduled, or never armed) - nothing to unlink. Without this  */
	/* guard the branches below would treat it as the sole entry and  */
	/* null out list->head/tail, losing every other scheduled task    */
	if (!task->linked)
		return;

	/* a task leaving the chain must never remain the insert hint:     */
	/* ActivateTimedTasks moves due tasks into the runnow chain (where  */
	/* AddTaskToTail sets linked=1 again), and a hint still pointing at */
	/* one passed AddTaskDelay's validity check, sent its insert walk   */
	/* down the WRONG chain, and appended the new task - a due-now poll */
	/* re-arm, say - unsorted at the main list's tail, behind whatever  */
	/* far-future timers were parked there. ActivateTimedTasks stops at */
	/* the first non-due entry, so that task then sat unrunnable until  */
	/* the unrelated timer ahead of it fired: the "web server stalls    */
	/* for ~20s until some other flow's timer goes off" stutter.        */
	if (list->insertHint == task)
		list->insertHint = NULL;

	if (!task->prev){
		list->head = task->next;
	} else {
		task->prev->next = task->next;
	}

	if (!task->next){
		list->tail = task->prev;
	} else {
		task->next->prev = task->prev;
	}

	task->prev = NULL;
	task->next = NULL;
	task->linked = 0;
}

/* hand back a task_entry: reuse a spare from the pool if one exists,      */
/* otherwise malloc a fresh one - the counterpart to RemoveTask, which     */
/* returns a retired task to this same pool instead of freeing it          */
TaskPtr
GetTask(TaskList list){

	TaskPtr task;

	if (list->pool) {
		task = list->pool;
		list->pool = task->next;
		task->next = NULL;
		return task;
	}

	return CreateTask(list);
}

/* retire a task without freeing it: unlink it from wherever it's         */
/* currently scheduled (safe no-op if it isn't linked) and park it on     */
/* owner's pool for GetTask to hand back out later. Callers that are      */
/* mid-teardown (DeleteTask, which frees the task_entry itself right      */
/* after calling back with task_deactivate) must not use this - it would  */
/* recycle a task_entry that's about to be freed out from under the pool  */
int
RemoveTask(TaskPtr task){

	TaskList list = task->owner;

	RemoveTaskFromList(task);

	task->callback = NULL;
	task->data = NULL;

	task->next = list->pool;
	list->pool = task;

	return 1;
}


int
DeactivateTask(TaskPtr task){

	RemoveTaskFromList(task);

	/* a task that was created but never armed (Activate never ran)   */
	/* has no callback yet                                             */
	if (task->callback)
		(*task->callback)(task->data, task->data, task_deactivate);

	return 1;
}

int
DeleteTask(TaskPtr task){

	TaskList owner = task->owner;

	/* Remove task from any list it is in */
	DeactivateTask(task);

	/* about to free task - insertHint must never point at freed memory   */
	if (owner->insertHint == task)
		owner->insertHint = NULL;

	free(task);

	task = NULL;

	return 1;

}

int
DeleteList(TaskList list){

	/* iterate through the list of tasks, deactivate each one */

	TaskPtr current = list->head;
	TaskPtr next;

	while (current){
		next = current->next;
		DeleteTask(current);
		current = next;
	}

	/* runnow is always empty at rest (ExecTasks fully drains it before */
	/* returning), so it just needs freeing, not walking                */
	if (list->runnow)
		free(list->runnow);

	/* pool holds retired-but-reusable task_entries (RemoveTask) - unlike */
	/* runnow these are real, if inert, allocations and must be walked    */
	current = list->pool;
	while (current) {
		next = current->next;
		free(current);
		current = next;
	}

	free (list);

	list = NULL;

	return 1;
}

/* minimal read-only introspection, see sched.h - lets a caller walk a  */
/* list's pending tasks (both the main chain and the in-flight runnow   */
/* bucket) without exposing struct task_entry/task_list themselves      */
TaskPtr
GetTaskListHead(TaskList list){
	if (!list)
		return NULL;
	return list->head;
}

TaskList
GetTaskListRunnow(TaskList list){
	return list->runnow;
}

TaskPtr
GetTaskNext(TaskPtr task){
	return task->next;
}

FuncPtr
GetTaskCallback(TaskPtr task){
	return task->callback;
}

NodeObj
GetTaskData(TaskPtr task){
	return task->data;
}

/* adds a task to the list with a delay. delay_micros is microseconds, not */
/* milliseconds - see the task_entry.micros comment                        */
int
AddTaskDelay(TaskPtr task, int delay_seconds, int delay_micros, FuncPtr func, int mesgid, NodeObj data){



//	printf("Add task with delay of % d seconds %d microseconds\n", delay_seconds, delay_micros);
	TaskList list = task->owner;
	TaskPtr current;

	unsigned long seconds;
	unsigned long micros;
	unsigned long dueSeconds;
	unsigned long dueMicros;

	(void) mesgid;

	/* Get the time */
	GetCurrentTime(&seconds, &micros);

	task->callback = func;

	/* the data rides with the task and is handed to the callback, */
	/* objects pass their instance node through here               */
	task->data = data;

	/* the absolute due time, in the same unsigned type the task and the  */
	/* sorted-insert comparisons below carry                               */
	dueSeconds = seconds + (unsigned long) delay_seconds;
	dueMicros  = micros  + (unsigned long) delay_micros;

	if (dueMicros > 1000000) {
		dueMicros = dueMicros - 1000000;
		dueSeconds += 1;
	}

	task->seconds = dueSeconds;
	task->micros = dueMicros;
	task->next=NULL;
	task->prev=NULL;
	task->linked=1;


	// just add the task if the task list is empty.
	if (!list->head) {
		list->head = list->tail = task;
		task->next = task->prev = NULL;
		list->insertHint = task;
		return 1;
	}

	/* insert the task into the time task list in the order that it is sleeping. */
	/* <= on the microsecond tie-break, not < : this must walk PAST every       */
	/* existing entry at the same timestamp, not stop in front of the first     */
	/* one, or same-tick insertions land in LIFO order instead of FIFO. That    */
	/* was invisible when only recurring task rearms went through here (they    */
	/* essentially never collide on the exact microsecond), but SndMsg now      */
	/* queues every message this way, and a burst of same-tick sends - a        */
	/* client's WebSocket commands forwarded back-to-back, notably - reversing  */
	/* their own delivery order is a real, user-visible correctness bug, not    */
	/* just a tie-break nicety.                                                  */
	/*                                                                            */
	/* Walking that past-the-ties every time is O(n) per insert, though, and a   */
	/* burst of same-tick sends is exactly what piles up many ties in a row -    */
	/* O(n) per insert times n inserts is O(n^2) for one burst, measured at      */
	/* over 600x slower on a 200k-message burst. insertHint remembers where the  */
	/* last insert landed and resumes the walk from there instead of from head:  */
	/* safe whenever the hint's timestamp is <= the new one, since every task    */
	/* between head and the hint is then still guaranteed <= the new timestamp   */
	/* too (the list stays sorted), and free/pool-recycle both go through paths  */
	/* that invalidate the hint first, so it never dangles onto freed memory.    */
	current = (list->insertHint && list->insertHint->linked
		&& (list->insertHint->seconds < dueSeconds
			|| (list->insertHint->seconds == dueSeconds && list->insertHint->micros <= dueMicros)))
		? list->insertHint : list->head;

	for ( ; current && (current->seconds < dueSeconds
		|| (current->seconds == dueSeconds && current->micros <= dueMicros)); ) {
        current = current->next;
    }

	// insert at the front of the list
	if (list->head == current) {

		list->head->prev = task;
		task->next = list->head;
		list->head = task;

	// insert at the end of the list
	} else if (!current) {

		list->tail->next = task;
		task->prev = list->tail;
		list->tail = task;

	// insert in the middle of the list
	} else {

		task->next = current;
		task->prev = current->prev;

		current->prev = task;
		task->prev->next = task;
	}

	list->insertHint = task;

	return 1;
}

/* convenience function to make Add Task Delay easier */
int
AddTaskNow(TaskPtr task, FuncPtr func, int mesgid, NodeObj data){
	return AddTaskDelay(task, 0, 0, func, mesgid, data);
}

/* convenience function to make Add Task Delay easier */
int
AddTaskMilli(TaskPtr task, unsigned long delay_millisecs, FuncPtr func, int mesgid, NodeObj data){
//	printf("Add task with delay of %d milliseconds\n", (int) delay_millisecs);
	return AddTaskDelay(task, delay_millisecs/1000, (delay_millisecs%1000)*1000, func, mesgid, data);
}

/* convenience function to make Add Task easier */
int
AddTaskSec(TaskPtr task, unsigned long delay_seconds, FuncPtr func, int mesgid, NodeObj data){
//	printf("Add task with delay of %d seconds\n", (int) delay_seconds);
	return AddTaskDelay(task, delay_seconds, 0, func, mesgid, data);
}

void
AddTaskToTail(TaskList list, TaskPtr task){

	task->linked = 1;

	/* add first task */
	if (!list->head){
		list->head = list->tail = task;
		task->next = NULL;
		task->prev = NULL;
		return;
	}

	list->tail->next = task;
	task->next = NULL;
	task->prev = list->tail;
	list->tail = task;
}

/* print out the names, msg id, and data values of each item in the given task list */
void
PrintDebugList(TaskList list){
	(void) list;
}

void
ActivateTimedTasks(TaskList list, TaskList runnow){

	unsigned long seconds;
	unsigned long micros;

	TaskPtr current = list->head;
	TaskPtr next= NULL;

	/* Get the time */
	GetCurrentTime(&seconds, &micros);

//	printf("\n\n*** Activate Time %d %d\n\n", seconds, micros);

	while (current) {
        	if ((current->seconds > seconds) ||
				(current->seconds == seconds && current->micros > micros))
			break;

//		printf("\n\n*** TASK Activate %d %d\n\n", current->seconds, current->micros);

		next=current->next;
		RemoveTaskFromList(current);
		AddTaskToTail(runnow, current);
		current=next;
	}

}

/* this is the part that does the work */
int
ExecTasks(TaskList list){

	TaskPtr current;

	int taskcount = 0;

	/* due tasks land here instead of a freshly malloc'd list every call - */
	/* runnow is always empty at rest (drained below), so it's created     */
	/* once per TaskList, ever, and just reused tick after tick            */
	if (!list->runnow)
		list->runnow = CreateList();

	/* move all the items whose timer has expired to the execute now list */
	/* this prevents there from being confusion between what we are currently doing and what we will be doing next time */

	ActivateTimedTasks(list, list->runnow);

	/* iterate through the list of items to execute */

	while (list->runnow->head){

		/* remove task from list */
		/* tasks must reschedule each time they are called */
		current = list->runnow->head;
		list->runnow->head = current->next;
		current->next = NULL;
		current->prev = NULL;
		current->linked = 0;

		/* a callback that reschedules itself calls AddTaskDelay, which  */
		/* inserts via task->owner - the real `list`, not runnow - so    */
		/* rescheduled/new tasks land pre-merged into `list` in sorted   */
		/* order with no separate merge step needed                     */
		if(current->callback)
			(*current->callback)(current->data, current->data, task_callback);

		taskcount++;
	}
	list->runnow->tail = NULL;

	if (list->head || taskcount)
		return 1;
	else return 0;

}

void
AdjustDelayedTasks(TaskList list, unsigned long offset){

	TaskPtr current = list->head;

	while (current) {
		current->seconds += offset;
		current = current->next;
	}

}

/* microseconds until list->head is due - the list stays time-sorted, so   */
/* head is always the soonest-due entry, and this is all the main loop     */
/* needs to sleep exactly that long instead of spinning on a fixed poll    */
/* interval. Returns 0 if something is already due (or overdue) or if the  */
/* list is empty - an empty list means ExecTasks is about to report 0 and  */
/* the program is quiescing anyway, so there is nothing meaningful to wait */
/* for.                                                                     */
unsigned long
SchedNextWakeMicros(TaskList list){

	unsigned long seconds, micros;
	long due;

	if (!list->head)
		return 0;

	GetCurrentTime(&seconds, &micros);

	due = ((long)list->head->seconds - (long)seconds) * 1000000L
	    + ((long)list->head->micros  - (long)micros);

	return (due > 0) ? (unsigned long) due : 0;
}

int
testcallback(NodeObj object, NodeObj data, int value){
	(void) object;
	(void) data;
	(void) value;

	printf("!!! ");
	return 1;
}
   # endif

void
SchedTest (){

	int CountOfScheduledTasks = 1;

	TimeUpdate();

   # ifndef S_SPLINT_S

	TaskList testlist = CreateList();
	
	TaskPtr   testtask1 =  CreateTask(testlist);
	TaskPtr   testtask2 =  CreateTask(testlist);
	TaskPtr   testtask3 =  CreateTask(testlist);
	TaskPtr   testtask4 =  CreateTask(testlist);

	NodeObj testdata = NewNode(INTEGER);
	SetPropInt(testdata, "TestData", 1);


	AddTaskDelay(testtask1, 5, 500000, &testcallback, 1000, testdata);  /* 5.5s: 500000us */
	AddTaskNow(testtask2, &testcallback, 1001, testdata);
	AddTaskMilli(testtask3, 100, &testcallback, 1002, testdata);
	AddTaskSec(testtask4, 10, &testcallback, 1003, testdata);

	printf("Schedtest\n");

	while(CountOfScheduledTasks){
		//long offset;

		TimeUpdate();

		CountOfScheduledTasks = ExecTasks(testlist);

		/* if we have no scheduled tasks, then begin stopping */

		printf(".");
		fflush(stdout);
		usleep(10000);
	}
   # endif

	printf("\n");

}


#ifdef TESTBUILD
int main (){

SchedTest();

return 0;
}

#endif
