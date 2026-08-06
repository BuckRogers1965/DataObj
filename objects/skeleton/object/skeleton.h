#ifndef SKELETON_H
#define SKELETON_H

/*
 * skeleton.h - the WHOLE interface to a Skeleton object.
 *
 * There is nothing else. No properties to read, no state to poll, no struct to
 * cast: a driver gets a handle from the class's own InstanceStart, hands over
 * where to report back, and from then on sends messages and catches answers.
 *
 * That restriction is the point. An object nobody can reach into can be
 * replaced, or later moved onto its own thread, without a single caller
 * changing - because the callers never had anything but this file.
 */

#include "callback.h"

/* Verbs and variables, all from USER_MESSAGE_BASE so they cannot collide with
   the framework's own message ids. Keep the ORDER stable across versions -
   these are the ABI, which is what the class's Major/Minor gate protects. */
enum {
	SKELETON_DO_MSG = USER_MESSAGE_BASE,
	SKELETON_START_MSG,
	SKELETON_STOP_MSG
};

/* what comes BACK, as offsets from the base the owner chose at creation:
   answers arrive as (base + ordinal) on the owner's chosen port */
enum {
	SKELETON_DONE = 0,
	SKELETON_FAILED
};

#define SkeletonDo(pSkel, data)  DeliverMsg((pSkel), "Msg", SKELETON_DO_MSG, (data))
#define SkeletonStart(pSkel)     DeliverMsg((pSkel), "Msg", SKELETON_START_MSG, 0L)
#define SkeletonStop(pSkel)      DeliverMsg((pSkel), "Msg", SKELETON_STOP_MSG, 0L)

#endif
