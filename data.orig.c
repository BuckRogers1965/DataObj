#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>


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

#define STRING 0
#define INTEGER 1
#define HEX 2
#define REAL 3


typedef struct Data * DataObj;

typedef int(*func_ptr)(DataObj, int, int, char *);

struct Data {

	int type;
	func_ptr call;
		
	int str_set;
	char * str_val;

	int int_set;
	int int_val;

	int hex_set;
	char * hex_val;

	int real_set;
	double real_val;

	
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
	this->int_set=0;
	this->int_val=0;
	this->hex_set=0;
	this->hex_val=NULL;
	this->real_set = 0;
	this->real_val = 0;
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
	default:
		;

	}
	return 0;
}

int datafunc(DataObj this, int kind, int type, char * val){
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
			return (int)this->str_val;
		case HEX:
			if (!this->hex_set){
				convert(this, HEX);
			}
			return (int)this->hex_val;
		case REAL:
			if (!this->real_set){
				convert(this, REAL);
			}
			return (int)&this->real_val;
		default:
			return 0;
			
		}		
	} else {
		clearAll(this);

		switch (type){

		case INTEGER:
			this->int_val=(int)val;
			this->int_set=1;
			return 1;
		case STRING:
			this->str_val = dup(val);
			if (!this->str_val)
				return 0;
			this->str_set = 1;
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
		default:
			return 0;
		}
	}
	return 1;
}

DataObj
NewData(int type){
	DataObj ret_val = malloc(sizeof(Data));
	if (!ret_val)
		return NULL;
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
		default:
			free(ret_val);
			return NULL;
	}
	return ret_val;
}

char *
GetStr(DataObj this){
	if (!this)
		return 0;
	return (char *)this->call(this, GET, STRING, NULL);
}

int
SetStr(DataObj this, char * value){
	if (!this)
		return 0;
	return this->call(this, SET, STRING, value);
}

int
SetInt(DataObj this, int value){
	if (!this)
		return 0;
	return this->call(this, SET, INTEGER, (char *)value);
}

int
GetInt(DataObj this){
	if (!this)
		return 0;
	return this->call(this, GET, INTEGER, NULL);
}

int
SetHex(DataObj this, char * value){
	if (!this)
		return 0;
	return this->call(this, SET, HEX, (char *)value);
}

char *
GetHex(DataObj this){
	if (!this)
		return 0;
	return (char *)this->call(this, GET, HEX, NULL);
}

int
SetReal(DataObj this, double value){
	if (!this)
		return 0;
	return this->call(this, SET, REAL, (char *)&value);
}

double
GetReal(DataObj this){
	char * result;
	if (!this)
		return 0;
	result =  (char *)this->call(this, GET, REAL, NULL);
	return *(double *)result ;
}

void
DataTest (){

	char * str_str;
	char * str_int;
	char * str_hex;
	char * str_real;

	int int_str;
	int int_int;
	int int_hex;
	int int_real;

	char * hex_str;
	char * hex_int;
	char * hex_hex;
	char * hex_real;

	double real_str;
	double real_int;
	double real_hex;
	double real_real;

	DataObj str_do  = NewData(STRING);
	DataObj int_do  = NewData(INTEGER);
	DataObj hex_do	= NewData(HEX);
	DataObj real_do = NewData(REAL);

	//int i = 0;

	SetStr(str_do, "   1000  test me ");
	SetInt(int_do, 67676);
	SetHex(hex_do, "BEEF");
	SetReal(real_do, 12344.56 );

	str_str  = GetStr(str_do);
	int_str  = GetInt(str_do);
	hex_str  = GetHex(str_do);
	real_str = GetReal(str_do);

	str_int  = GetStr(int_do);
	int_int  = GetInt(int_do);
	hex_int  = GetHex(int_do);
	real_int = GetReal(int_do);

	str_hex  = GetStr(hex_do);
	int_hex  = GetInt(hex_do);
	hex_hex  = GetHex(hex_do);
	real_hex = GetReal(hex_do);

	str_real  = GetStr(real_do);
	int_real  = GetInt(real_do);
	hex_real  = GetHex(real_do);
	real_real = GetReal(real_do);

	printf("      str\t\t\tint\t\t\thex\t\t\treal\n");
	printf("str  >%s<\t %s \t\t\t %s \t\t\t %s \n", str_str, str_int, str_hex, str_real);
	printf("int   %d \t\t\t>%d<\t\t\t %d \t\t\t %d \n", int_str, int_int, int_hex, int_real);
	printf("hex   %s \t\t\t %s \t\t\t>%s<\t\t\t %s \n", hex_str, hex_int, hex_hex, hex_real);
	printf("real  %e \t\t %e \t\t %e \t\t>%e<\n\n", real_str, real_int, real_hex, real_real);
}
