#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "buff.h"


/*
  queue 


	Each queue uses two buffers

	one buffer stores the size of the inserted items
	ever element in this buffer is a standard size.

	the second buffer stores the data 

	All retrevials are done in O(1).  All inserts are done in O(N).
*/



#define kDefaultNumElements			100
#define kDefaultElementSize			1024

typedef struct queue {
	buff		sizeBuffer;
	buff		dataBuffer;
	unsigned int	lastDataSize;
	
	//  statistics
	
	unsigned int	numPushes;
	unsigned int	numPopsLIFO;
	unsigned int	numPopsFIFO;

	unsigned int	LastFailedSize;
	unsigned int 	FailedCount;
	
} queue, *queuePtr;

typedef struct SizeType {
	unsigned int	size;
	unsigned int	type;
} SizeType, *SizeTypePtr;


/********************************************
  to destroy a queue that you have previously created
  given a previously allocated queue
  returns a 1 if good, 0 if failed
*/

int
queueDestroy (queuePtr queue) {

	if (queue != NULL) {

		if (queue->dataBuffer != NULL) {
			buffDestroy(queue->dataBuffer);
			queue->dataBuffer = NULL;
		}

		if (queue->sizeBuffer != NULL) {
			buffDestroy(queue->sizeBuffer);
			queue->sizeBuffer = NULL;
		}

		// added a free here and set the pointer to NULL.
		free(queue);
		queue = NULL;
	}

	return 1;
}


/********************************************
  to create a new queue
  returns a pointer to an allocated queue
  or NULL if can't allocate memory
*/

queuePtr
queueCreateWithSize (unsigned int numElements, unsigned int elementSize) {
	queuePtr		qPtr;

	//  a queue is made up of two buffers. the first one stores the data
	//  and the 2nd one stores the size of each piece of data.
	
	qPtr = malloc(sizeof(queue));
	if (qPtr != NULL) {
	
		qPtr->dataBuffer = buffCreate(numElements * elementSize);
		qPtr->sizeBuffer = buffCreate(numElements * sizeof(SizeType));
		
		buffReallocAdjustment (qPtr->dataBuffer, numElements * elementSize);
		buffReallocAdjustment (qPtr->sizeBuffer, numElements * sizeof(SizeType));
		
		qPtr->lastDataSize = 0;
		
		qPtr->numPushes = 0;
		qPtr->numPopsLIFO = 0;
		qPtr->numPopsFIFO = 0;

		//  if we cannot allocate both buffer, destroy everything and bail out
		
		if ((qPtr->dataBuffer == NULL) || (qPtr->sizeBuffer == NULL))
			queueDestroy(qPtr);
	}

	return qPtr;
}

// moved this down a function so it could see queueCreateWithSize()
queuePtr
queueCreate () {

	//  create the queue with the default size

	return queueCreateWithSize( kDefaultNumElements, kDefaultElementSize );
}


/********************************************
  to place an event onto the end of the queue

  given a queue, a pointer to memory and a length

  returns a 1 if added, 0 if failed
  if you get a 0 then the current data in the queue
  is still good
*/

int
queuePush (queuePtr queue, char * pointer, unsigned int length, unsigned int type) {
	SizeType	sizeType;
	int		result = 0;
	int		ReallocSize;

	double		FreePercentage;
	double		SizeOfBuffer;
	double		SizeOfData;

	//  we need to put the data in the data buffer and put the size into the size buffer
	
	if (queue != NULL) {
	
		//  check and make sure the grow size is correct on the data buffer. We always want the
		//  data buffer to double in size

		ReallocSize = buffGetSize( queue->dataBuffer );

		if (ReallocSize > 10000000)
			ReallocSize = 10000000;

		SizeOfBuffer	= buffGetSize(queue-> dataBuffer);
		SizeOfData	= buffGetLength(queue->dataBuffer);
		FreePercentage	= SizeOfData / SizeOfBuffer;

		// if we have failed and are using more than half the buffer
		if ( queue->FailedCount  && (FreePercentage > .49))
			return 0;
		
		buffReallocAdjustment( queue->dataBuffer, ReallocSize );
			
		buffAdd (queue->dataBuffer, pointer, length);

		if ( !result ) {

			int buffsize;

			// only try to resize if resizing would give us enough room to fit

			buffsize = buffGetSize(queue->dataBuffer) - buffGetLength(queue->dataBuffer) + length;

			if (buffsize) {
				// if we failed to add, then try to recover some memory that might be in buffer
				buffResize (	queue->dataBuffer, 1);

				//then retry adding

				result = buffAdd (queue->dataBuffer, pointer, length);

				if(!result)
					queue->FailedCount = 1;
			}

			if (result) {
				sizeType.size = length;
				sizeType.type = type;

				result = buffAdd (queue->sizeBuffer, (char*) &sizeType, sizeof(SizeType));

				if (result) {
					queue->numPopsLIFO++;
					result = 1;
				} else {
					// remove the first thing
					// needs done
				}
			}
		}
	}

	return result;
}


/********************************************

  pops an event from the queue, last in first out

  give a queue, a pointer to a block, a pointer to a length, and a pointer to a type

  returns a 1 if was able to pop, 0 if failed
  pointer is pointed at beginning of block to send, or NULL if error
*/

int
queuePopLIFO (queuePtr queue, char ** pointer, unsigned int * length, unsigned int * type) {
	SizeTypePtr	sizeType;
	int		result = 0;
	unsigned int	blockSizeResult;
	
	if (queue != NULL) {

		//  we need to get the size and type of the last element added to the buffer
	
		blockSizeResult = buffGetBlockFromHead (queue->sizeBuffer, (char**) &sizeType, sizeof(SizeType));
		if (blockSizeResult == sizeof(SizeType)) {
		
			//  we got a size and type for the block of data
			
			blockSizeResult = buffGetBlockFromHead (queue->dataBuffer, pointer, sizeType->size);
			if (blockSizeResult == sizeType->size) {
			
				//  we got the block and it is the right size
				
				*length = sizeType->size;
				*type = sizeType->type;
				
				queue->numPopsLIFO++;

				result = 1;
			
			}
		
		}
		
	}
	
	return result;
}


/********************************************

  pops an event from the queue, first in first out

  give a queue, a pointer to a block, a pointer to a length, and a pointer to a type

  returns a 1 if was able to pop, 0 if failed
  pointer is pointed at beginning of block to send, or NULL if error
*/

int
queuePopFIFO (queuePtr queue, char ** pointer, unsigned int * length, unsigned int * type) {
	SizeTypePtr	sizeType;
	int		result = 0;
	unsigned int	blockSizeResult;
	
	if (queue != NULL) {

		//  we need to get the size and type of the last element added to the buffer
	
		blockSizeResult = buffGetBlockFromTail (queue->sizeBuffer, (char**) &sizeType, sizeof(SizeType));
		if (blockSizeResult == sizeof(SizeType)) {
		
			//  we got a size and type for the block of data
			
			blockSizeResult = buffGetBlockFromTail (queue->dataBuffer, pointer, sizeType->size);
			if (blockSizeResult == sizeType->size) {
			
				//  we got the block and it is the right size
				
				*length = sizeType->size;
				*type = sizeType->type;
				
				queue->numPopsFIFO++;

				result = 1;
			
			}
		
		}
		
	}
	
	return result;
}


/********************************************

  undo the previous pop

  to do a peek you must first Pop and then PopUndo

  given a queue

  return 0 if we can't undo the last pop
  return 1 if we were able to undo the last pop

  Only backs up 1 event.
  Not valid after a Push.
*/

int
queuePopUndo (queuePtr queue) {
	int		result = 0;

	if ((queue != NULL) && (queue->lastDataSize)) {
	
		//  HMS - this is not implemented yet. We need to somehow undo LIFO and FIFO pops
	
	}
	
	return result;
}


/********************************************

  to clear the queue 

  given a queue, reset all internal pointers to empty state 

  return a 0 if a NULL object
  return a 1 if succeeded
*/

int
queueClear (queuePtr queue) {
	int		result = 0;
	
	if (queue != NULL) {
	
		buffClear(queue->sizeBuffer);
		buffClear(queue->dataBuffer);
		
		queue->lastDataSize = 0;
	
		queue->numPushes = 0;
		queue->numPopsLIFO = 0;
		queue->numPopsFIFO = 0;
	
		result = 1;
	
	}
	
	return result;
}


/********************************************

  to get the number of entries in a Q

  given a queue

  return current number of entries.
*/

unsigned int
queueCountEntries (queuePtr queue) {
	unsigned int	numEntries = 0;

	if (queue != NULL) {
	
		//  the size queue has entries which are all the same size
	
		numEntries = buffGetLength(queue->sizeBuffer);
		numEntries /= sizeof(SizeType);
		
	}
	
	return numEntries;
}


/********************************************

  to get the size of a queue

  given a queue

  return current allocated size
*/

unsigned int
queueGetSize (queuePtr queue) {
	unsigned int	queueSize = 0;

	if (queue != NULL) {
	
		queueSize = buffGetSize(queue->sizeBuffer) + buffGetSize(queue->dataBuffer);
		
	}
	
	return queueSize;
}


/********************************************

  to get the count of the times that we have pushed into the queue

  given a queue

  return the count
*/

unsigned int
queueCountPushes (queuePtr queue) {
	unsigned int	numPushes = 0;

	if (queue != NULL) {
		numPushes = queue->numPushes;
	}
	
	return numPushes;
}


/********************************************

  to get the count of the times popLIFO was successfully called.

  given a queue

  return the count
*/

unsigned int
queueCountPopLIFO (queuePtr queue) {
	unsigned int	numPopsLIFO = 0;

	if (queue != NULL) {
		numPopsLIFO = queue->numPopsLIFO;
	}
	
	return numPopsLIFO;
}


/********************************************

  to get the count of the times PopFIFO was successfully called.

  given a queue

  return the count
*/

unsigned int
queueCountPopFIFO (queuePtr queue) {
	unsigned int	numPopsFIFO = 0;

	if (queue != NULL) {
		numPopsFIFO = queue->numPopsFIFO;
	}
	
	return numPopsFIFO;
}


/********************************************
  to resize the internal data sizes to the given amount

  given a queue, a pointer to memory and a length

  returns a 1 if succeed, 0 if failed
*/

int
queueResize (queuePtr queue, unsigned int entryLength, unsigned int bufferLength) {

	buffResize (	queue->dataBuffer, entryLength*bufferLength);

	return 0;
}


int
queueMark(queuePtr queue) {

	if (queue != NULL) {
		buffMark(queue->sizeBuffer);
		buffMark(queue->dataBuffer);
	}
	
	return 0;
}


int
queueRewindToMark(queuePtr queue) {

	if (queue != NULL) {
		buffRewindToMark(queue->sizeBuffer);
		buffRewindToMark(queue->dataBuffer);
	}
	
	return 0;
}


