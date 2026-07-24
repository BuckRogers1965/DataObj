#include "callback.h"

typedef void * TaskObj;
typedef void * TaskId;
typedef void * TaskList;

enum {task_callback, task_deactivate};

int
ExecTasks();

void
AdjustDelayedTasks(TaskList list, unsigned long offset);

/* microseconds until the list's next due task - 0 if something is already */
/* due or the list is empty. See sched.c for the full comment.             */
unsigned long
SchedNextWakeMicros(TaskList list);

TaskList
CreateList();

TaskObj
CreateTask(TaskList list);

int
AddTaskDelay(TaskObj task, int delay_seconds, int delay_milliseconds, FuncPtr func, int mesgid, NodeObj data, char * name);

int
AddTaskNow(TaskObj task, FuncPtr func, int mesgid, NodeObj data);

int
AddTaskMilli(TaskObj task, unsigned long delay_milliseconds, FuncPtr func, int mesgid, NodeObj data);

int
AddTaskSec(TaskObj task, int delay_seconds, FuncPtr func, int mesgid, NodeObj data);

int
RemoveTask(TaskObj task);

TaskObj
GetTask(TaskList list);

int
DeleteTask(TaskObj task);

/* allocation accounting: task_entry structs currently allocated (in a  */
/* list OR parked on the reuse pool) - see NodeCount (node.h)            */
long
TaskStructCount(void);

/* minimal read-only introspection for walking a list's pending tasks   */
/* from outside sched.c - used by object.c to find and neutralize a     */
/* queued message dispatch whose source instance is being deleted       */
/* (TaskList/TaskObj stay opaque; these just expose the chain and the   */
/* two fields needed to identify a task without exposing task_entry)    */
TaskObj
GetTaskListHead(TaskList list);

TaskList
GetTaskListRunnow(TaskList list);

TaskObj
GetTaskNext(TaskObj task);

FuncPtr
GetTaskCallback(TaskObj task);

NodeObj
GetTaskData(TaskObj task);

void
SchedTest ();
