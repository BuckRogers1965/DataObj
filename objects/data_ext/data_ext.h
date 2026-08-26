#ifndef DATA_EXT_H
#define DATA_EXT_H

/*
 * data_ext.h - the WHOLE interface to a data object.
 *
 * A data object holds values in nodes. The nodes are the only place a
 * value lives; anything showing one holds a link to it, never a copy.
 *
 * A shape says how those nodes are addressed and how they are written
 * out. A subclass answers where it differs and drops the rest, so the
 * walk carries on up and the plain node tree is what you get.
 *
 * Ids are the ABI, which the class's Major/Minor gate protects: append
 * at the end for a minor bump, reorder or remove for a major one.
 */

#include "callback.h"

enum {
	/* Write me out the way I store myself. The data node comes back
	   carrying the text. A class that drops this is written out as its
	   nodes. */
	DATA_EXT_SERIALIZE_MSG = USER_MESSAGE_BASE,

	/* The reverse, from that same text. Ships with SERIALIZE or not at
	   all - a representation and its inverse are one answer, and they
	   cannot be allowed to drift apart. */
	DATA_EXT_DESERIALIZE_MSG,

	/* Hand back the node holding the value at this address. What an
	   address IS belongs to the shape: a grid reads Row and Col, a
	   record reads a field name. The answer is returned on the data
	   node's "Node" property. */
	DATA_EXT_ADDRESS_MSG,

	/* How much of me is there, in the terms of my own shape. */
	DATA_EXT_SHAPE_MSG
};

#define DataExtSerialize(pData,into)   DeliverMsg((pData), "Msg", DATA_EXT_SERIALIZE_MSG, (into))
#define DataExtDeserialize(pData,from) DeliverMsg((pData), "Msg", DATA_EXT_DESERIALIZE_MSG, (from))
#define DataExtAddress(pData,at)       DeliverMsg((pData), "Msg", DATA_EXT_ADDRESS_MSG, (at))
#define DataExtShape(pData,into)       DeliverMsg((pData), "Msg", DATA_EXT_SHAPE_MSG, (into))

#endif
