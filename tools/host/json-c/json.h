// Host mock of <json-c/json.h>. The UI preview does not need a real report
// export, so the serialiser is a stub: it produces a placeholder document.
#pragma once

typedef struct json_object json_object;

enum {
    JSON_C_TO_STRING_PRETTY = 1 << 0,
    JSON_C_TO_STRING_SPACED = 1 << 1,
};

json_object* json_object_new_object(void);
json_object* json_object_new_array(void);
json_object* json_object_new_string(const char* s);
json_object* json_object_new_double(double d);
int          json_object_object_add(json_object* obj, const char* key, json_object* val);
int          json_object_array_add(json_object* arr, json_object* val);
const char*  json_object_to_json_string_ext(json_object* obj, int flags);
int          json_object_put(json_object* obj);
