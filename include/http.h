#ifndef HTTP_H
#define HTTP_H

#include "cJSON.h"
#include <stddef.h>

// Returns a dynamically allocated string containing the response body.
// The caller is responsible for free()ing it.
// Returns NULL on error.
char* http_post_json(const char* url, const char* auth_header, const char* json_body);
char* http_get(const char* url, const char* auth_header);

#endif // HTTP_H
