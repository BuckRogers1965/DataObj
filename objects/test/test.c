#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#include "../../node.h"
//#include "../../callback.h"

#include "../../node.h"
#include "../../object.h"
#include "../../DebugPrint.h"
#include "../../callback.h"
#include "../../data.h"

typedef enum {
    PROP_TEXTBOX=1,
    PROP_LED,
    PROP_BUTTON,
    PROP_CHECKBOX,
    PROP_NULL
} PropertyType;

typedef void (*PropertyChangeHandler)(int index, DataObj defaults);


typedef struct {
    int index;                     // Unique index for the property
    char name[50];                 // Name of the property
    PropertyType type;             // Type of the property
    PropertyChangeHandler handler; // Function to call on change
    DataObj defaults;
} Property;


void onFilenameChange(int index, DataObj defaults) 
  {printf("Property %d: %s\n", index, GetStr(defaults));}
void onStatusChange(int index, DataObj defaults) 
  {printf("Property %d: %s\n", index, GetStr(defaults));}
void onRunChange(int index, DataObj defaults) 
  {printf("Property %d: %s\n", index, GetStr(defaults));}
void onStopChange(int index, DataObj defaults) 
  {printf("Property %d: %s\n", index, GetStr(defaults));}
void onTailFileChange(int index, DataObj defaults) 
  {printf("Property %d: %s\n", index, GetStr(defaults));}
void Instance_Start(int index, DataObj defaults) 
  {printf("Property %d: %s\n", index, GetStr(defaults));}
void Instance_Finish(int index, DataObj defaults) 
  {printf("Property %d: %s\n", index, GetStr(defaults));}
void Instance_msg(int index, DataObj defaults) 
  {printf("Property %d: %s\n", index, GetStr(defaults));}

void onPropertyChange(int index, DataObj defaults) {
    // Handle the property change here
    printf("Property %d: %s\n", index, GetStr(defaults));
}

Property * build_properties(){

    //Create defaults
	DataObj str_do  = NewData(STRING);
	DataObj int_do  = NewData(INTEGER);
	DataObj hex_do	= NewData(HEX);
	DataObj real_do = NewData(REAL);
    DataObj real_do2 = NewData(REAL);

    //Create defaults
    SetStr(str_do, "   1000  test me ");
	SetInt(int_do, 67676);
	SetHex(hex_do, "BEEF");
	SetReal(real_do, 12344.56 );
    SetReal(real_do2, 12344.56 );

    //Create and set properties of instances
    Property *properties = malloc(5 * sizeof(Property));
    properties[0] = (Property){0, "Instance Start", PROP_NULL,     Instance_Start,   int_do};
    properties[0] = (Property){0, "Instance Stop",  PROP_NULL,     Instance_Finish,  str_do};
    properties[0] = (Property){0, "message",        PROP_NULL,     Instance_msg,     hex_do};
    properties[0] = (Property){0, "Filename",       PROP_TEXTBOX,  onFilenameChange, str_do};
    properties[1] = (Property){1, "Status LED",     PROP_LED,      onStatusChange,   int_do};
    properties[2] = (Property){2, "Run Button",     PROP_BUTTON,   onRunChange,      hex_do};
    properties[3] = (Property){3, "Stop Button",    PROP_BUTTON,   onStopChange,     real_do};
    properties[4] = (Property){4, "Tail File",      PROP_CHECKBOX, onTailFileChange, real_do2};
    properties[5] = (Property){5, "Output",         PROP_TEXTBOX,  onTailFileChange, real_do2};
    properties[6] = (Property){6, NULL, NULL, NULL, NULL};

    return (properties);
}

void main (){
	
    printf("main a\n");

    Property *properties = build_properties();

    printf("main b\n");
    // Example usage
    for (int i = 0; i < 5; ++i) {
        // Simulate a change
        printf("main %d\n", i);
        int dummyValue = 0;
        properties[i].handler(properties[i].type, properties[i].defaults);
    }

}