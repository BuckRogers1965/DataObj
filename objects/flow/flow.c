/*
 * Flow - composition recorded as data, and replayed.
 *
 * Build one with NewFlow, then use the Flow* calls in place of the plain
 * CreateObject/SetProp/Connect/ActivateInstance: each performs the real call
 * AND records it as a step. RunFlow replays those steps into a container;
 * SaveFlow/LoadFlow are the same steps as text.
 *
 * This was in object.c, but a flow is loaded LAST - after every module is
 * registered - so by the time anything needs a flow interpreted, the object
 * that interprets it is already here. The core keeps no flow verbs at all.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "object.h"
#include "callback.h"
#include "DebugPrint.h"
#define FLOW_IMPL
#include "flow.h"

static NodeObj LibrarySelf;
static NodeObj ClassSelf;

NodeObj NewFlow(char *name){

	NodeObj flow = NewNode(INTEGER);
	SetName(flow, name);
	return flow;
}

/* every instance a flow creates carries the alias it was created under, */
/* so later Flow* calls on that instance know what to record it as       */
NodeObj FlowCreateObject(NodeObj flow, NodeObj container, char *classname){

	NodeObj inst, instr;
	int count;
	char alias[80];

	inst = CreateObject(container, classname, NULL);
	if (!inst)
		return NULL;

	count = GetPropInt(flow, classname) + 1;
	SetPropInt(flow, classname, count);
	snprintf(alias, sizeof(alias), "%s%d", classname, count);

	SetPropStr(inst, "FlowAlias", alias);

	instr = NewNode(INTEGER);
	SetName(instr, "Create");
	SetPropStr(instr, "Class", classname);
	SetPropStr(instr, "As", alias);
	AppendChild(flow, instr);

	return inst;
}

void FlowSetProp(NodeObj flow, NodeObj instance, char *prop, char *value){

	NodeObj instr;
	char *alias;

	if (!instance)
		return;

	SetPropStr(instance, prop, value);

	alias = GetPropStr(instance, "FlowAlias");
	if (!flow || !alias)
		return;

	instr = NewNode(INTEGER);
	SetName(instr, "Set");
	SetPropStr(instr, "Instance", alias);
	SetPropStr(instr, "Prop", prop);
	SetPropStr(instr, "Value", value);
	AppendChild(flow, instr);
}

/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
int FlowConnect(NodeObj flow, NodeObj fromInst, char *fromPort, NodeObj toInst, char *toPort){

	NodeObj instr;
	char *fromAlias, *toAlias;
	int ok;

	ok = Connect(fromInst, fromPort, toInst, toPort);
	if (!ok || !flow)
		return ok;

	fromAlias = GetPropStr(fromInst, "FlowAlias");
	toAlias   = GetPropStr(toInst, "FlowAlias");
	if (!fromAlias || !toAlias)
		return ok;

	instr = NewNode(INTEGER);
	SetName(instr, "Connect");
	SetPropStr(instr, "FromInstance", fromAlias);
	SetPropStr(instr, "FromPort", fromPort);
	SetPropStr(instr, "ToInstance", toAlias);
	SetPropStr(instr, "ToPort", toPort);
	AppendChild(flow, instr);

	return ok;
}

int FlowActivateInstance(NodeObj flow, NodeObj instance){

	NodeObj instr;
	char *alias;
	int rc;

	rc = ActivateInstance(instance);

	alias = instance ? GetPropStr(instance, "FlowAlias") : NULL;
	if (flow && alias) {
		instr = NewNode(INTEGER);
		SetName(instr, "Activate");
		SetPropStr(instr, "Instance", alias);
		AppendChild(flow, instr);
	}

	return rc;
}

/* replay a flow script - Create/Set/Connect/Activate instructions, in   */
/* order - into a container, building live instances from scratch. Used */
/* both right after recording (to sanity check it) and after loading a  */
/* script back off disk. The alias table is local to one replay: it     */
/* only has to resolve the instance names the script itself defines.    */
NodeObj RunFlow(NodeObj container, NodeObj flow){

	NodeObj instr, aliases, inst, fromInst, toInst;
	char *classname, *alias, *prop, *value;
	char *fromAlias, *fromPort, *toAlias, *toPort;

	if (!flow)
		return NULL;

	aliases = NewNode(INTEGER);

	instr = GetChild(flow);
	while (instr) {

		if (CmpName(instr, "Create")) {
			classname = GetPropStr(instr, "Class");
			alias     = GetPropStr(instr, "As");
			inst = CreateObject(container, classname, NULL);
			if (inst) {
				SetPropStr(inst, "FlowAlias", alias);
				SetPropLong(aliases, alias, (long)inst);
			}
		}
		else if (CmpName(instr, "Set")) {
			alias = GetPropStr(instr, "Instance");
			inst  = (NodeObj) GetPropLong(aliases, alias);
			prop  = GetPropStr(instr, "Prop");
			value = GetPropStr(instr, "Value");
			if (inst)
				SetPropStr(inst, prop, value);
		}
		else if (CmpName(instr, "Connect")) {
			fromAlias = GetPropStr(instr, "FromInstance");
			fromPort  = GetPropStr(instr, "FromPort");
			toAlias   = GetPropStr(instr, "ToInstance");
			toPort    = GetPropStr(instr, "ToPort");
			fromInst  = (NodeObj) GetPropLong(aliases, fromAlias);
			toInst    = (NodeObj) GetPropLong(aliases, toAlias);
			if (fromInst && toInst)
				Connect(fromInst, fromPort, toInst, toPort);
		}
		else if (CmpName(instr, "Activate")) {
			alias = GetPropStr(instr, "Instance");
			inst  = (NodeObj) GetPropLong(aliases, alias);
			if (inst)
				ActivateInstance(inst);
		}

		instr = GetNextSibling(instr);
	}

	DelNode(aliases);
	return flow;
}

int SaveFlow(NodeObj flow, char *filename){

	char *text;
	FILE *f;

	if (!flow || !filename)
		return 0;

	text = NodeToText(flow);
	if (!text)
		return 0;

	f = fopen(filename, "w");
	if (!f) {
		free(text);
		return 0;
	}

	fputs(text, f);
	fclose(f);
	free(text);
	return 1;
}

NodeObj LoadFlow(NodeObj container, char *filename){

	FILE *f;
	long size;
	char *text;
	NodeObj flow;

	f = fopen(filename, "r");
	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);

	text = malloc(size + 1);
	if (!text) {
		fclose(f);
		return NULL;
	}
	fread(text, 1, size, f);
	text[size] = '\0';
	fclose(f);

	flow = TextToNode(text);
	free(text);
	if (!flow)
		return NULL;

	RunFlow(container, flow);
	return flow;
}

/* every loadable object must export this, the loader checks for it */
int Handle_Message(NodeObj instance, MsgId message, NodeObj data)
{
	(void) instance; (void) message; (void) data;

	return rtrn_dropped;
}

int ClassStart(NodeObj library, MsgId message, NodeObj data)
{
	NodeObj class = NewNode(INTEGER);

	(void) message; (void) data;

	SetName(class, "Flow");

	/* no InstanceStart: a flow is a recording, not a thing on a canvas */

	ClassSelf = RegisterClass(library, class);

	SetClassVersion(ClassSelf, "1", "0");
	SetClassParent(ClassSelf, "Object");

	/* the names flow.h looks up */
	SetPropLong(ClassSelf, "New",      (long)NewFlow);
	SetPropLong(ClassSelf, "Create",   (long)FlowCreateObject);
	SetPropLong(ClassSelf, "Set",      (long)FlowSetProp);
	SetPropLong(ClassSelf, "Wire",     (long)FlowConnect);
	SetPropLong(ClassSelf, "Activate", (long)FlowActivateInstance);
	SetPropLong(ClassSelf, "Run",      (long)RunFlow);
	SetPropLong(ClassSelf, "Save",     (long)SaveFlow);
	SetPropLong(ClassSelf, "Load",     (long)LoadFlow);

	return rtrn_handled;
}

int ClassEnd(NodeObj library, MsgId message, NodeObj data)
{
	(void) message; (void) data;

	UnRegisterClass(library, ClassSelf);
	ClassSelf = NULL;
	return rtrn_handled;
}

void _init()
{
	NodeObj temp = NewNode(INTEGER);

	SetName(temp, "Flow");
	SetPropStr(temp, "Company", "GrokThink");
	SetPropStr(temp, "UUID", "bc5b9e3b-40ff-4b0d-8bd2-af3308d348bd");
	SetPropStr(temp, "Major", "1");
	SetPropStr(temp, "Minor", "0");
	SetPropLong(temp, "ClassStart", (long)ClassStart);
	SetPropLong(temp, "ClassEnd", (long)ClassEnd);
	SetPropLong(temp, "ClassMsg", (long)Handle_Message);
	SetPropInt(temp, "State", 1);

	AddDependency(temp, CORE_LIBRARY_FILE, "Object", "1", "0");

	LibrarySelf = RegisterLibrary(temp);
}

void _fini()
{
	ClearDependencies(LibrarySelf);
	UnregisterLibrary(LibrarySelf);
	LibrarySelf = NULL;
}
