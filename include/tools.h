#ifndef TOOLS_H
#define TOOLS_H

#include "cJSON.h"

// Get the JSON array describing available tools
cJSON* tools_get_definitions(void);

// Execute a tool given its name and arguments, returns a dynamically allocated JSON string (result or error)
// The caller must free() the returned string.
char* tools_execute(const char* name, cJSON* args);

#endif // TOOLS_H
