#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef WINSYS
#include <malloc.h>
#include <string.h>
#endif

#include "buff.h"

/********************************************
  buff.c
  Dynamic Buffer

*/

/* private variables */

int buffCount = 0;
int buffSize = 0;

typedef struct object {
	unsigned int	head;			//  where data is read from for normal LIFO operation
						//  currently where data is added		
	unsigned int	lastHead;		//  saved for reading from the head
	unsigned int	tail;			//  where data is read from for normal FIFO operation
	//  the saved tail character is taken from the current tail which is replaced with a 0 byte
	char		savedTailChar;		//  character from tail + length of most recent FIFO read
	//  the saved head character is taken from the last head which is replaced with a 0 byte
	char		savedHeadChar;		//  character from head of most recent LIFO read
	char		lastNormalizedChar;  // reflects last character added to head of buffer by add normalize line end
	unsigned int	markHead;
	unsigned int	markLastHead;
	unsigned int	markTail;
	
	char			markSavedHeadChar;
	char			markSavedTailChar;
	
	unsigned int	size;
	void*			buffer;

	unsigned int	ReallocAdjustment;
	unsigned int	lineSearchOffset;

	unsigned int	countReallocs;
	unsigned int	countAddResetEmpty;
	unsigned int	countAddMoveForward;
	unsigned int	countInserts;
	unsigned int	countRetrievals;

	int addType;
	char * lineEnd;

} object;


/********************************************
  to create a new buffer
  given an initial size
  returns a pointer to an allocated buff
  or NULL if can't allocate memory
*/

buff
buffCreate (unsigned int size) {

	struct object * obj;  /* This will hold the return value */
	char * charPoint;

	/* if size is less than 1, set it to 1 */
	/* do this rather than error just to make our buffer code more robust */
	/* if needed we will resize later */
	if (size < 1)
		size = 1;

	/* allocate the object */
	obj = (object *) malloc (sizeof (object));

	/* if we were not able to allocate return a NULL */
	if (!obj) 
		return NULL;

	/* allocate the buffer */
	obj->buffer = (void *) malloc(size);

	if (!obj->buffer) {
		free (obj);
		return NULL;
	}

	/* keep track of size of all created buffers buffers */
	buffSize = buffSize + size;

	/* initialize values */	
	obj->head = 0;
	obj->lastHead = 0;
	obj->tail   = 0;
	obj->savedTailChar = 0;
	obj->size = size;

	obj->markHead = 0;
	obj->markTail   = 0;
	obj->markSavedTailChar = 0;
	obj->markSavedHeadChar = 0;
	obj->lastNormalizedChar = 0;

	charPoint = (char *)obj->buffer;
	charPoint[0] = 0;
	obj->lineSearchOffset = 0;

	obj->countReallocs       = 0;
	obj->countAddResetEmpty  = 0;
	obj->countAddMoveForward = 0;
	obj->countInserts        = 0;
	obj->countRetrievals     = 0;
	obj->ReallocAdjustment   = 4096;

	obj->addType = buff_default;
	obj->lineEnd = "\n";

	/* keep track of number of created buffers */
	buffCount++;

	return (buff) obj;
}

/********************************************
  to destroy a buffer that you have previously created
  given a previously allocated buff
  returns a 1 if good, 0 if failed
*/

int
buffDestroy (buff buffer) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer; 

	/* valid memory check */
	if (!obj)
		return 0;

	/* keep track of size of all created buffers buffers */
	buffSize = buffSize - obj->size;

	if (obj->buffer)
		free (obj->buffer);
	free (obj);

	/* keep track of number of created buffers */
	buffCount--;

	return 1;
}

/********************************************
  Set the kind of add to use

  given a buff and a type and a line end for normalizing line ends.

  returns a 1 if valid, 0 if failed
*/

int buffSetType(buff buffer, int type, char * lineEnd){

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;

	/* valid memory check */
	if (!obj)
		return 0;

	switch (type){

	case buff_default:
		obj->addType = buff_default;
		obj->lineEnd = "\n";
		break;

	case buff_FixCarbon:
		obj->addType = buff_FixCarbon;
		obj->lineEnd = "\r";
		break;

	case buff_NormalizeLineEnds:
		obj->addType = buff_NormalizeLineEnds;
		obj->lineEnd = lineEnd;
		break;

	default:
		return 0;

	}

	return 1;

}

/********************************************
  Get the kind of add a buffer is set to

  given a buff

  returns a 0 if failed
  otherwise return the addType.
*/

int buffGetType(buff buffer){
	
	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;

	/* valid memory check */
	if (!obj)
		return 0;

	return obj->addType;
}

/********************************************
  to add a block to the end of the buffer

  given a buff, a pointer to memory and a length

  returns a 1 if added, 0 if failed
  if you get a 0 then the current data in the buffer
  is probably still good
*/

int
buffAdd (buff buffer, char * pointer, unsigned int length) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;

	/* this allows us to access the buffer without compiler warnings */
	char * charPoint;

	/* this is the new size we need to hold the addition to the buffer */
	unsigned int newsize;
	int i, k, l;		/*  hms - removed unused vars , m, n, o, p;   */
	unsigned int j;

	/* valid memory check */
	if (!obj || !pointer)
		return 0;

	/* if we are adding less than one character, we are done */
	if (length < 1)
		return 1;
	
	/* if empty, reset ourselves back to the beginning of the buffer */
	if (obj->head && obj->head == obj->tail){

		/* accumulate statistics about buff operation */
		obj->countAddResetEmpty++;

		/* reset entries */
		obj->head = 0;
		obj->lastHead = 0;
		obj->tail = 0;
		obj->savedTailChar = 0;
		obj->lineSearchOffset = 0;

		obj->markHead = 0;
		obj->markTail   = 0;
		obj->markSavedTailChar = 0;
		obj->markSavedHeadChar = 0;

		charPoint = (char *)obj->buffer;
		charPoint[0] = 0;

	/* otherwise, if the front half of the buffer is empty */
	} else if (obj->tail > (obj->size / 2)) {

		/* accumulate statistics about buff operation */
		obj->countAddMoveForward++;

		/* copy the contents of the buffer forward */
		/* we shouldn't need to use move mem here, there shouldn't be a memory overlap */
		memcpy((char *)obj->buffer, (char *)obj->buffer+obj->tail, obj->head-obj->tail);

		/* put saved character back because we are at the beginning */
		charPoint = (char *)obj->buffer;
		charPoint[0] = obj->savedTailChar;

		/* initialize values */	
		obj->lastHead = obj->lastHead - obj->tail;
		obj->head = obj->head - obj->tail;
		obj->tail   = 0;
	}

	/* calculate new size */
	if (obj->addType == buff_NormalizeLineEnds){
		/* multiply length times 2 here to get the maximum amount we could need */
		newsize = length*2 + obj->head + 1;
	} else {
		newsize = length + obj->head + 1;
	}

	/* we are wrapping here, return to prevent a crash */
	if (newsize < length)
		return 0;

	/* check length, allocate more memory if needed */
	if ( newsize > obj->size) {

		buff temp;
	
		/* accumulate statistics about buff operation */
		obj->countReallocs++;

		/* keep track of size of all created buffers buffers */
		buffSize = buffSize - obj->size;

		/* make the buffer bigger than we really need just so we don't do this very often */
		newsize = newsize + obj->ReallocAdjustment;

		/* get a new reallocated pointer, if null the old pointer is still good */
		temp = realloc (obj->buffer, newsize);
		if (!temp) {
			/* keep track of size of all created buffers buffers */
			buffSize = buffSize + obj->size;
			return 0;
		}

		obj->buffer = temp;
		obj->size = newsize;

		/* keep track of size of all created buffers buffers */
		buffSize = buffSize + obj->size;
	}
	
	/* now copy the data into place */
	charPoint = (char *)obj->buffer + obj->head;

	if (obj->addType == buff_default) {

		memcpy((char *)obj->buffer + obj->head, pointer, length);

		/* if the buffer was empty, then store the first character for the get routines */
		if (obj->head == obj->tail)
			obj->savedTailChar = charPoint[0];

		/* set the beginning to be ready for the next write */
		obj->head = obj->head + length;
		obj->lastHead = obj->head;

		/* just for fun, but a null beyond the last character */
		charPoint[length] = 0;

	} else if (obj->addType == buff_FixCarbon){

		for (i=0; i < length; i++){
			if (pointer[i] == '\r')
				charPoint[i] = '\n';
			else
				charPoint[i] = pointer[i];
		}

		/* if the buffer was empty, then store the first character for the get routines */
		if (obj->head == obj->tail)
			obj->savedTailChar = charPoint[0];

		/* set the beginning to be ready for the next write */
		obj->head = obj->head + length;
		obj->lastHead = obj->head;

		/* just for fun, but a null beyond the last character */
		charPoint[length] = 0;

	} else if (obj->addType == buff_NormalizeLineEnds){

		i=0;
		j=0;
		l=strlen(obj->lineEnd);

		/* found a line end split between 2 inserts, skip last part */
		if ((obj->lastNormalizedChar == '\r') && (pointer[1] == '\n')) {
			j++;
			--length;
		}
	
		for (; j< length; j++){
			int lineSkip;

			lineSkip = 0;

		        // check the line ends to see if we skip 2 or 1 characters
		        if (pointer[j] == '\r')
		                if (pointer[j+1] == '\n')
		                        // found a windows line end
		                        lineSkip = 2;
		                else
		                        // found a mac line end
		                        lineSkip = 1;
		        else if (pointer[j] == '\n')
		                // found a mac or unix line end
		                lineSkip = 1;

			switch (lineSkip){
				case 2:
					j++;
				case 1:
					for (k=0;k<l;k++)
						charPoint[i+k] = obj->lineEnd[k];
					i = i + l;
					break;
				case 0:
					charPoint[i++] = pointer[j];
					break;
			}
		}
	
		obj->lastNormalizedChar = charPoint[i-1];
	
		/* if the buffer was empty, then store the first character for the get routines */
		if (obj->head == obj->tail)
			obj->savedTailChar = charPoint[0];
	
		/* set the beginning to be ready for the next write */
		obj->head = obj->head + i;
		obj->lastHead = obj->head;
	
		/* just for fun, but a null beyond the last character */
		charPoint[i] = 0;

	}

	/* accumulate statistics about buff operation */
	obj->countInserts++;

	return 1;
}

void
CheckAndResize (buff buffer){

	/* if the buffer is more than 4 times bigger than it's contents, reduce it to twice the content size */
	unsigned int buffsize = 0;
	unsigned int contentlength = 0;

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;

	/* if a null pointer or the buffer is empty */
	if (!obj || obj->head == obj->tail) {
		return;
	}

	// return immediately if the buffer is marked
	// a marked buffer isn't really releasing anything yet.
	if (obj->markHead)
		return;


	buffsize = buffGetSize (obj);
	contentlength = buffGetLength (obj);

	if (contentlength < obj->ReallocAdjustment)
		contentlength = obj->ReallocAdjustment;

	if ( contentlength < (buffsize/4) )
		buffResize (obj, contentlength*2);
}


/********************************************

  to get a single line from the buffer

  give a buff, and pointer to a pointer

  return length found, or 0 if nothing to find
  pointer is pointed at beginning of string to send, or NULL if error
*/

unsigned int
buffGetLine (buff buffer, char ** pointer) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;

	/* resize buffer as needed if it has grown too big */
	CheckAndResize(obj);

	/* this allows us to access the buffer without compiler warnings */
	char * charPoint;

	/* keep track of the return value, which is the actual length returned */
	unsigned int actualLength = 0;

	/* these two are used in the search for newlines */
	unsigned int i;
	int lineSkip = 0;

	/* if a null pointer in the return string pointer */
        if (!pointer) {
                return 0;
        }
	
	/* if a null pointer or the buffer is empty */
	if (!obj || obj->head == obj->tail) {
		*pointer = "";
		return 0;
	}

	/* put saved character back */
	charPoint = (char *)obj->buffer + obj->tail;
	charPoint[0] = obj->savedTailChar;

	/* point to the beginning */
	*pointer = (char *)obj->buffer + obj->tail;

	/* How big is the buffer? */
	actualLength = obj->head - obj->tail;
	
	/* start searching from the lineSearchOffset */
	/* this is an optimization to keep us from having to repeatedly */
	/* look through the same block of code that doesn't have a line end */
	for (i = obj->lineSearchOffset; i < actualLength; i++) {

                // check the line ends to see if we skip 2 or 1 characters
                if (charPoint[i] == '\r')
                        if (charPoint[i+1] == '\n')
                                // found a windows line end
                                lineSkip = 2;
                        else
                                // found a mac line end
                                lineSkip = 1;
                else if (charPoint[i] == '\n')
                        // found a mac or unix line end
                        lineSkip = 1;

		if (lineSkip)
			break;
	}

	/* if we get a lineSkip we found a lineend */
	if (lineSkip)
		actualLength = i + lineSkip;
	else {
		/* we didn't find a line end */
		/* only return complete lines with terminating line end */
		/* to force a fragment out, use getbuffer */
		obj->lineSearchOffset = i;
		*pointer = "";
		return 0;
	}

	obj->tail = obj->tail + actualLength;
	obj->lineSearchOffset = 0;

	/* terminate the string and save the character that was there */
	obj->savedTailChar = charPoint[actualLength];
	charPoint[actualLength] = 0;
	
	/* accumulate statistics about buff operation */
	obj->countRetrievals++;
	
	return actualLength;
}

/********************************************

  to get a block of text from the tail of the buffer 

  give a buff, a pointer to a pointer, and a maximum length

  return length found, or 0 if nothing to find 
  pointer is pointed at beginning of block to send, or NULL if error
  
  0 - start of buffer
  T - tail
  H - head
  P - pointer returned
  # - end of buffer
  
  0------T-----------H------#
  
  *T == 0 (character from *T saved)
  restore *T
  
		P
  0-----------T------H------#
  
  set P to old T
  change T
  save character at new *T
  replace *T with 0
  return P
  
*/

unsigned int
buffGetBlockFromTail (buff buffer, char ** pointer, unsigned int length) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;

	/* this allows us to access the buffer without compiler warnings */
	char * charPoint;

	/* resize buffer as needed if it has grown too big */
	CheckAndResize(obj);

	/* keep track of the return value, which is the actual length returned */
	unsigned int actualLength = 0;

	/* if a null pointer in the return string pointer */
        if (!pointer) {
                return 0;
        }
	
	/* if a null pointer or the buffer is empty or they requested less than one byte*/
	if (!obj || obj->head == obj->tail || length < 1) {
		*pointer = "";
		return 0;
	}

	/* put saved character back */
	charPoint = (char *)obj->buffer + obj->tail;
	charPoint[0] = obj->savedTailChar;

	/* point to the beginning */
	*pointer = charPoint;

	/* How big is the buffer? */
	actualLength = obj->head - obj->tail;
	
	/* if the buffer is bigger than the requested amount */
	if (length < actualLength)
		actualLength = length;

	obj->tail = obj->tail + actualLength;
	obj->lineSearchOffset = 0;

	/* terminate the string and save the character that was there */
	charPoint = (char *)obj->buffer + obj->tail;
	obj->savedTailChar = charPoint[0];
	charPoint[0] = 0;
	
	/* accumulate statistics about buff operation */
	obj->countRetrievals++;

	/* keep track of number of created buffers */
	return actualLength;
}

/********************************************

  to get a block of text from the head of the buffer 

  give a buff, a pointer to a pointer, and a maximum length

  return length found, or 0 if nothing to find 
  pointer is pointed at beginning of block to send, or NULL if error
  
  0 - start of buffer
  T - tail
  H - head
  P - pointer returned
  L - last head
  # - end of buffer
  
  0------T-----------H--L---#
  
  *L == 0 (character from *L saved)
  restore *L
                  P    
  0------T--------H--L-----#
  
  save character at old *H
  save last H as L
  replace *H with 0
  change H
  set P to H
  return P
  
*/

unsigned int
buffGetBlockFromHead (buff buffer, char ** pointer, unsigned int length) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;

	/* this allows us to access the buffer without compiler warnings */
	char * charPoint;

	/* resize buffer as needed if it has grown too big */
	CheckAndResize(obj);

	/* keep track of the return value, which is the actual length returned */
	unsigned int actualLength = 0;

	/* if a null pointer in the return string pointer */
        if (!pointer) {
                return 0;
        }
	
	/* if a null pointer or the buffer is empty or they requested less than one byte*/
	if (!obj || obj->head == obj->tail || length < 1) {
		*pointer = "";
		return 0;
	}

	/* put saved character back */
	charPoint = (char *)obj->buffer + obj->lastHead;
	charPoint[0] = obj->savedHeadChar;

	/* save character at what will become the last head and replace it with 0 */
	charPoint = (char *)obj->buffer + obj->head;
	obj->savedHeadChar = charPoint[0];
	charPoint[0] = 0;

	/* How big is the buffer? */
	actualLength = obj->head - obj->tail;
	
	/* if the buffer is bigger than the requested amount */
	if (length < actualLength)
		actualLength = length;

	obj->lastHead = obj->head;
	obj->head = obj->head - actualLength;
	obj->lineSearchOffset = 0;

	/* point to the beginning */
	*pointer = (char *)obj->buffer + obj->head;

	/* accumulate statistics about buff operation */
	obj->countRetrievals++;

	/* keep track of number of created buffers */
	return actualLength;
}

/********************************************

  to get the entire contents of the buffer

  give a buff, a pointer to a pointer

  return length found, or 0 if nothing to find 
  pointer is pointed at beginning of block to send, or NULL if error
*/

unsigned int
buffGetBuffer (buff buffer, char ** pointer) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;

	/* this allows us to access the buffer without compiler warnings */
	char * charPoint;

	/* resize buffer as needed if it has grown too big */
	CheckAndResize(obj);

	/* keep track of the return value, which is the actual length returned */
	unsigned int actualLength = 0;

	/* if a null pointer in the return string pointer */
        if (!pointer) {
                return 0;
        }
	
	/* if a null pointer or the buffer is empty or they requested less than one byte*/
	if (!obj || obj->head == obj->tail) {
		*pointer = "";
		return 0;
	}

	/* put saved character back */
	charPoint = (char *)obj->buffer + obj->tail;
	charPoint[0] = obj->savedTailChar;

	/* point to the beginning */
	*pointer = charPoint;

	/* How big is the buffer? */
	actualLength = obj->head - obj->tail;
	obj->lineSearchOffset = 0;
	
	/* set the buffer to be empty */
	obj->tail = obj->head;
	
	/* accumulate statistics about buff operation */
	obj->countRetrievals++;

	return actualLength;
}


/********************************************

  to clear the buffer 

  given a buff, reset all internal pointers to empty state 

  return a 0 if a NULL object
  return a 1 if succeeded
*/

int
buffClear (buff buffer) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;

	/* this allows us to access the buffer without compiler warnings */
	char * charPoint;
	
	/* valid memory check */
	if (!obj)
		return 0;

	/* reset entries */
	obj->head = 0;
	obj->lastHead = 0;
	obj->tail   = 0;
	obj->savedTailChar = 0;
	
	obj->markHead            = 0;
	obj->markTail            = 0;
	obj->markSavedTailChar   = 0;
	obj->markSavedHeadChar   = 0;
	obj->lastNormalizedChar  = 0;
	
	obj->lineSearchOffset    = 0;
	
	obj->countReallocs       = 0;
	obj->countAddResetEmpty  = 0;
	obj->countAddMoveForward = 0;
	obj->countInserts        = 0;
	obj->countRetrievals     = 0;

	/* keep track of number of created buffers */
	charPoint = (char *)obj->buffer;
	charPoint[0] = 0;

	return 1;
}

/********************************************

  to get the size of a buffer

  given a buff

  return current allocated size
*/

unsigned int
buffGetSize (buff buffer) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;
	
	/* valid memory check */
	if (!obj)
		return 0;

	return obj->size;
}

/********************************************

  to get the length of the contents of the buffer

  given a buff

  return the length of the contents of the buffer
*/

unsigned int
buffGetLength (buff buffer) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;
	
	/* valid memory check */
	if (!obj)
		return 0;

	return obj->head - obj->tail;
}

/********************************************

  to get the count of the times that the add routine has moved a buffer forward

  given a buff

  return the count
*/

unsigned int
buffCountMovedForward (buff buffer) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;
	
	/* valid memory check */
	if (!obj)
		return 0;

	return obj->countAddMoveForward;
}

/********************************************

  to get the count of the times that the add routine has reset an empty buffer

  given a buff

  return the count
*/

unsigned int
buffCountResetEmpty (buff buffer) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;
	
	/* valid memory check */
	if (!obj)
		return 0;

	return obj->countAddResetEmpty;
}

/********************************************

  to get the count of the number of reallocs

  given a buff

  return the count
*/

unsigned int
buffCountReallocs (buff buffer) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;
	
	/* valid memory check */
	if (!obj)
		return 0;

	return obj->countReallocs;
}

/********************************************

  to get the count of the inserts

  given a buff

  return the count
*/

unsigned int
buffCountInserts (buff buffer) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;
	
	/* valid memory check */
	if (!obj)
		return 0;

	return obj->countInserts;
}

/********************************************

  to get the count of the number of times data has been retrieved

  given a buff

  return the count
*/

unsigned int
buffCountRetrievals (buff buffer) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;
	
	/* valid memory check */
	if (!obj)
		return 0;

	return obj->countRetrievals;
}

/********************************************

  to get the count of the number of times data has been retrieved

  given a buff

  return the count
*/

unsigned int
buffTotalCount (buff buffer) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;
	
	/* valid memory check */
	if (!obj)
		return 0;

	return buffCount;
}

/********************************************

  to get the total size of all allocated buffers

  given a buff

  return the total allocated buff sizes
*/

unsigned int
buffTotalSize (buff buffer) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;
	
	/* valid memory check */
	if (!obj)
		return 0;

	return buffSize;
}

/********************************************

  change the default offset of additional memory allocated when we realloc the buffer

  given a buff and a newSize for the realloc adustment
  default is 4096
  max value is (2^20)MB

  return 1 on success, 0 if new size is out of rante, ot the buffer is NULL
*/

unsigned int
buffReallocAdjustment (buff buffer, unsigned int newSize) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;
	
	/* valid memory check */
	if (!obj)
		return 0;

	obj->ReallocAdjustment = newSize;

	return 1;
}

/********************************************
  to resize the block to the given length, or the size of the contents, which ever is bigger

  given a buff, a pointer to memory and a length

  returns a 1 if succeed, 0 if failed
*/

int
buffResize (buff buffer, unsigned int length) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;

	/* this allows us to access the buffer without compiler warnings */
	char * charPoint;

	/* holds the return from malloc until we know if we want to let go of the old value */
	buff temp;
	
	/* this is the new size we need to hold the addition to the buffer */
	unsigned int newsize;

	/* valid memory check */
	if (!obj)
		return 0;

	/* if empty, reset ourselves back to the beginning of the buffer */
	if (obj->head && obj->head == obj->tail){

		/* accumulate statistics about buff operation */
		obj->countAddResetEmpty++;

		/* reset entries */
		obj->head = 0;
		obj->lastHead = 0;
		obj->tail = 0;
		obj->savedTailChar = 0;
		obj->lineSearchOffset = 0;

		charPoint = (char *)obj->buffer;
		charPoint[0] = 0;

	/* otherwise, always move everything forward */
	} else {

		/* accumulate statistics about buff operation */
		obj->countAddMoveForward++;

		/* copy the contents of the buffer forward */
		/* we shouldn't need to use move mem here, there shouldn't be a memory overlap */
		memcpy((char *)obj->buffer, (char *)obj->buffer+obj->tail, obj->head-obj->tail);

		/* put saved character back because we are at the beginning */
		charPoint = (char *)obj->buffer;
		charPoint[0] = obj->savedTailChar;

		/* initialize values */	
		obj->lastHead = obj->lastHead - obj->tail;
		obj->head = obj->head - obj->tail;
		obj->tail   = 0;
	}

	/* calculate new size */
	if (length < obj->size)
		newsize = obj->size;
	else
		newsize = length;

	/* accumulate statistics about buff operation */
	obj->countReallocs++;

	/* keep track of size of all created buffers buffers */
	buffSize = buffSize - obj->size;

	/* get a new reallocated pointer, if null the old pointer is still good */
	temp = realloc (obj->buffer, newsize);
	if (!temp) {
		/* keep track of size of all created buffers buffers */
		buffSize = buffSize + obj->size;
		return 0;
	}

	obj->buffer = temp;
	obj->size = newsize;

	/* keep track of size of all created buffers buffers */
	buffSize = buffSize + obj->size;
	
	return 1;
}

/********************************************

  to mark the current position in the buffer

  give a buff

  return 0;
*/

unsigned int
buffMark (buff buffer) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;

	/* valid memory check */
	if (!obj)
		return 0;
		
	//  save the position of the head, tail and the saved character stuff

	obj->markHead = obj->head;
	obj->markLastHead = obj->lastHead;
	obj->markTail = obj->tail;
	
	obj->markSavedTailChar = obj->savedTailChar;
	obj->markSavedHeadChar = obj->savedHeadChar;
	
	return 0;
}

/********************************************

  to rewind to the mark in the buffer

  give a buff

  return 0;
*/

unsigned int
buffRewindToMark (buff buffer) {
	char*		charPoint;

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;

	/* valid memory check */
	if (!obj)
		return 0;

	/* put saved characters back */
	
	charPoint = (char *)obj->buffer + obj->lastHead;
	charPoint[0] = obj->savedHeadChar;

	charPoint = (char *)obj->buffer + obj->tail;
	charPoint[0] = obj->savedTailChar;

	//  restore saved values

	obj->head = obj->markHead;
	obj->lastHead = obj->markLastHead;
	obj->tail = obj->markTail;
	
	obj->savedTailChar = obj->markSavedTailChar;
	obj->savedHeadChar = obj->markSavedHeadChar;
	
	obj->markHead = 0;
	obj->markLastHead = 0;
	obj->markTail = 0;
	
	obj->markSavedTailChar = 0;
	obj->markSavedHeadChar = 0;

	return 0;
}


void BuffUnmark(buff buffer) {

	/* dereference from a handle to an object pointer */
	struct object * obj = (object *) buffer;

	/* valid memory check */
	if (!obj)
		return;

	obj->markHead = 0;
	obj->markLastHead = 0;
	obj->markTail = 0;
	
	obj->markSavedTailChar = 0;
	obj->markSavedHeadChar = 0;

}
