#ifndef TABLE_H
#define TABLE_H

/*
 * table.h - the WHOLE interface to a Table.
 *
 * A Table is a data_ext whose shape is a grid. It adds no verbs of its
 * own: it answers data_ext's, reading an address as a Row and a Col
 * instead of a name, and writing itself out as rows of cells rather
 * than as a list of nodes.
 *
 *     DataExtAddress(t, at)   at carries "Row" and "Col", the node
 *                             holding that cell comes back on "Node"
 *     DataExtShape(t, into)   "Rows" and "Cols" come back on it
 *     DataExtSerialize(t,to)  the grid, as text, on "Text"
 *     DataExtDeserialize      the same text, read back
 *
 * Rows and Cols are ordinary properties on the instance, so they clone
 * and export with the cells.
 */

#include "callback.h"
#include "data_ext.h"

/* How a cell is named. The cells are properties and a property is found
   by name, so the address is IN the name - and it has to be unique,
   because IsPortableProp refuses a name already used earlier in the
   list, which would drop the duplicate from clone and export. */
#define TABLE_CELL_FORMAT "R%dC%d"

#endif
