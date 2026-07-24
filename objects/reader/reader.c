
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "sched.h"
#include "DebugPrint.h"
#include "widget.h"

/*

Reader object: a data source.

Reads the file named in its Filename property one chunk at a time and
sends each chunk as a message out its Out port.  The message is routed
to every subscriber of the port.

When the file is used up the reader sends msg_eof out the same port,
closes the file, and stops rescheduling its task.  With no task in the
scheduler the reader has gone quiet on its own.

Chunks travel as null terminated strings for now, so this reads text
files.  Binary payloads need a length carried beside the data node.

*/

#define READER_CHUNK_SIZE 1024

typedef struct InstanceData
{
	FILE  * file;
	TaskObj task;
	int     active;
	int     enabled;	/* the Enable port gates the reading   */
	int     scheduled;	/* is the read task currently armed?   */
} InstanceData;

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

static WidgetItem ReaderPanel[];

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	DebugPrint ( "Reader handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
	return rtrn_handled;
}

/* scheduler callback: read one chunk, send it, reschedule until EOF     */
/* the scheduler calls back as (data, data, reason) so the instance node */
/* must be the data the task was armed with                              */
int Reader_ReadChunk(NodeObj instance, NodeObj data, int reason)
{
	char buffer[READER_CHUNK_SIZE + 1];
	size_t bytes;
	NodeObj chunk;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (reason == task_deactivate)
		return rtrn_handled;

	if (!local || !local->file)
		return rtrn_dropped;

	local->scheduled = 0;

	/* paused: the file stays open, the Enable port re-arms us */
	if (!local->enabled)
		return rtrn_handled;

	bytes = fread(buffer, 1, READER_CHUNK_SIZE, local->file);

	if (bytes > 0)
	{
		buffer[bytes] = 0;

		chunk = NewNode(STRING);
		SetName(chunk, "Data");
		SetValueStr(chunk, buffer);

		/* SndMsg takes ownership of chunk and frees it once it's */
		/* been delivered to every subscriber                     */
		SndMsg(instance, "Out", msg_send, chunk);
	}

	if (feof(local->file) || ferror(local->file))
	{
		/* the stream is over, EOF goes out the same port as the */
		/* data, then this object goes quiet: no reschedule      */
		SndMsg(instance, "Out", msg_eof, NULL);

		fclose(local->file);
		local->file = NULL;
		local->active = 0;
		SetPropInt(instance, "State", Stopping);

		DebugPrint ( "Reader hit end of file, sent EOF, and deactivated.", __FILE__, __LINE__, OBJMSGHANDLING);
	}
	else
	{
		AddTaskNow(local->task, (FuncPtr)Reader_ReadChunk, msg_send, instance);
		local->scheduled = 1;
	}

	return rtrn_handled;
}

/* control callback: 1 enables, 0 disables, EOF on this line is ignored */
int Reader_OnEnable(NodeObj instance, MsgId message, NodeObj data)
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

			/* resume a paused read */
			if (local->active && local->file && !local->scheduled)
			{
				AddTaskNow(local->task, (FuncPtr)Reader_ReadChunk, msg_send, instance);
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

/* open the file and start the read task pumping */
int Reader_Activate(NodeObj instance, MsgId message, NodeObj data)
{
	char * filename;
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	if (!local)
		return rtrn_dropped;

	Widget_BuildOnce(instance, ReaderPanel);

	if (local->active)
		return rtrn_handled;

	filename = GetPropStr(instance, "Filename");
	if (!filename || !filename[0])
	{
		DebugPrint ( "Reader has no Filename to read.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	local->file = fopen(filename, "r");
	if (!local->file)
	{
		DebugPrint ( "Reader could not open its input file.", __FILE__, __LINE__, ERROR);
		return rtrn_dropped;
	}

	/* one task struct for the instance's whole life - see leaktest.py */
	if (!local->task)
		local->task = CreateTask(ObjGetTaskList());
	local->active = 1;
	SetPropInt(instance, "State", Running);

	/* the first read happens on the next trip through the main loop */
	AddTaskNow(local->task, (FuncPtr)Reader_ReadChunk, msg_send, instance);
	local->scheduled = 1;

	return rtrn_handled;
}

/* The whole panel in one table: main view, Help, and every control. Out is the
   output port, shown as a readout of the last chunk it emitted. */
static WidgetItem ReaderPanel[] = {
	/* cls        prop       def  panel   x    y    w    h  label       [handler] */
	{ "View",     "Reader",  "",  0,   0,   0, 300, 220, 0 },			/* 0: main */
	{ "Help",     "objects/reader/README.md", "", 0, 0, 0, 0, 0, 0 },	/* 1: help */

	{ "Checkbox", "Enable",  "1", 0, 270,  12,   9,  9, LABEL_LEFT, (void *)Reader_OnEnable },
	{ "Textbox",  "Filename","",  0,  15,  35, 260, 22, LABEL_NONE },
	{ "LED",      "State",   "1", 0,  15,  78,  12, 12, LABEL_NONE },
	{ "TextOut",  "Out",     "",  0,  15, 118, 260, 20, LABEL_LEFT },

	{ NULL }
};

int InstanceStart(NodeObj class, MsgId message, NodeObj data)
{
	NodeObj instance;
	InstanceData * local = malloc(sizeof(InstanceData));

	(void) message; (void) data;

	local->file = NULL;
	local->task = NULL;
	local->active = 0;
	local->enabled = 1;
	local->scheduled = 0;

	instance = NewNode(INTEGER);
	SetName(instance, "Reader");

	/* every control's value + handler from the table (Enable carries a handler;
	   Filename/State/Out are plain data - Out is the emit port, read on the panel) */
	Widget_Init(instance, ReaderPanel);

	SetPropLong(instance, "local", (long)local);
	SetPropLong(instance, "Activate", (long)Reader_Activate);

	InitPosition(instance);
	Widget_MainSize(instance, ReaderPanel);
	RegisterInstance(class, instance);
	Widget_DeferBuild(instance, ReaderPanel);

	return rtrn_handled;
}

int InstanceEnd(NodeObj instance, MsgId message, NodeObj data)
{
	InstanceData * local = (InstanceData *)GetPropLong(instance, "local");

	Widget_CancelBuild(instance);
	if (local)
	{
		/* stop the read task before freeing local, or a still-scheduled */
		/* task fires later with a dangling instance pointer as its data */
		if (local->task)
			DeleteTask(local->task);
		if (local->file)
			fclose(local->file);
		free(local);
	}

	return rtrn_handled;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	SetName(class, "Reader");
	SetPropLong(class, "InstanceStart", (long)InstanceStart);
	SetPropLong(class, "InstanceEnd", (long)InstanceEnd);

	ClassSelf = RegisterClass(library, class);

	PublishPosition(ClassSelf);

	/* every control, from the table (Out among them, shown as a readout) */
	Widget_Publish(ClassSelf, ReaderPanel);

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

	SetName(temp, "Reader");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c639");
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
