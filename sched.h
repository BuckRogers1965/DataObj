#include "callback.h"

typedef void * TaskObj;
typedef void * TaskId;
typedef void * TaskList;

enum {task_callback, task_deactivate};

int
ExecTasks();

void
AdjustDelayedTasks(TaskList list, unsigned long offset);

TaskList
CreateList();

TaskObj
CreateTask(TaskList list);

int
AddTaskDelay(TaskObj task, int delay_seconds, int delay_milliseconds, FuncPtr func, int mesgid, NodeObj data, char * name);

int
AddTaskNow(TaskObj task, FuncPtr func, int mesgid, NodeObj data);

int
AddTaskMilli(TaskObj task, int delay_milliseconds, FuncPtr func, int mesgid, NodeObj data);

int
AddTaskSec(TaskObj task, int delay_seconds, FuncPtr func, int mesgid, NodeObj data);

int 
RemoveTask(TaskObj task);

void
SchedTest ();
