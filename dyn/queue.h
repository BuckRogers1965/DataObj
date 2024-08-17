/*
  Dynamic Queue 
*/

#pragma once

//#define kDefaultDataBufferSize  (100*1024)
//#define kDefaultSizeBufferSize  (100*8)

typedef void * queuePtr;

/********************************************
  to create a new queue
  returns a pointer to an allocated queue
  or NULL if can't allocate memory
*/

queuePtr
queueCreate ();

/********************************************
  to create a new queue
  returns a pointer to an allocated queue
  or NULL if can't allocate memory
  
  this version allows creation of a queue at a specific size. the
  size of the buffer that will be created is numElements * elementSize.
  
  The size of the size buffer will be numElements * sizeof(SizeType)
*/

queuePtr
queueCreateWithSize ( unsigned int numElements, unsigned int elementSize );

/********************************************
  to destroy a queue that you have previously created
  given a previously allocated queue
  returns a 1 if good, 0 if failed
*/

int
queueDestroy (queuePtr queue);

/********************************************
  to place an event onto the end of the queue

  given a queue, a pointer to memory and a length

  returns a 1 if added, 0 if failed
  if you get a 0 then the current data in the queue
  is still good
*/

int
queuePush (queuePtr queue, char * pointer, unsigned int length, unsigned int type);

/********************************************

  pops an event from the queue, last in first out

  give a queue, a pointer to a block, a pointer to a length, and a pointer to a type

  returns a 1 if was able to pop, 0 if failed
  pointer is pointed at beginning of block to send, or NULL if error
*/

int
queuePopLIFO (queuePtr queue, char ** pointer, unsigned int * length, unsigned int * type);

/********************************************

  pops an event from the queue, first in first out

  give a queue, a pointer to a block, a pointer to a length, and a pointer to a type

  returns a 1 if was able to pop, 0 if failed
  pointer is pointed at beginning of block to send, or NULL if error
*/

int
queuePopFIFO (queuePtr queue, char ** pointer, unsigned int * length, unsigned int * type);

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
queuePopUndo (queuePtr queue);

/********************************************

  to clear the queue 

  given a queue, reset all internal pointers to empty state 

  return a 0 if a NULL object
  return a 1 if succeeded
*/

int
queueClear (queuePtr queue);

/********************************************

  to get the number of entries in a Q

  given a queue

  return current number of entries.
*/

unsigned int
queueCountEntries (queuePtr queue);

/********************************************

  to get the size of a queue

  given a queue

  return current allocated size
*/

unsigned int
queueGetSize (queuePtr queue);

/********************************************

  to get the count of the times that we have pushed into the queue

  given a queue

  return the count
*/

unsigned int
queueCountPushes (queuePtr queue);

/********************************************

  to get the count of the times popLIFO was successfully called.

  given a queue

  return the count
*/

unsigned int
queueCountPopLIFO (queuePtr queue);

/********************************************

  to get the count of the times PopFIFO was successfully called.

  given a queue

  return the count
*/

unsigned int
queueCountPopFIFO (queuePtr queue);

/********************************************
  to resize the internal data sizes to the given amount

  given a queue, a pointer to memory and a length

  returns a 1 if succeed, 0 if failed
*/

int
queueResize (queuePtr queue, unsigned int entryLength, unsigned int bufferLength);

int
queueMark(queuePtr queue);

int
queueRewindToMark(queuePtr queue);

