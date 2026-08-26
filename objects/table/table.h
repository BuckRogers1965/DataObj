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

#include <stdio.h>

#include "callback.h"
#include "data_ext.h"

/* HOW A CELL IS NAMED, and it is spreadsheet naming on purpose: columns
   are letters, rows are 1-based, so a cell is A1 and a path is
   /Root/Sheet/A1 - a name a person can type and a script can resolve.
   The cells are properties and a property is found by name, so the
   address IS the name; it has to be unique, because IsPortableProp
   refuses a name already used earlier in the list.

   Row and column stay 0-based everywhere in the code - only the name
   people read is 1-based. */
static inline void TableCellName(char *out, int size, int row, int col)
{
	char letters[8];
	int  n = 0, c = col;

	if (!out || size < 8 || row < 0 || col < 0)
	{
		if (out && size) out[0] = 0;
		return;
	}

	/* bijective base-26: A..Z, AA..AZ, BA.. - no zero digit, which is why
	   this decrements before each step rather than dividing cleanly */
	do {
		letters[n++] = (char) ('A' + (c % 26));
		c = c / 26 - 1;
	} while (c >= 0 && n < (int) sizeof(letters) - 1);

	while (n > 0 && size > 1)
	{
		*out++ = letters[--n];
		size--;
	}
	snprintf(out, size, "%d", row + 1);
}

/* the inverse: A1 -> row 0, col 0. Non-zero if the name is a cell name. */
static inline int TableCellParse(const char *name, int *row, int *col)
{
	int c = 0, r = 0, sawLetter = 0, sawDigit = 0;

	if (!name)
		return 0;

	for (; *name >= 'A' && *name <= 'Z'; name++)
	{
		c = c * 26 + (*name - 'A' + 1);
		sawLetter = 1;
	}
	for (; *name >= '0' && *name <= '9'; name++)
	{
		r = r * 10 + (*name - '0');
		sawDigit = 1;
	}

	if (!sawLetter || !sawDigit || *name || r < 1)
		return 0;

	if (row) *row = r - 1;
	if (col) *col = c - 1;
	return 1;
}

#endif
