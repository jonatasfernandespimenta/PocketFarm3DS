// workshop.c internals shared with bench.c (workshop.js exports that api.h doesn't list).
#pragma once
#include "api.h"

typedef struct { const char *item; int have, need; } WsGoal;   // item points into the sandbox (or "money")
J *puzzle(const char *id);                                   // PUZZLES entry (table reference) or NULL
J *ws_build(J *pz, J *layout);                               // fresh sandbox state, caller frees
void ws_tick(J *s);
int ws_progress(J *pz, J *s, WsGoal *out, int max, bool *done);
int ws_used(J *layout, const char *type);
bool ws_canPut(J *pz, J *s, J *layout, const char *type, int x, int y);
int ws_stars(J *pz, int pieces, int steps);
int ws_solve(J *pz, J *layout, int max);                     // steps to the goal, 0 = never
J *ws_daily(const char *date);                               // today's puzzle, caller frees
