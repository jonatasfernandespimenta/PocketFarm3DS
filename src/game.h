// Shared types and the functions each module exports (one section per web module).
#pragma once
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cv.h"
#include "json.h"

static inline double jround(double x) { return floor(x + 0.5); }          // Math.round
static inline double frand(void) { return rand() / (RAND_MAX + 1.0); }    // Math.random
static inline double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }

typedef struct { double x, y, w, h; } Rect;
static inline bool rectsOverlap(Rect a, Rect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }
typedef struct { int x, y; } Tile;

enum { UP, DOWN, LEFT, RIGHT };
extern const char *DIR_NAME[4];
extern const int DV[4][2];   // unit vector per direction
int dirFrom(const char *s);

// ---------- map.c ----------
typedef struct PropDef {
    char key[48];
    Img *img;
    int rect[4];
    char kind[12], mask[10], stump[24], item[24];
    int frames;
    bool flat;
} PropDef;

typedef struct Prop {
    char id[24], type[48];
    int x, y;
    PropDef *def;
    const char *kind;
    double hp, shake;
    int dx, dy;
    Rect box, col;
    bool hasCol, flat;
    double foot;
    long uid; bool movable;     // house furniture
    char item[40];
} Prop;

typedef struct Map {
    int w, h;
    char **terrain; int nterrain;
    uint8_t *coll;              // collision layer
    Img *ground, *mask;
    Prop *props; int nprops, capprops;
    Rect extra[16]; int nextra;  // building colliders
    uint8_t *solid, *blocked;
    bool indoor, isHouse;
    Rect floor; Tile door;
} Map;

PropDef *propDef(const char *type);
Map *loadMap(const char *url, J *removed, J *stumps, const char *season);
void freeMap(Map *m);
Rect opaqueBox(Img *img, const int r[4]);
bool maskCollider(const char *mask, Rect box, double dx, double dy, Rect *out);
void setPropType(Prop *p, const char *type);
Prop *addProp(Map *m, Prop p);
void removeProp(Map *m, Prop *p);
void drawProp(Prop *p, double t);
void rebuildSolid(Map *m);
bool inBounds(Map *m, int x, int y);
bool isSolid(Map *m, int x, int y);
bool isBlocked(Map *m, int x, int y);
bool boxBlocked(Map *m, Rect box);
bool isWater(Map *m, int x, int y);
Prop *propAt(Map *m, int x, int y);
int colliders(Map *m, Rect *out, int max);

// ---------- world.c ----------
typedef struct Node { char node[12]; int x, y; double hp, shake; bool ladder; } Node;
const char *nextHop(const char *from, const char *to);
int genMine(Map *map, int level, int day, Node *out, int max);

// ---------- house.c ----------
const char *furnitureType(const char *item);
Map *buildRoom(int w, int d, const char *wall, const char *floor, J *placed, bool isHouse);
Map *buildHouseMap(J *s);
bool tryPlace(Map *map, const char *item, int x, int y, long uid, Rect *blockers, int nb, Prop *out);
PropDef *propDefNew(const char *key);

#include "hud.h"

// ---------- sfx.c / music.c ----------
void sfx(const char *name);
void sfxStep(const char *surface);
void setMuted(bool m);
void startMusic(void);
void setMusicMood(const char *mood);
void setMusicEnabled(bool on);

// ---------- terrain.c ----------
double thash(int x, int y, int s);
void paintTerrain(char **rows, int h, int seed, const char *season, Img **ground, Img **mask);
void waterFrames(Img *frames[16]);
void soilTiles(Img *dry[16], Img *wet[16]);

// ---------- sprites.c / assets ----------
Img *sprite(const char *name);
Img *I(const char *src);      // game images (Tiny Factory art restyled), cached

// ---------- player.c ----------
typedef struct Thunk { void (*fn)(void *arg); char arg[128]; } Thunk;
typedef struct Action {
    int row, frames; double dur;
    int overlay, ovRow;          // overlay sheet: -1 none, 0 tools, 1 rod
    Thunk onHit; bool hasHit;
    double hitAt; bool hold;
    double t; bool hit;
} Action;
typedef struct Player { double x, y; int dir; bool moving; double t; bool acting; Action a; } Player;
typedef struct ActOpts { int row, frames; double dur; int overlay, ovRow; double hitAt; bool hold; } ActOpts;

extern Player player;
void loadPlayerSprites(void);
void startAction(Player *p, ActOpts o, Thunk *onHit);
ActOpts actDefaults(void);
double updatePlayer(Player *p, double dt, double dx, double dy, bool (*blocked)(Rect));
Tile playerTile(Player *p);
void faceTile(Player *p, Tile t);
Rect feetBox(double x, double y);
Tile facingTile(Player *p);
void drawPlayer(Player *p);
void drawNpc(Img *img, double x, double y, int dir, bool moving, double t);

// ---------- farm.c ----------
J *soilAt(J *state, int x, int y);
int cropStage(J *tile);
bool isReady(J *tile);
void sleepCrops(J *state);
void drawSoil(J *state, Img *dry[16], Img *wet[16]);
void drawCrop(Img *img, int x, int y, J *tile);

// ---------- fx.c ----------
void fx_chips(double x, double y, uint32_t color, int n);
void fx_puff(double x, double y, uint32_t color, int n);
void fx_splash(double x, double y, int n);
void fx_sparkle(double x, double y, int n, uint32_t color);
void fx_fly(const char *item, double x, double y);
void fx_shake(double amp, double dur);
void fx_shakeOffset(int *x, int *y);
void fx_tap(double x, double y);
void fx_update(double dt);
void fx_drawWorld(void);
void fx_shadow(double x, double y, double w);
void fx_cursor(int tx, int ty, double time, bool ok, int w, int h);
void fx_ambientColor(double minutes, int out[3]);
typedef struct Light { double x, y, r, a; bool glow; } Light;
void fx_lighting(int vw, int vh, const int tint[3], Light *lights, int n);
void fx_updateAmbient(double dt, const char *kind, bool night, double camx, double camy, int vw, int vh);
void fx_drawAmbient(void);
void fx_drawFireflies(double camx, double camy);

// ---------- state.c ----------
J *newState(void);
J *loadState(void);
void saveState(J *s, double nowMs);
J *slotInfo(int n);
void useSlot(int n);
void deleteSlot(int n);
void resetSlot(int slot);
J *mascotOf(J *s);
bool hasBonus(J *s, const char *b);
bool albumDone(J *s);
bool commonsDone(J *s);
int comfort(J *s);
int comfortBonus(J *s);
int maxEnergy(J *s);
int count(J *s, const char *id);
int give(J *s, const char *id, int n);   // returns how many overflowed into s.overflow
bool take(J *s, const char *id, int n);
bool canAfford(J *s, J *cost);
bool pay(J *s, J *cost);
void earn(J *s, int n);
bool isFairDay(J *s);
double sellMult(J *s);

// ---------- systems.c ----------
typedef struct Rng { uint32_t s; } Rng;   // NULL Rng* = Math.random
typedef struct Fish { char id[24]; const char *rarity; double window; } Fish;
Rng rng(double seed);
double rngNext(Rng *r);
int randInt(Rng *r, int a, int b);
J *rollDrops(J *drops, Rng *r);
int seasonIndex(J *s);
J *season(J *s);
const char *seasonId(J *s);
int dayOfSeason(J *s);
int year(J *s);
bool isFestival(J *s);
bool listHas(J *list, const char *v);
bool inSeason(J *s, const char *crop);
bool available(J *s, const char *id);
J *friendOf(J *s, const char *name);
int hearts(J *s, const char *name);
int maxHearts(J *s);
J *talkTo(J *s, const char *name);
J *giveGift(J *s, const char *name, const char *item);
bool hasDiscount(J *s, const char *shop);
J *contractOffers(J *s);
const char *acceptContract(J *s, double id);
J *deliverContract(J *s, double id);
int taxAmount(J *s);
bool payTax(J *s);
int animalsIn(J *s, const char *home);
const char *buyAnimal(J *s, const char *kind, const char *name, double nowMs);
const char *touchAnimal(J *s, J *a);
Fish rollFish(J *s, Rng *r, bool river);
bool projectOpen(J *s, J *p);
bool buildProject(J *s, const char *id);
J *nextGoal(J *s, char *label, int n);
int checkGoals(J *s, J *done[], int max);
J *newDay(J *s);

// ---------- factory.c ----------
#define STEP 0.5   // seconds per simulation step
extern const int ROTATE[4];
int currentTick(void);
J *mdef(J *m);
int factoryTier(J *s);
bool machineCovers(J *m, int x, int y);
J *machineAt(J *s, int x, int y);
bool canPlace(J *s, const char *type, int x, int y, bool (*isFree)(int, int));
J *place(J *s, const char *type, int x, int y, int dir, double nowMs);
void pickUp(J *s, J *m);
bool accepts(J *m, const char *item);
int insert(J *s, J *m, const char *item);
Tile outputCell(J *m);
Tile outputCellOf(const char *type, int x, int y, int dir);
void step(J *s, Rng *r, int *sold, int *items);
bool isRunning(J *m);
void nightFactory(J *s, int *sold, int *items);

// ---------- shops.c / gacha.c / quests.c / tutorial.c / scene (ponder) ----------
void shops_questDone(J *quest);
void shops_npcMenu(J *npc, void (*shop)(void *), const char *action);
void shops_sellMenu(void);
void shops_boardMenu(void);
void shops_innMenu(void);
void shops_forgeMenu(void);
void shops_carpenterMenu(void);
void shops_fishMenu(void);
void shops_marketMenu(void);
void shops_mainMenu(void);
void shops_morningReport(J *log, J **goals, int ngoals);
void gacha_open(const char *set);
int checkQuests(J *s, J **done, int max);
void scene_step(void);
typedef struct Hint { bool has; char map[16]; int x, y; } Hint;
Hint tutorial_update(const char *mapId, double px, double py);
void hud_decor(bool on);
void hud_boot(void);

// ---------- game.c ----------
extern J *state;
bool isFlat(J *m);
double nowMs(void);
void changed(void);
void persist(void);
void refreshTools(void);
void equip(const char *id);
void selectTool(const char *id);
int toolList(const char **out);
J *copyLine(J *m0);
void machineMenu(J *m);
void drawMachine(J *m);
void sleepNow(void);
const char *game_mapName(void);
const char *game_clock(void);
const char *game_worldId(void);
void game_goTo(const char *id);
void game_goHere(void);
void game_onBuilt(const char *id);
void game_onProject(void);
void game_onHouse(void);
bool game_decorOn(void);
void game_setDecor(bool on);
const char *game_decorItem(void);
void game_pickDecor(const char *item);
bool game_npcTile(const char *name, Tile *t);
bool game_bedTile(Tile *t);
bool game_nearestDebris(const char **tool, Tile *t);
void game_quit(void);
extern double gtime;
void drawItem(const char *id, double x, double y);
void game_boot(void);
void game_frame(double dt);

// ---------- platform ----------
enum { BTN_A = 1, BTN_B = 2, BTN_X = 4, BTN_Y = 8, BTN_L = 16, BTN_R = 32, BTN_START = 64, BTN_SELECT = 128,
       BTN_UP = 256, BTN_DOWN = 512, BTN_LEFT = 1024, BTN_RIGHT = 2048 };
typedef struct Input { uint32_t down, held; bool touch, touchDown, touchUp; int tx, ty; } Input;
extern Input in;
char *readFile(const char *path, size_t *len);   // romfs/sdmc path → malloc'd, NUL-terminated
bool writeFile(const char *path, const char *data, size_t len);
void plog(const char *fmt, ...);
double profNow(void);    // ms
bool plat_debug(void);   // sdmc:/3ds/PocketFarm/debug exists (PF_DEBUG on the PC)

// ---- port industrial: lead (UI) ----
enum { BUY_TECH = 40, BUY_DRONE, BUY_BAG, BUY_CRAFT };   // extra shops_costRow actions (shops.c buyClick)
typedef struct Row Row;
Row *shops_costRow(const char *icon, const char *label, const char *sub, J *cost, bool can, const char *why, int act, const char *id);
void game_buyAction(int act, const char *id);   // game.c: BUY_TECH "home|tech", BUY_DRONE dock id
void shops_storeMenu(J *m);                     // the counter inside a warehouse
void shops_inboxMenu(void);                     // what didn't fit in the backpack

// ---- port industrial: B ----
// systems.c
const char *animalMap(J *s, J *a);                 // "farm" or the building's interior id
bool isGranja(J *s, const char *home);
int capacity(J *s, const char *home);
const char *installTech(J *s, const char *home, const char *tech, bool paid);   // error or NULL
const char *eat(J *s, const char *id);             // error or NULL
double industryTarget(J *s);
int industryBand(J *s);
double priceMult(J *s);
int animalPrice(J *s, const char *kind);
J *newDayRand(J *s, double (*rand)(void));         // newDay(state, rand); NULL rand = Math.random
// quests.c
bool questOpen(J *s, const char *id);

// ---- port industrial: A (state.c inventory, factory.c, cooking.c) ----
#define BAG_SLOTS 24
#define STACK 99
int slotsUsed(J *s);
int room(J *s, const char *id);            // INT_MAX for factory pieces (they don't take backpack space)
int countAll(J *s, const char *id);        // backpack + every warehouse
bool takeAll(J *s, const char *id, int n);
// factory.c
#define SUPPLY_RANGE 3
#define MAX_DRONES 3
#define DOCK_CAP 40
#define FEEDER_CAP 60
#define COLLECTOR_CAP 40
// rows(type): footprint height. A function-like macro, so file-scope `rows` arrays elsewhere (hud.c, terrain.c) don't clash.
int machineRows(const char *type);
#define rows(type) machineRows(type)
int cells(J *m, Tile *out, int max);
const char *storeFor(const char *item);
int stored(J *m);
bool canPickUp(J *m);
void ageCellars(J *s);
const char *machineIssue(J *s, J *m, char *text, int n);   // code ("nopower", "nofuel", "full", "refused", "blocked", "idle") or NULL
void growVines(J *s, bool isFall);
int harvestVine(J *s, J *m);               // grapes go to the backpack
J *droneCost(void);                        // DRONE_COST: { iron_bar, copper_bar, money }
J *newDrone(J *dock);
int powerNodes(J *s, J **out, int max);
// cooking.c
int addInput(J *s, J *m, const char *id);
int stationQueued(J *m);
const char *work(J *s, J *m, double *progress);
bool mix(J *s, J *m);
const char *startCook(J *s, J *m, const char *id);
const char *cookState(J *s, J *m);
int minutesLeft(J *s, J *m);
const char *collectCooked(J *s, J *m);
bool bottle(J *s, J *m);
J *stationContents(J *m);
