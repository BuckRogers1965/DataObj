#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>  


/* Data objects.

Create intelligent data objects that can handle automatic data conversions.

*/

/*

Tasks needed done:

O  Add date types

O  add list container type with iterators and selectors

O  add function pointer types

O  add bulk blob data type

o  add getxml and store xml types

*/

#define SET 0
#define GET 1

#define STRING  0
#define INTEGER 1
#define HEX     2
#define REAL    3
#define LONG    4
#define BOOL    5


typedef struct Data * DataObj;

typedef intptr_t(*func_ptr)(DataObj, int, int, char *);

struct Data {

	int type;

	/* which representations are currently valid. One bit each, and they   */
	/* live HERE, together, in the alignment hole after type - six ints    */
	/* spread between the values they describe cost 40 bytes of the        */
	/* struct in flags and the padding they forced. Grouped, they cost 1   */
	/* byte in space that was already being wasted, and struct Data drops  */
	/* 96 -> 56, so a DataObj is one cache line instead of two.            */
	/* Only ever 0 or 1 - every use is an assignment of a literal or a     */
	/* truth test, which is what makes a single bit enough.                */
	unsigned char str_set  : 1;
	unsigned char int_set  : 1;
	unsigned char hex_set  : 1;
	unsigned char real_set : 1;
	unsigned char long_set : 1;
	unsigned char bool_set : 1;

	char bool_val;

	func_ptr call;

	char * str_val;
	int str_len;	/* actual byte count of str_val - may exceed strlen() if it */
			/* holds bytes set through SetStrLen, which allows embedded NULs */

	int int_val;

	char * hex_val;

	double real_val;

	long long_val;

} Data;

char *
Real2Str(double val){
	char * ret_val = malloc(13);
	if (!ret_val)
		return NULL;
	sprintf(ret_val, "%f", val);
	return ret_val;
}

char *
Int2Hex(int val){
	char * ret_val = malloc(10);
	if (!ret_val)
		return NULL;
	sprintf(ret_val, "%x", val);
	return ret_val;
}

int
Hex2Int(char * val){
	int ret_val = 0;
	if (!val)
		return 0;
	while(val[0] && isxdigit(val[0])){
		if (isdigit(val[0]))
			ret_val = ret_val * 16 + val[0] - '0';
		else
			ret_val = ret_val * 16 + val[0] -'A' + 10;
		val++;
	}
	return ret_val;
}

char * Int2Str(int val){
	char * ret_val = malloc(10);
	if (!ret_val)
		return NULL;
	sprintf(ret_val, "%d", val);
	return ret_val;
}

int Str2Int(char * val){
	int ret_val=0;
	if (!val)
		return 0;

	while(val[0] && isspace(val[0])){
		val++;
	}
	while(val[0] && isdigit(val[0])){
		ret_val=ret_val*10+val[0]-'0';
		val++;
	}
	return ret_val;
}

char * Long2Str(long val){
	/* LONG_MIN is 20 characters plus the terminator */
	char * ret_val = malloc(24);
	if (!ret_val)
		return NULL;
	sprintf(ret_val, "%ld", val);
	return ret_val;
}

long Str2Long(char * val){
	long ret_val=0;
	if (!val)
		return 0;

	while(val[0] && isspace(val[0])){
		val++;
	}
	while(val[0] && isdigit(val[0])){
		ret_val=ret_val*10+val[0]-'0';
		val++;
	}
	return ret_val;
}



char * dup (char * val){
	int length;
	char * ret_val = NULL;
	if (val == NULL)
		return NULL;
	length = strlen (val);
	ret_val = malloc (length+1);
	if (!ret_val)
		return NULL;
	strncpy(ret_val, val, length+1);
	return ret_val;
}


void clear (DataObj this){
	this->str_set=0;
	this->str_val=NULL;
	this->str_len=0;
	this->int_set=0;
	this->int_val=0;
	this->hex_set=0;
	this->hex_val=NULL;
	this->real_set = 0;
	this->real_val = 0;
	this->long_set = 0;
	this->long_val = 0;
	this->bool_set = 0;

}

void clearAll (DataObj this){

	if (this->str_val)
		free(this->str_val);

	if (this->hex_val)
		free(this->hex_val);

	clear(this);
}

int convert(DataObj this, int type){
	switch(this->type){
	case STRING:
		switch(type){
		case INTEGER:
			if (!this->int_set) {
				this->int_val = Str2Int(this->str_val);
				this->int_set=1;
			}
			return 1;
		case HEX:
			if (!this->hex_set) {
				convert (this, INTEGER);
				this->hex_val = Int2Hex(this->int_val);
				this->hex_set=1;
			}
			return 1;
		case REAL:
			if (!this->real_set) {
				convert (this, INTEGER);
				this->real_val = this->int_val;
				this->real_set=1;
			}
			return 1;
		case LONG:
			if (!this->long_set) {
				this->long_val = Str2Long(this->str_val);
				this->long_set=1;
			}
			return 1;
		default:
			break;		
		}
		break;
	case INTEGER:
		switch(type){
		case STRING:
			if (!this->str_set) {
				this->str_val = Int2Str(this->int_val);
				this->str_set=1;
				this->str_len = strlen(this->str_val);
			}
			return 1;
		case HEX:
			if (!this->hex_set) {
				this->hex_val = Int2Hex(this->int_val);
				this->hex_set=1;
			}
			return 1;
		case REAL:
			if (!this->real_set) {
				this->real_val=this->int_val;
				this->real_set=1;
			}
			return 1;
		default:
			break;		
		}
		break;
	case HEX:
		switch(type){
		case STRING:
			if (!this->str_set) {
				this->str_val = dup(this->hex_val);
				this->str_set = 1;
				this->str_len = strlen(this->str_val);
			}
			return 1;
		case INTEGER:
			if (!this->int_set) {
				this->int_val = Hex2Int(this->hex_val);
				this->int_set = 1;
			}
			return 1;
		case REAL:
			if (!this->real_set) {
				convert (this, INTEGER);
				this->real_val = this->int_val;
				this->real_set=1;
			}
			return 1;
		default:
			break;		
		}
		break;
	case REAL:
		switch(type){
		case STRING:
			if (!this->str_set) {
				this->str_val=Real2Str(this->real_val);
				this->str_set=1;
				this->str_len = strlen(this->str_val);
			}
			return 1;
		case INTEGER:
			if (!this->int_set) {
				this->int_val=this->real_val;
				this->int_set=1;
			}
			return 1;
		case HEX:
			if (!this->hex_set) {
				convert (this, INTEGER);
				this->hex_val = Int2Hex(this->int_val);
				this->hex_set=1;
			}
			return 1;
		default:
			break;		
		}
		break;
	case LONG:
		switch(type){
		case STRING:
			if (!this->str_set) {
				this->str_val=Long2Str(this->long_val);
				this->str_set=1;
				this->str_len = strlen(this->str_val);
			}
			return 1;
		default:
			break;		
		}
		break;
	default:
		;

	}
	return 0;
}

intptr_t datafunc(DataObj this, int kind, int type, char * val){
	if (kind == GET){
		switch (type) {
		case INTEGER:
			if (!this->int_set) {
				convert(this, INTEGER);
			}
			return this->int_val;
		case STRING:
			if (!this->str_set){
				convert(this, STRING);
			}
			return (intptr_t)this->str_val;
		case HEX:
			if (!this->hex_set){
				convert(this, HEX);
			}
			return (intptr_t)this->hex_val;
		case REAL:
			if (!this->real_set){
				convert(this, REAL);
			}
			return (intptr_t)&this->real_val;
		case LONG:
		    if (!this->long_set){
			    convert(this, LONG);
			}
			return (intptr_t)this->long_val;
		default:
			return 0;
			
		}		
	} else {
		clearAll(this);

		/* whichever type is being written becomes the authoritative     */
		/* representation convert() derives everything else from - without */
		/* this, a DataObj created as one type and later Set as another   */
		/* keeps converting from its stale birth type and finds no path   */
		this->type = type;

		switch (type){

		case INTEGER:
			this->int_val=(intptr_t)val;
			this->int_set=1;
			return 1;
		case STRING:
			this->str_val = dup(val);
			if (!this->str_val)
				return 0;
			this->str_set = 1;
			this->str_len = strlen(val);
			return 1;
		case HEX:
			this->hex_val = dup(val);
			if (!this->hex_val)
				return 0;
			this->hex_set = 1;
			return 1;
		case REAL:
			this->real_val = *(double*)val;
			this->real_set = 1;
			return 1;
		case LONG:
			this->long_val=(long)val;
			this->long_set=1;
			return 1;
		default:
			return 0;
		}
	}
	return 1;
}

/* allocation accounting - see the twin counter in node.c for the idea */
static long datasAlive = 0;

long DataCount(void)
{
	return datasAlive;
}

DataObj
NewData(int type){
	DataObj ret_val = malloc(sizeof(Data));
	if (!ret_val)
		return NULL;
	datasAlive++;
	clear(ret_val);
	ret_val->type = type;
	switch (type){
		case INTEGER:	
			ret_val->call= &datafunc;
			ret_val->int_set=1;
			break;
		case STRING:
			ret_val->call= &datafunc;
			ret_val->str_set=1;
			ret_val->str_val = dup("");
			break;
		case HEX:
			ret_val->call= &datafunc;
			ret_val->hex_set=1;
			ret_val->hex_val = dup("");
			break;
		case REAL:
			ret_val->call= &datafunc;
			ret_val->real_set=1;
			ret_val->real_val = 0;
			break;
		case LONG:	
			ret_val->call= &datafunc;
			ret_val->long_set=1;
			break;
		default:
			datasAlive--;
			free(ret_val);
			return NULL;

	}
	return ret_val;
}

/* free a data object created by NewData - str_val/hex_val included.  Every */
/* NodeObj owns two of these (name, value) that NewNode allocates but never */
/* frees on its own; DelNode calls this for both when a node is deleted     */
void
DelData(DataObj this){
	if (!this)
		return;

	clearAll(this);
	datasAlive--;
	free(this);
}

int
GetDataType(DataObj this){
	if (!this)
		return STRING;
	return this->type;
}

char *
GetStr(DataObj this){
	if (!this)
		return 0;
	return (char *)(intptr_t)this->call(this, GET, STRING, NULL);

}

int
SetStr(DataObj this, char * value){
	if (!this)
		return 0;
	return this->call(this, SET, STRING, value);
}

/*
 * Like SetStr, but copies exactly `length` bytes via memcpy instead of
 * stopping at the first NUL - for payloads that may contain embedded NULs
 * (raw TCP bytes, WebSocket frames). Bypasses the call() dispatch since
 * that interface has no way to pass a length through; str_val still gets
 * a defensive trailing NUL so GetStr's text view keeps working, but only
 * GetStrLen bytes are guaranteed to be the real content.
 */
int
SetStrLen(DataObj this, char * value, int length){
	if (!this || length < 0)
		return 0;
	clearAll(this);
	this->type = STRING;
	this->str_val = malloc(length + 1);
	if (!this->str_val)
		return 0;
	memcpy(this->str_val, value, length);
	this->str_val[length] = 0;
	this->str_len = length;
	this->str_set = 1;
	return 1;
}

int
GetStrLen(DataObj this){
	if (!this)
		return 0;
	if (!this->str_set)
		convert(this, STRING);
	return this->str_len;
}

int
SetInt(DataObj this, int value){
	if (!this)
		return 0;
	return this->call(this, SET, INTEGER, (char *)(intptr_t)value);
}

int
GetInt(DataObj this){
	if (!this)
		return 0;
	return this->call(this, GET, INTEGER, NULL);
}

int
SetLong(DataObj this, long value){
	if (!this)
		return 0;
	return this->call(this, SET, LONG, (char *)(intptr_t)value);
}

long
GetLong(DataObj this){
	if (!this)
		return 0;
	return (long)(intptr_t)this->call(this, GET, LONG, NULL);
}

int
SetHex(DataObj this, char * value){
	if (!this)
		return 0;
	return this->call(this, SET, HEX, (char *)(intptr_t)value);
}

char *
GetHex(DataObj this){
	if (!this)
		return 0;
	return (char *)(intptr_t)this->call(this, GET, HEX, NULL);
}

int
SetReal(DataObj this, double value){
	if (!this)
		return 0;
	return this->call(this, SET, REAL, (char *)(intptr_t)&value);
}

double
GetReal(DataObj this){
	char * result;
	if (!this)
		return 0;
	result =  (char *)(intptr_t)this->call(this, GET, REAL, NULL);
	return *(double *)(intptr_t)result ;
}


