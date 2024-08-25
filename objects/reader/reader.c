#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

//#include "callback.h"
#include "framework.h"

struct InstanceData
{
    int state;           // Current state of the instance (e.g., running, stopped)
    char filename[1025]; // Path to the file being processed
    int tail;
    FILE *file_handle; // File handle for the open file
    NodeObj *instance;
    struct InstanceData *next;
};

typedef struct InstanceData InstanceData;

typedef struct
{
    NodeObj *instance;
    InstanceData *list;
    NodeObj self;
} ClassData;

ClassData class;

int Handle_Message(NodeObj instance, NodeObj data)
{
    DebugPrint("Handling a message.", __FILE__, __LINE__, OBJMSGHANDLING);
    return rtrn_handled;
}


int onFilenameChange(NodeObj instance, int msg_id, NodeObj data)
{
    InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
    char *new_filename = GetValueStr(data);
    strcpy(new_filename, local->filename);
    return rtrn_handled;
}

int onStatusChange(NodeObj instance, int msg_id, NodeObj data)
{
    InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
    // Handle changes to the "Status LED" property
    local->state = GetPropInt(data, "value");
    return rtrn_handled;
}


/* 

onStatusChange

onStopChange’ undeclared (first use in this function)
onTailFileChange’ undeclared (first use in this function)
  122 |     SetSubProp(temp, "Filename", PROP_CHECKBOX, onTailFileChange, 
*/

//onRunChange undeclared (first use in this function)
int onRunChange(NodeObj instance, int msg_id, NodeObj data)
{
    InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
    // Get the current state
    int current_state = GetPropInt(instance, "State");
    // Toggle the state
    if (local->state == 0)
    {
        // Open the file in non-blocking mode
        FILE *file = fopen(local->filename, "r");
        if (file != NULL)
        {
            int flags = fcntl(fileno(file), F_GETFL, 0);
            if (flags != -1)
            {
                fcntl(fileno(file), F_SETFL, flags | O_NONBLOCK);
            }

            // Store the file handle in the instance's local data
            local->file_handle = file;
            local->state = 1;
            // send message to text box with a start message handle
            // Send_Message (local, START, empty_node);
        }
        else
        {
            return rtrn_dropped;
        }
    }
    else
    {
        return rtrn_dropped;
    }
    return rtrn_handled;
}

int onStopChange(NodeObj instance, int msg_id, NodeObj data)
{
    InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
    if (local->state == 1)
    { // Check if the object is currently running
        // Stop the object
        local->state = 0;
        // Close the file if it's open
        if (local->file_handle != NULL)
        {
            fclose(local->file_handle);
            local->file_handle = NULL;
        }
    }
    else
    {
        return rtrn_dropped;
    }
    return rtrn_handled;
}

int onOutputChange(NodeObj instance, int msg_id, NodeObj data)
{
    InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
    char buffer[1025];

    if (local->state == 1 && local->file_handle != NULL)
    {
        ssize_t bytes_read =  0; // fread(fileno(local->file_handle), buffer, 1, 1024);
        buffer[bytes_read] = 0;

        if (bytes_read > 0)
        {
            //ScheduleMessage(local, "ReadNext", NULL, 0);
        }
        else if (feof(local->file_handle))
        {
            // End of file reached
            fclose(local->file_handle);
            local->file_handle = NULL;
            local->state = 0;
        }
        else
        {
            // Error reading from file
            // Handle the error (e.g., log, emit an error message)
        }
    }
    ScheduleMessage(local, "ReadNext", NULL, 0);
    return rtrn_handled;
}

int onTailFileChange(NodeObj instance, int msg_id, NodeObj data)
{
    InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
    local->tail = GetPropInt(data, "value");
    return rtrn_handled;
}

int onCustomMessage1(NodeObj instance, int msg_id, NodeObj data)
{
    InstanceData *local = (InstanceData *)GetPropLong(instance, "local");
    return rtrn_handled;
}




void SetSubProp(NodeObj prop, char *name, int graphics, long func_ptr, long local_ptr)
{
    NodeObj subprop = NewNode(INTEGER);
    SetName(subprop, "Properties");
    SetPropInt(subprop, "graphics", graphics);
    SetPropLong(subprop, "OnChange", (long)func_ptr);
    SetPropLong(subprop, "local", (long)local_ptr);

    NodeObj the_prop = GetPropNode(prop, name);
    AddProp(the_prop, subprop);
}

int InstanceStart(NodeObj instance, int msg_id, NodeObj data)
{
    printf("    ***   In INSTANCE START    ***\n");

    // Create the local data structure
    InstanceData *local = malloc(sizeof(InstanceData));
    local->state = 0; // Initial state: Stopped
    local->filename[0] = 0;

    NodeObj temp = NewNode(INTEGER);
    SetName(temp, "Reader");
    SetPropStr(temp, "Filename", "");
    SetSubProp(temp, "Filename",  PROP_TEXTBOX, (long) onFilenameChange, (long) local);
    SetPropInt(temp, "Status LED", 0);
    SetSubProp(temp, "Status LED", PROP_LED, (long) onStatusChange,  (long) local);
    SetPropInt(temp, "Run Button", 0);
    SetSubProp(temp, "Run Button", PROP_BUTTON, (long) onRunChange,  (long) local);
    SetPropInt(temp, "Stop Button", 0);
    SetSubProp(temp, "Stop Button", PROP_BUTTON, (long) onStopChange,  (long) local);
    SetPropInt(temp, "Tail File", 0);
    SetSubProp(temp, "Tail File", PROP_CHECKBOX, (long) onTailFileChange,  (long) local);
    SetPropStr(temp, "Output", "");
    SetSubProp(temp, "Output", PROP_TEXTBOX, (long) onTailFileChange,  (long) local);

    local->instance = RegisterInstance(instance, temp); // does this need a node passed to hold info
    return rtrn_handled;
}



int InstanceEnd(NodeObj instance, int msg_id, NodeObj data)
{

    InstanceData *local = (InstanceData *)GetPropLong(instance, "local");

    // Close the file if it's open
    if (local->file_handle != NULL)
    {
        fclose(local->file_handle);
    }
    UnRegisterInstance(instance);
    UnRegisterinstancewithclass(instance);
    free(local);

    return rtrn_handled;
}

int ClassStart(NodeObj instance, int msg_id,  NodeObj data)
{
    printf("    ***   In CLASS START    ***\n");
    NodeObj temp = NewNode(INTEGER);
    SetName(temp, "Reader");
    SetPropLong(temp, "InstanceStart", (long)InstanceStart);
    SetPropLong(temp, "InstanceEnd", (long)InstanceEnd);
    //NodeObj depends = NewNode();

    class.instance = RegisterClass(class.self, temp);
    class.list = NULL;
    return rtrn_handled;
}

int ClassEnd(NodeObj instance, int msg_id, NodeObj data)
{
    InstanceData *element, *next;
    element = class.list;
    while (element != NULL)
    {
        next = element->next;
        InstanceEnd(element->instance, data, msg_id);
        element = next;
    }
    UnRegisterClass(class.instance);
    class.instance = NULL;
    class.list = NULL;
    return rtrn_handled;
}

int ClassMsg(NodeObj instance, int msg_id, NodeObj data)
{
    return rtrn_handled;
}

void _init()
{
    // object level loading by library function
    // add a dependency list to this call so the core and load the classes in the correct order
    // incase one class requires another class to be present to subclass

    printf ("In object:  Class callbacks: %lu, %lu, %lu\n", (unsigned long)ClassStart, (unsigned long)ClassEnd, (unsigned long)ClassMsg);

    NodeObj temp = NewNode(INTEGER);
    SetName(temp, "Reader");
    SetPropStr(temp, "Company", "GrokThink");
    SetPropStr(temp, "UUID", "8da17004-242c-4f21-a77e-6a823a52c639");
    SetPropLong(temp, "ClassStart", (long)ClassStart);
    SetPropLong(temp, "ClassEnd", (long)ClassEnd);
    SetPropLong(temp, "ClassMsg", (long)ClassMsg);
    SetPropInt(temp, "State", 1);

    class.self=temp;

    class.self = RegisterLibrary(temp);
}

void _fini()
{
    UnregisterLibrary(class.self);
    class.self = NULL;
}
