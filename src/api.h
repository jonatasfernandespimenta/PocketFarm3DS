// Cross-module API of the screens/logic still being ported. One C function per web export, same name.
// Convention: a JS function that returns an object returns a NEW cJSON tree with the same keys/values (data
// defs inside it — a quest, a critter — are cJSON_Duplicate copies); the caller cJSON_Delete()s it.
// `g` (main.js's game API) is gone: call game.c's functions directly (changed, persist, refreshTools, equip,
// toast, game_onBuilt, game_onProject, game_onHouse, game_goTo, sleepNow…; see game.h).
#pragma once
#include "game.h"

// ---------- quests.c (quests.js) ----------
int activeQuests(J *s, J **out, int max);                     // references into table("QUESTS"), don't free
const char *questProgress(J *s, J *quest, char *buf, int n);  // q.progress(state); NULL when it has none / ""
bool questHint(J *s, J *quest, Hint *out);                    // q.hint(questQuery()) → target tile on a map
J *coachStep(J *s);                                           // { quest, step } copy or NULL
J *deliverQuest(J *s, const char *npcName);                   // { quest, reward: [strings] } / { quest, missing: true } / NULL
// checkQuests(s, done, max) is declared in game.h (done[] = { quest, reward } trees, caller frees)

// ---------- tutorial.c (tutorial.js + main.js updateTutorial / objective chip) ----------
// Hint tutorial_update(const char *mapId, double px, double py) and void tutorial_tapObjective(void): game.h / hud.h

// ---------- trade.c (trade.js, no camera: codes are typed with the keyboard / shown as text + QR) ----------
J *makeOffer(J *s, int give, int want, const char *from);     // { code } | { error }
J *readCode(const char *code);                                // { kind: offer|reply|score|blueprint|gift, ... } | { error }
J *acceptOffer(J *s, J *offer);                               // { code } | { error }
J *applyReply(J *s, J *reply);                                // { got, gave } | { error }
char *scoreCode(J *s, J *sc, const char *date);               // malloc'd
void noteFriend(J *s, const char *name, J *score);
bool giftable(const char *id);
J *makeGift(J *s, const char *item, int qty, const char *day);
J *acceptGift(J *s, J *gift);
char *blueprintCode(J *s, J *bp);                             // malloc'd
// MAX_PIECES is in data.json (num(DATA, "MAX_PIECES"))

// ---------- workshop.c / bench.c (workshop.js, bench.js) ----------
void today(char out[11]);                                     // "YYYY-MM-DD" (real date)
bool better(J *a, J *b);                                      // scores { s, t, p }
const char *scoreText(J *sc, char *buf, int n);
J *myScore(J *s);                                             // { s, t, p } or NULL
void playDaily(J *rival);                                     // rival score or NULL
void workshopMenu(void);

// ---------- capsules.c / gacha.c / album.c / title.c ----------
int capsulePrice(J *s);
J *buyCapsule(J *s, Rng *r, const char *set);                 // { critter, isNew, price } or NULL
int convertDuplicate(J *s, int id);
void collectionProgress(J *s, int *have, int *total);
void openAlbum(void);                                         // the critter album (hud.js openAlbum)
void albumHelp(void);
// gacha_open(set) is in game.h; title_frame(dt) in hud.c

// ---------- shops.c (shops.js + main.js machineMenu) / scene.c (main.js Ponder + drawScene) ----------
void shops_sellMenu(void);
void shops_questLog(void);
void shops_projectsMenu(void);
void shops_factoryGuide(const char *id);
void shops_blueprintMenu(J *bp, bool received);
void shops_openTradeCode(const char *code);
void shops_savesMenu(void);
void shops_friendsMenu(void);
void shops_shareScore(void);
// A sandbox factory drawn inside a menu (menu_canvas): Ponder demos and blueprint previews.
// scene_ponder(id or NULL, sc) shows a guide scene; drawScene paints machines of a sandbox state at (x, y).
void scene_ponder(const char *id, J *sc);
void drawScene(int x, int y, J *sandbox, int w, int h, J **shown, int nshown, double (*drop)(J *m), Tile *mark, int nmark);
