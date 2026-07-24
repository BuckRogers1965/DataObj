
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"
#include "dyn/buff.h"

/*

Writer object: a data sink.

Its In port subscribes to a source through Connect().  Chunks arriving
on In are copied into a dynamic buffer, and a drain task writes the
buffer to the file named in the Filename property one chunk per tick.

When the source has sent EOF and the write buffers are done, the file
is closed and no further task is scheduled.  Once every object has gone
quiet like this the task list empties and the program ends on its own.

*/

#define WRITER_CHUNK_SIZE 1024

typedef struct InstanceData
{
	FILE  * file;
	TaskObj task;
	buff    buffer;
	int     active;
	int     enabled;	/* the Enable port gates the draining */
	int     scheduled;	/* is the drain task currently armed? */
	int     eof_received;
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem WriterPanel[];

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "Writer handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* scheduler callback: drain one chunk from the buffer to the file */
int Writer_WriteChunk(NodeObj instance, NodeObj data, int reason)
{
	char * block;
	unsigned int length;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (reason == task_deactivate)
		return rtrn_handled;

	if (!local || !local->active)
		return rtrn_dropped;

	local->scheduled = 0;

	/* paused: chunks keep buffering, the Enable port re-arms the drain */
	if (!local->enabled)
		return rtrn_handled;

	/* oldest data first, the tail is the front of the queue */
	length = buffGetBlockFromTail(local->buffer, &block, WRITER_CHUNK_SIZE);
	if (length && block && local->file)
		fwrite(block, 1, length, local->file);

	if (buffGetLength(local->buffer) > 0)
	{
		/* more waiting, keep draining */
		AddTaskNow(local->task, (FuncPtr)Writer_WriteChunk, msg_send, instance);
		local->scheduled = 1;
	}
	else if (local->eof_received)
	{
		/* the source said EOF and our write buffers are done */
		if (local->file)
		{
			fclose(local->file);
			local->file = NULL;
		}
		local->active = 0;
		SetPropInt(instance, "State", Stopping);

		DebugPrint ( "Writer drained its buffer after EOF and deactivated.", __FILE__, __LINE__, OBJMSGHANDLING);
	}

	/* otherwise go quiet until the next chunk arrives on In */

	return rtrn_handled;
}

/* subscription callback: the router delivers messages from the source here */
int Writer_OnIn(NodeObj instance, MsgId message, NodeObj data)
{
	char * str;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || !local->active)
		return rtrn_dropped;

	switch (message)
	{
	case msg_send:
		/* copy the chunk into our buffer, the data node   */
		/* belongs to the source and goes away after this  */
		str = GetValueStr(data);
		if (str && str[0])
			buffAdd(local->buffer, str, strlen(str));
		break;

	case msg_eof:
		local->eof_received = 1;
		break;

	default:
		return rtrn_dropped;
	}

	/* make sure the drain task is armed */
	if (!local->scheduled && local->enabled)
	{
		AddTaskNow(local->task, (FuncPtr)Writer_WriteChunk, msg_send, instance);
		local->scheduled = 1;
	}

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Writer_OnEnable(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local || message != msg_send)
		return rtrn_dropped;

	if (GetValueInt(data))
	{
		if (!local->enabled)
		{
			local->enabled = 1;
			SetValueStr(GetPropNode(instance, "Enable"), "1");

			/* resume draining whatever piled up while paused */
			if (local->active && !local->scheduled
				&& (buffGetLength(local->buffer) > 0 || local->eof_received))
			{
				AddTaskNow(local->task, (FuncPtr)Writer_WriteChunk, msg_send, instance);
				local->scheduled = 1;
			}
		}
	}
	else
	{
		local->enabled = 0;
		SetValueStr(GetPropNode(instance, "Enable"), "0");
	}

	return rtrn_handled;
}

/* open the output file, then sleep until data arrives on In */
int Writer_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	char * filename;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	Widget_BuildOnce(instance, WriterPanel);

	if (local->active)
		return rtrn_handled;

	filename = GetPropStr(instance, "Filename");
	if (!filename || !filename[0])
	{
		DebugPrint ( "Writer has no Filename to write.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	local->file = fopen(filename, "w");
	if (!local->file)
	{
		DebugPrint ( "Writer could not open its output file.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	/* one task struct for the instance's whole life - see leaktest.py */
	if (!local->task)
		local->task = CreateTask(ObjGetTaskList());
	local->active = 1;
	SetPropInt(instance, "State", Running);

	/* no task armed yet, the first chunk on In arms the drain task */

	return rtrn_handled;
}

/* The whole panel in one table: main view, Help, and every control. In is the
   input port, shown as a readout of the last chunk that arrived. */
static WidgetItem WriterPanel[] = {
	/* cls        prop       def  panel   x    y    w    h  label       [handler] */
	{ "View",     "Writer",  "",  0,   0,   0, 300, 220, 0 },			/* 0: main */
	{ "Help",     "objects/writer/README.md", "", 0, 0, 0, 0, 0, 0 },	/* 1: help */

	{ "Checkbox", "Enable",  "1", 0, 270,  12,   9,  9, LABEL_LEFT, (void *)Writer_OnEnable },
	{ "Textbox",  "Filename","",  0,  15,  35, 260, 22, LABEL_NONE },
	{ "LED",      "State",   "1", 0,  15,  78,  12, 12, LABEL_NONE },
	{ "TextOut",  "In",      "",  0,  15, 118, 260, 20, LABEL_LEFT, (void *)Writer_OnIn },

	{ NULL }
};

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData * local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->file = NULL;
	local->task = NULL;
	local->buffer = buffCreate(4 * WRITER_CHUNK_SIZE);
	local->active = 0;
	local->enabled = 1;
	local->scheduled = 0;
	local->eof_received = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "Writer");

	/* every control's value + handler from the table (Enable/In carry a handler;
	   Filename/State are plain data - In is the sink port, read on the panel) */
	Widget_Init(instance, WriterPanel);

	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Writer_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, WriterPanel);
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, WriterPanel);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	Widget_CancelBuild(instance);
	if (local)
	{
		/* stop the drain task before freeing local, or a still-scheduled */
		/* task fires later with a dangling instance pointer as its data  */
		if (local->task)
			DeleteTask(local->task);
		if (local->file)
			fclose(local->file);
		if (local->buffer)
			buffDestroy(local->buffer);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Writer");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* every control, from the table (In among them, shown as a readout) */
	Widget_Publish(ClassSelf, WriterPanel);

	return rtrn_handled;
}

int ClassEnd(NodeObj library, MsgId message, NodeObj data)
{
	UnRegisterClass(library, ClassSelf);
	ClassSelf = NULL;

	return rtrn_handled;
}

void _init()
{
	NodeObj temp = NewNode(INTEGER);

	SetName(temp, "Writer");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c640");
	SetPropStr(temp, "Version", "1.0");
	SetPropStr(temp, "Dependencies", "");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)0);
	SetPropInt(temp, "State", 1);

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
