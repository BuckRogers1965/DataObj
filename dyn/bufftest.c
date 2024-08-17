
#include "buff.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define LOOP 10000

#define STRING_ENTRIES 5

int BuffTest (){

	buff buffer;
	int i, j, k, length;
	char * resultString;
	int resultLength;

	/**********************/

	char tStr[STRING_ENTRIES][10000] = {
		"test\n",
		"01234567890\n",
		"abcdefghijklmnopqrstuvwxyz\n",
		"This is a very very very very very very very very very very very long line.\n",
		"<event originatingID=\"12345\",UnityEventID=\"3434346347\",UnityOriginator=\"QA Server\",EventCategory=\"NA\",UnityEventCategory=\"NetworkEvent\",PhysicalSource=\"Unitytest.singlestep.com\",OriginatingTime=\"05/31/2002 00:44:02\",Message=\"\",Severity=\"3\",Priority=\"1\",Assignee=\"None\",Count=\"1\",Description=\"new event 7 Something very very very very very very very naughty happenned on the network.\",State=\"UP\",Status=\"3\",Path=\"/EventGen-1.1!DBI!\",GenericKey=\"UnityStartingPolicyOutput\"/>\n"
	};

	unsigned int tStrLen[STRING_ENTRIES];

	for (k=0; k < STRING_ENTRIES; k++)
		tStrLen[k] = strlen(tStr[k]);

	/**********************/

	if ( buffTotalCount (NULL)) {
		printf("T01: Failed the total count should be equal to 0 \n");
	}

	if ( buffTotalSize (NULL)) {
		printf("T02: Failed the total size would be equal to 0 \n");
	}

	printf("Creating a buffer of 0 bytes (will really create a buffer 1 byte long\n");
	buffer = buffCreate (0);
	if (!buffer) {
		printf("T03: Could not buffCreate (0).\n");
		exit (1);
	}

	if ( buffTotalCount () != 1) {
		printf("T04: Failed should be 1 \n");
		//exit (1);
	}

	if ( buffTotalSize () != 1) {
		printf("T05: Failed Should be 1 \n");
		exit (1);
	}

	/**********************/

	if ( buffCountResetEmpty (buffer)) {
		printf("Y39: Failed Should be zero \n");
		exit (1);
	}

	if ( buffCountMovedForward (buffer)) {
		printf("Y37: Failed Should be zero \n");
		exit (1);
	}

	if ( buffCountReallocs (buffer)) {
		printf("Y41: Failed Should be zero \n");
		exit (1);
	}

	if ( buffCountInserts (buffer)) {
		printf("Y43: Failed  Should be zero\n");
		exit (1);
	}

	if ( buffCountRetrievals (buffer)) {
		printf("Y45: Failed  Should be zero\n");
		exit (1);
	}

	/**********************/

	if ( buffDestroy (NULL)) {
		printf("A02: Failed buffDestroy must return a 0 if given a NULL pointer.\n");
		exit (1);
	}

	/**********************/

	if ( buffAdd (NULL, "test", 5)) {
		printf("A03: Failed buffAdd must return a 0 if given a NULL pointer. \n");
		exit (1);
	}

	if (!buffAdd (buffer, "test", 0)) {
		printf("A04: Failed  \n");
		exit (1);
	}

	if ( buffAdd (buffer, "test", -1)) {
		printf("A05: Failed  \n");
		exit (1);
	}

	/* no real idea how to detect this */
	/* was crashing me later */
/*	if (!buffAdd (buffer, "", 5)) {
		printf("A06: Failed buffAdd(buffer, "", 5) should not work but does. \n");
		exit (1);
	}
*/

	if ( buffAdd (buffer, NULL, 5)) {
		printf("A07: Failed  must return a 0 if given a NULL pointer.\n");
		exit (1);
	}

	if ( buffGetLine (NULL, &resultString)) {
		printf("A08: Failed  must return a 0 if given a NULL pointer.\n");
		exit (1);
	}

	if ( buffGetLine (buffer, NULL)) {
		printf("A09: Failed  must return a 0 if given a NULL pointer.\n");
		exit (1);
	}

	/**********************/

	buffClear (buffer);
	if (!buffAdd (buffer, "\ntesta\rtestbb\r\ntestccc", 22)) {
		printf("A10: Failed  buffAdd should return a 1 on successful insert.\n");
		exit (1);
	}

	if ( (length = buffGetLine (buffer, &resultString)) != 1) {
		printf("A11: Failed buffGetLine() should return a 1 and a \\n here.\n");
		exit (1);
	}

	if (!memcmp("\n", &resultString, length)) {
		printf("A11a: Failed should be '%x' here, found '%x'.\n", '\n', *resultString);
		exit (1);
	}

	if ( (length = buffGetLine (buffer, &resultString)) != 6) {
		printf("A12: Failed buffGetLine() should return a 6 and a 'testa\\r' here.\n");
		exit (1);
	}

	if (!memcmp("testa\r", &resultString, length)) {
		printf("A12a: Failed should be 'testa\\r' here, found '%s'.\n", resultString);
		exit (1);
	}

	if ( (length = buffGetLine (buffer, &resultString)) != 8 ) {
		printf("A13: Failed buffGetLine() should return an 8 and a 'testbb\\r\\n' here.\n");
		exit (1);
	}

	if (!memcmp("testbb\r\n", &resultString, length)) {
		printf("A13a: Failed should be 'testbb\r\n' here, found '%s'.\n", resultString);
		exit (1);
	}

	if ( (length = buffGetLine (buffer, &resultString)) ) {
		printf("A14: Failed buffGetLine() should return a 0 and a '' here. found %s of len %d\n", resultString, length);
		exit (1);
	}

	if ( memcmp("", &resultString, length)) {
		printf("A14a: Failed should be '' here found '%s'.\n", resultString);
		exit (1);
	}

	if (!buffAdd (buffer, "\n", 1)) {
		printf("A14b: Failed  buffAdd should return a 1 on successful insert.\n");
		exit (1);
	}

	if ( (length = buffGetLine (buffer, &resultString)) != 8 ) {
		printf("A14c: Failed buffGetLine() should return an 8 and a 'testccc\\n' here.\n");
		exit (1);
	}

	if (!memcmp("testccc\n", &resultString, length)) {
		printf("A14d: Failed should be 'testccc\n' here, found '%s'.\n", resultString);
		exit (1);
	}

	if ( buffGetLine (buffer, &resultString)) {
		printf("A15: Failed buffGetLine() should return a 0 in an empty buffer.\n");
		exit (1);
	}

	/**********************/

	if ( buffGetBlockFromTail(NULL, &resultString, 5)) {
		printf("A16: Failed  \n");
		exit (1);
	}

	if ( buffGetBlockFromTail(buffer, NULL, 5)) {
		printf("A17: Failed  \n");
		exit (1);
	}

	if (!buffAdd (buffer, "test", 4)) {
		printf("A18: Failed  \n");
		exit (1);
	}

	if (!buffGetBlockFromTail(buffer, &resultString, 4)) {
		printf("A19: Failed  \n");
		exit (1);
	}

	if (!buffAdd (buffer, "test", 4)) {
		printf("A20: Failed  \n");
		exit (1);
	}

	if (!buffGetBlockFromTail(buffer, &resultString, 3)) {
		printf("A21: Failed  \n");
		exit (1);
	}

	if (!buffAdd (buffer, "test", 4)) {
		printf("A22: Failed  \n");
		exit (1);
	}

	if (!buffGetBlockFromTail(buffer, &resultString, 10)) {
		printf("A23: Failed  \n");
		exit (1);
	}

	/**********************/

	if ( buffGetBlockFromHead (NULL, &resultString, 5)) {
		printf("A16: Failed  \n");
		exit (1);
	}

	if ( buffGetBlockFromHead (buffer, NULL, 5)) {
		printf("A17: Failed  \n");
		exit (1);
	}

	if (!buffAdd (buffer, "test", 4)) {
		printf("A18: Failed  \n");
		exit (1);
	}

	if (!buffGetBlockFromHead (buffer, &resultString, 4)) {
		printf("A19: Failed  \n");
		exit (1);
	}

	if (!buffAdd (buffer, "test", 4)) {
		printf("A20: Failed  \n");
		exit (1);
	}

	if (!buffGetBlockFromHead (buffer, &resultString, 3)) {
		printf("A21: Failed  \n");
		exit (1);
	}

	if (!buffAdd (buffer, "test", 4)) {
		printf("A22: Failed  \n");
		exit (1);
	}

	if (!buffGetBlockFromHead (buffer, &resultString, 10)) {
		printf("A23: Failed  \n");
		exit (1);
	}

	/**********************/

/*
int
buffResize (buff buffer, unsigned int length);
*/
	if ( buffResize (NULL, 50)) {
		printf("H01: Failed  \n");
		exit (1);
	}

	if ( buffResize (NULL, 2000000)) {
		printf("H03: Failed  \n");
		exit (1);
	}

	if (!buffResize (buffer, 16000)) {
		printf("H04: Failed  \n");
		exit (1);
	}


	/**********************/

/*
unsigned int
buffReallocAdjustment (buff buffer, unsigned int newSize);
*/
	if (buffReallocAdjustment (NULL, 16000)) {
		printf("J01: Failed  \n");
		exit (1);
	}

	if (!buffReallocAdjustment (buffer, 16000)) {
		printf("J02: Failed  \n");
		exit (1);
	}

	if (buffReallocAdjustment (NULL, 1600000)) {
		printf("J03: Failed  \n");
		exit (1);
	}

	if (!buffReallocAdjustment (buffer, 16000)) {
		printf("J04: Failed  \n");
		exit (1);
	}


	/**********************/

	if ( buffGetBuffer (NULL, &resultString)) {
		printf("A24: Failed  \n");
		exit (1);
	}

	if ( buffGetBuffer (buffer, NULL)) {
		printf("A25: Failed  \n");
		exit (1);
	}

	if (!buffAdd (buffer, "test", 4)) {
		printf("A26: Failed  \n");
		exit (1);
	}

	if (!buffGetBuffer (buffer, &resultString)) {
		printf("A27: Failed  \n");
		exit (1);
	}

	/*********************

	if ( buffGetUndoTail (NULL, 1)) {
		printf("A28: Failed  \n");
		exit (1);
	}

	if (!buffGetUndoTail (buffer, 0)) {
		printf("A29: Failed  \n");
		exit (1);
	}

	if ( buffGetUndoTail (buffer, -1)) {
		printf("A30: Failed  \n");
		exit (1);
	}

	// actually rewind a character
	if (!buffGetUndoTail (buffer, 1)) {
		printf("A31: Failed  \n");
		exit (1);
	}
 */

	/**********************/

	if ( buffGetSize (NULL)) {
		printf("A32: Failed  \n");
		exit (1);
	}

	if (!buffGetSize (buffer)) {
		printf("A33: Failed  \n");
		exit (1);
	}

	/**********************/

	if ( buffGetLength (NULL)) {
		printf("A34: Failed  \n");
		exit (1);
	}

	if (!buffGetLength (buffer)) {
		printf("A35: Failed  \n");
		//exit (1);
	}

	/**********************/

	if ( buffCountMovedForward (NULL)) {
		printf("A36: Failed  \n");
		exit (1);
	}

	if ( buffCountResetEmpty (NULL)) {
		printf("A38: Failed  \n");
		exit (1);
	}

	if ( buffCountReallocs (NULL)) {
		printf("A40: Failed  \n");
		exit (1);
	}

	if ( buffCountInserts (NULL)) {
		printf("A42: Failed  \n");
		exit (1);
	}

	if ( buffCountRetrievals (NULL)) {
		printf("A44: Failed  \n");
		exit (1);
	}

	/**********************/

	if ( buffClear (NULL)) {
		printf("A48: Failed  \n");
		exit (1);
	}

	/* clean everything up */
	if (!buffClear (buffer)) {
		printf("A49: Failed  \n");
		exit (1);
	}

	/**********************/

	for (k=0; k < STRING_ENTRIES; k++) {

		printf("\nTesting is using this string:%s", tStr[k]);

/*
		printf("Inserting and removing %d lines, 10 times to test buffer growing functionality.\n", LOOP);
		for (j=0; j<10; j++){
			for (i = 0; i < LOOP; i++) {
				if (!buffAdd(buffer, tStr[k], tStrLen[k])) {
					printf("M01: Failed Could not add a string.\n");
					exit (1);
				}
			}
			printf("+");
			fflush(NULL);
		
			printf("-");
			fflush(NULL);
		}
		printf("\n");
		fflush(NULL);
*/

		printf("Inserting and removing %d lines, 10 times to test buffer growing functionality.\n", LOOP);
		for (j=0; j<10; j++){
			for (i = 0; i < LOOP; i++) {
				if (!buffAdd(buffer, tStr[k], tStrLen[k])) {
					printf("M01: Failed Could not add a string.\n");
					exit (1);
				}
			}
			printf("+");
			fflush(NULL);
		
			for (i=0; i < LOOP; i++) {
				if ((resultLength = buffGetLine(buffer, &resultString)) != tStrLen[k]){
					printf("M03: Failed on buffGetLine on line %d expected '%s' of length %d, got '%s' of length %d\n",
						i, tStr[k], tStrLen[k], resultString, resultLength);
					exit (1);
				}
			}
			printf("-");
			fflush(NULL);
		}
		printf("\n");
		fflush(NULL);
	
		printf("Inserting and removing %d lines, 30 times to test buffer move to front functionality.\n", LOOP/3);
		for (i = 0; i < LOOP/3; i++) {
			if (!buffAdd(buffer, tStr[k], tStrLen[k])) {
				printf("M01: Failed Could not add a string.\n");
				exit (1);
			}
		}
		printf("+");
		fflush(NULL);

		for (j=0; j<29; j++){
			for (i = 0; i < LOOP/3; i++) {
				if (!buffAdd(buffer, tStr[k], tStrLen[k])) {
					printf("M02: Failed Could not add a string.\n");
					exit (1);
				}
			}
			printf("+");
			fflush(NULL);
		
			for (i=0; i < LOOP/3; i++) {
				if ((resultLength = buffGetLine(buffer, &resultString)) != tStrLen[k]){
					printf("C03: Failed on buffGetLine on line %d expected '%s' of length %d, got '%s' of length %d\n",
						i, tStr[k], tStrLen[k], resultString, resultLength);
					exit (1);
				}
			}
			printf("-");
			fflush(NULL);
		}
		
		for (i=0; i < LOOP/3; i++) {
			if ((resultLength = buffGetLine(buffer, &resultString)) != tStrLen[k]){
				printf("C03: Failed on buffGetLine on line %d expected '%s' of length %d, got '%s' of length %d\n",
					i, tStr[k], tStrLen[k], resultString, resultLength);
				exit (1);
			}
		}
		printf("-");
		printf("\n");
		fflush(NULL);
	}
	printf("\n");

	if (!buffCountMovedForward (buffer)) {
		printf("Q01: Failed  \n");
	}

	if (!buffCountReallocs (buffer)) {
		printf("Q02: Failed  \n");
	}

	if (!buffCountInserts (buffer)) {
		printf("Q03: Failed  \n");
	}

	if (!buffCountRetrievals (buffer)) {
		printf("Q04: Failed  \n");
	}

	printf("Destroying a buffer.\n");
	if (!buffDestroy(buffer)) {
		printf("Q05: Failed Could not buffDestroy\n");
	}

	if ( buffTotalCount ()) {
		printf("Q06: Failed the total count should be equal to 0 \n");
	}

	if ( buffTotalSize ()) {
		printf("Q07: Failed the total size would be equal to 0 \n");
	}

	printf("Completed tests.\n");
	return 0;
}
