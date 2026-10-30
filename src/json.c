#include <stdlib.h>
#include <string.h>
#include "json.h"

J *DATA;

static void put(J *o, const char *k, J *v) {
    if (jget(o, k)) cJSON_ReplaceItemInObjectCaseSensitive(o, k, v);
    else cJSON_AddItemToObject(o, k, v);
}
void setnum(J *o, const char *k, double v) {
    J *e = jget(o, k);
    if (cJSON_IsNumber(e)) cJSON_SetNumberValue(e, v);
    else put(o, k, cJSON_CreateNumber(v));
}
void setstr(J *o, const char *k, const char *s) { put(o, k, s ? cJSON_CreateString(s) : cJSON_CreateNull()); }
void setbool(J *o, const char *k, bool b) { put(o, k, cJSON_CreateBool(b)); }
void delkey(J *o, const char *k) { cJSON_DeleteItemFromObjectCaseSensitive(o, k); }
J *child(J *o, const char *k) {
    J *c = jget(o, k);
    if (!cJSON_IsObject(c)) { c = cJSON_CreateObject(); put(o, k, c); }
    return c;
}
J *childarr(J *o, const char *k) {
    J *c = jget(o, k);
    if (!cJSON_IsArray(c)) { c = cJSON_CreateArray(); put(o, k, c); }
    return c;
}
