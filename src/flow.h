#ifndef FLOW_H
#define FLOW_H

/*
 * flow.h - composition recorded as data, reached through the Flow class.
 *
 * The implementation lives in flow.object. Its entry points are function-
 * pointer properties on the Flow class node, so a caller goes through the
 * registry it already shares instead of linking against another .object.
 * FLOW_IMPL is defined by the implementation, which wants the prototypes.
 */

#include "node.h"
#include "object.h"
#include "DebugPrint.h"

#ifdef FLOW_IMPL
NodeObj NewFlow(char *name);
NodeObj FlowCreateObject(NodeObj flow, NodeObj container, char *classname);
void FlowSetProp(NodeObj flow, NodeObj instance, char *prop, char *value);
/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
int FlowConnect(NodeObj flow, NodeObj fromInst, char *fromPort, NodeObj toInst, char *toPort);
int FlowActivateInstance(NodeObj flow, NodeObj instance);
NodeObj RunFlow(NodeObj container, NodeObj flow);
int SaveFlow(NodeObj flow, char *filename);
NodeObj LoadFlow(NodeObj container, char *filename);
#else
static inline long FlowEntry(char *name)
{
	static NodeObj cls;
	char msg[120];

	if (!cls)
		cls = FindClass("Flow");
	if (!cls) {
		snprintf(msg, sizeof(msg),
				 "flow.h: the Flow class is not loaded - '%s' unreachable", name);
		DebugPrint(msg, __FILE__, __LINE__, ERROR);
		return 0;
	}
	return GetPropLong(cls, name);
}

static inline NodeObj NewFlow(char *name)
{
	NodeObj (*fn)(char *name) = (NodeObj (*)(char *name)) FlowEntry("New");

	return fn ? fn(name) : NULL;
}

static inline NodeObj FlowCreateObject(NodeObj flow, NodeObj container, char *classname)
{
	NodeObj (*fn)(NodeObj flow, NodeObj container, char *classname) = (NodeObj (*)(NodeObj flow, NodeObj container, char *classname)) FlowEntry("Create");

	return fn ? fn(flow, container, classname) : NULL;
}

static inline void FlowSetProp(NodeObj flow, NodeObj instance, char *prop, char *value)
{
	void (*fn)(NodeObj flow, NodeObj instance, char *prop, char *value) = (void (*)(NodeObj flow, NodeObj instance, char *prop, char *value)) FlowEntry("Set");

	if (fn)
		fn(flow, instance, prop, value);
}

/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
static inline int FlowConnect(NodeObj flow, NodeObj fromInst, char *fromPort, NodeObj toInst, char *toPort)
{
	/* This is named wrong. The framework does not have ports, it has properties that exist in containers and that are containers. */
	int (*fn)(NodeObj flow, NodeObj fromInst, char *fromPort, NodeObj toInst, char *toPort) = (int (*)(NodeObj flow, NodeObj fromInst, char *fromPort, NodeObj toInst, char *toPort)) FlowEntry("Wire");

	return fn ? fn(flow, fromInst, fromPort, toInst, toPort) : 0;
}

static inline int FlowActivateInstance(NodeObj flow, NodeObj instance)
{
	int (*fn)(NodeObj flow, NodeObj instance) = (int (*)(NodeObj flow, NodeObj instance)) FlowEntry("Activate");

	return fn ? fn(flow, instance) : 0;
}

static inline NodeObj RunFlow(NodeObj container, NodeObj flow)
{
	NodeObj (*fn)(NodeObj container, NodeObj flow) = (NodeObj (*)(NodeObj container, NodeObj flow)) FlowEntry("Run");

	return fn ? fn(container, flow) : NULL;
}

static inline int SaveFlow(NodeObj flow, char *filename)
{
	int (*fn)(NodeObj flow, char *filename) = (int (*)(NodeObj flow, char *filename)) FlowEntry("Save");

	return fn ? fn(flow, filename) : 0;
}

static inline NodeObj LoadFlow(NodeObj container, char *filename)
{
	NodeObj (*fn)(NodeObj container, char *filename) = (NodeObj (*)(NodeObj container, char *filename)) FlowEntry("Load");

	return fn ? fn(container, filename) : NULL;
}
#endif

#endif
