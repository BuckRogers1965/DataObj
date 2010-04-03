#include <stdlib.h>
#include <stdio.h>

#define IN_NAMESPACE


/* Given a set of strings, create a search tree of values */

typedef struct NSObj {

        /*@null@*/ struct NSObj * Child;

        /*@null@*/ struct NSObj * Next;

        char Character;
        long Value;

} NSObj;

//#include "namespace.h"


/*@null@*/ NSObj * NSCreate (){

	NSObj * Root;
	
        // create the NSObjs, zeroed memory;
        Root = (NSObj *)calloc(sizeof(NSObj), 1);
	if (Root == NULL) 
		return NULL;

        // put the NSObjs in a big loop around the Root. 

        // initialize the first new NSObj
        //Root->Next = NULL;
        //Root->Child = 0;
        //Root->Character = 0;
        //Root->Value = 0;

	return Root;
}

void NSRelease (NSObj * Root){

	NSObj * DeleteNow;
	NSObj * DeleteNext;

	DeleteNow = Root;

	while (DeleteNow) {
		DeleteNext = DeleteNow->Next;
		if (DeleteNow->Child)
			NSRelease(DeleteNow->Child);
		free(DeleteNow);
		DeleteNow = DeleteNext;
	}

	Root = NULL;
}

/*@null@*/ NSObj * AllocNSObj(){

	NSObj * Root;

        // create the NSObjs, zeroed memory;
        Root = (NSObj *)calloc(sizeof(NSObj), 1);
	if (Root == NULL) 
		return NULL;

        // put the NSObjs in a big loop around the Root. 

        // initialize the first new NSObj
        //Root->Next = NULL;
        //Root->Child = 0;
        //Root->Character = 0;
        //Root->Value = 0;
	
	return Root;
}

int NSInsert (NSObj * Root, char * String, long Value){

	/*@null@*/ NSObj * Current;
	/*@null@*/ NSObj * Previous;
	int StrLoc=0;

	// check values to make sure they are valid.
	if (String && String[0] && Root) {

		Current = Root->Child;
		Previous = Root;

		// crawl the string and tree, inserting NSObjs as needed.

		// while there are still characters left
		while (String[StrLoc]){

			// is current NULL?
			if (!Current) {

				// attach free NSObj to Previous
				Current = AllocNSObj();
				if (!Current)
					return 0;
				Previous->Child = Current;

				// put the first character from the string in it
				Current->Character = String[StrLoc];

			} else if (String[StrLoc] < Current->Character) {
			// is the character is less than Current->Character
				// Create a new NSObj and place it before this NSObj.
				NSObj * Temp;

				// attach free NSObj to Current
				Temp = AllocNSObj();
				if (!Temp)
					return 0;
				Temp->Character = String[StrLoc];

				Previous->Child = Temp;
				Temp->Next = Current;

				Current = Temp;

			} else if (String[StrLoc] != Current->Character) {

			// the character is greater than Current->Character

				while (Current && String[StrLoc] > Current->Character){
					Previous = Current;
					Current = Current->Next;
				}
				if (!Current){
					// we are larger than anything else
					Current = AllocNSObj();
					if (!Current)
						return 0;
					Previous->Next = Current;
                                	Current->Character = String[StrLoc];

				} else {

					// we are not equal to
					if (String[StrLoc] != Current->Character) {

						// attach free NSObj to Previous
						Previous->Next = AllocNSObj();
						if (!Previous->Next)
							return 0;
						Previous->Next->Next = Current;
						Current = Previous->Next;

						// put the character from the string in it
						Current->Character = String[StrLoc];
					}
				}
			}

			// Move to the next character
			Previous = Current;
			Current = Current->Child;

			StrLoc++;
		}	

		// all done with String
		Previous->Value = Value;
		return 1;

	} else
		return 0;
}

long NSSearch (NSObj * Root, char * String){

	NSObj * Current;
	NSObj * Previous;
	int StrLoc=0;

	// check values to make sure they are valid.
	if (String && String[0] && Root) {

		Current = Root->Child;
		Previous = Root;
	
		while (Current && String[StrLoc]) {
			if (String[StrLoc] > Current->Character) {
				while (Current && String[StrLoc] > Current->Character)
					Current = Current->Next;

				if (!Current)
					break;

				if (String[StrLoc] == Current->Character)
					if (!String[StrLoc+1])
						// Found
                        			return (Current->Value);
					Current = Current->Child;
	
			} else  if (String[StrLoc] == Current->Character) {
				if (!String[StrLoc+1])
					// Found
                        		return (Current->Value);
				Current = Current->Child;

			} else {
				break;
			}

			StrLoc++;
		}
	}
	
	// Not Found
	return 0;
}

int NSDelete (NSObj * Root, char * String){
	NSObj * Current;
	NSObj * Previous;
	NSObj * LastBranch;  
	int StrLoc=0;

	// check values to make sure they are valid.
	if (String && String[0] && Root) {

		Current    = Root->Child;
		Previous   = Root;
		LastBranch = Current;     // Remember the last value point, 
					  // delete from there to current when found.
	
		while (Current && String[StrLoc]) {
			if (String[StrLoc] > Current->Character) {
				while (Current && String[StrLoc] > Current->Character)
					Current = Current->Next;

				if (!Current)
					break;

				if (String[StrLoc] == Current->Character)
					if (!String[StrLoc+1]) {

						NSObj * DeleteNSObj;
						NSObj * DeleteNext;
						// Found

						// If there is no value here
						// Then we didn't find anything.
						if (!Current->Value)
							return 0;

						// if there is more from here, then leave it, 
						// just remove the stored value
						if(Current->Child){
							Current->Value = 0;
							return (1);
						}	

						// Delete the extra NSObjs
						DeleteNSObj = LastBranch->Child;
						LastBranch->Child = NULL;
						while (DeleteNSObj) {
							//printf("%c", DeleteNSObj->Character);
							DeleteNext = DeleteNSObj->Child;
							free (DeleteNSObj);
							DeleteNSObj = DeleteNext;
						}

                        			return (1);
					}

					if (Current->Value > 0)
						LastBranch = Current;
					Current = Current->Child;
	
			} else  if (String[StrLoc] == Current->Character) {

				if (!String[StrLoc+1]) {

					NSObj * DeleteNSObj;
					NSObj * DeleteNext;
					// Found

					// If there is no value here
					// Then we didn't find anything.
					if (!Current->Value)
						return 0;

					// if there is more from here, then leave it, 
					// just remove the stored value
					if(Current->Child){
						Current->Value = 0;
						return (1);
					}	

					// Delete the extra NSObjs
					DeleteNSObj = LastBranch->Child;
					LastBranch->Child = NULL;
					while (DeleteNSObj) {
						//printf("%c", DeleteNSObj->Character);
						DeleteNext = DeleteNSObj->Child;
						free (DeleteNSObj);
						DeleteNSObj = DeleteNext;
					}

                        		return (1);
				}

				if (Current->Value > 0)
					LastBranch = Current;
				Current = Current->Child;

			} else {
				break;
			}

			StrLoc++;
		}
	}
	
	// Not Found
	return 0;
}

#include <stdio.h>
#include <unistd.h>

#include "namespace.h"

int NameSpaceTest (){

	NSObj * NameSpace;
	int i, j;
	char string[1024];

  for (j=0; j<=10; j++) {

	NameSpace = NSCreate();

    for (i=0; i <= 10000; i++) {
	
	sprintf(string, "lksdfhjklsdfjklsdfhjklsdfhjklsdfh%d/test0/test1/test2/test3/test4/%d", i/10, j);
	NSInsert(NameSpace, string, i);
	printf("%ld\n", NSSearch(NameSpace, string));

	NSInsert(NameSpace, "james",     1);
	NSInsert(NameSpace, "rogers",    2);
	NSInsert(NameSpace, "bob",       3);
	NSInsert(NameSpace, "aaa",       3);
	NSInsert(NameSpace, "bobabcdefghijklmnopqrstuv",    4);
	NSInsert(NameSpace, "bobabcdefghij",   5);
	NSInsert(NameSpace, "fred",      6);
	NSInsert(NameSpace, "This is a very/long/test of /multiple dirs/",       7);
	NSInsert(NameSpace, "This is a very/long/test of /multiple dirs/me too",       8);
	NSInsert(NameSpace, "harry",     9);
	NSInsert(NameSpace, "retriever", 10);

	printf("%ld ", NSSearch(NameSpace, "james"));
	printf("%ld ", NSSearch(NameSpace, "rogers"));
	printf("%ld ", NSSearch(NameSpace, "bob"));
	printf("%ld ", NSSearch(NameSpace, "aaa"));
	printf("%ld ", NSSearch(NameSpace, "bobabcdefghijklmnopqrstuv"));
	printf("%ld ", NSSearch(NameSpace, "bobabcdefghij"));
	printf("%ld ", NSSearch(NameSpace, "fred"));
	printf("%ld ", NSSearch(NameSpace, "This is a very/long/test of /multiple dirs/"));
	printf("%ld ", NSSearch(NameSpace, "This is a very/long/test of /multiple dirs/me too"));
	printf("%ld ", NSSearch(NameSpace, "harry"));
	printf("%ld  ", NSSearch(NameSpace, "retriever"));

	printf("%ld ", NSSearch(NameSpace, "I don't exist"));
	
	printf("%d ", NSDelete(NameSpace, "bobabcdefghij") );
	printf("%d ", NSDelete(NameSpace, "bobabcdefghij") );
	printf("%d  ", NSDelete(NameSpace, "I don't exist") );

	printf("%ld ", NSSearch(NameSpace, "bob"));
	printf("%ld ", NSSearch(NameSpace, "bobabcdefghijklmnopqrstuv"));
	printf("%ld ", NSSearch(NameSpace, "bobabcdefghij"));

	NSInsert(NameSpace, "james",     1000);
	printf("%ld\n", NSSearch(NameSpace, "james"));

    }

	NSRelease(NameSpace);

	usleep (0);
	fprintf(stderr,".");
  }
	fprintf(stderr,"\n");
	return 0;
}

#ifdef TESTBUILD
int main (){

NameSpaceTest();

return 0;
}

#endif

