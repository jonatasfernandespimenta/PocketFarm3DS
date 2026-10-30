// Shorthands over cJSON so table/state access reads like the JS: num(o, "sell") ≈ o.sell || 0.
#pragma once
#include <stdbool.h>
#include "cJSON.h"

typedef cJSON J;
extern J *DATA;   // romfs:/data.json: every table the web modules export

static inline J *jget(const J *o, const char *k) { return o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL; }
static inline J *jat(const J *a, int i) { return a ? cJSON_GetArrayItem(a, i) : NULL; }
static inline int jlen(const J *a) { return a ? cJSON_GetArraySize(a) : 0; }
static inline double num(const J *o, const char *k) { J *v = jget(o, k); return cJSON_IsNumber(v) ? v->valuedouble : cJSON_IsTrue(v) ? 1 : 0; }
static inline double numv(const J *v) { return cJSON_IsNumber(v) ? v->valuedouble : cJSON_IsTrue(v) ? 1 : 0; }
static inline const char *str(const J *o, const char *k) { J *v = jget(o, k); return cJSON_IsString(v) ? v->valuestring : NULL; }
static inline bool has(const J *o, const char *k) { J *v = jget(o, k); return v && !cJSON_IsNull(v) && !cJSON_IsFalse(v); }
static inline J *table(const char *name) { return jget(DATA, name); }

void setnum(J *o, const char *k, double v);
void setstr(J *o, const char *k, const char *s);
void setbool(J *o, const char *k, bool b);
void delkey(J *o, const char *k);
J *child(J *o, const char *k);            // o[k] ??= {}
J *childarr(J *o, const char *k);         // o[k] ??= []
#define JFOR(it, o) for (J *it = (o) ? (o)->child : NULL; it; it = it->next)
