// capsules.js: capsule machine logic (pure: state in, state out).
#include <stdio.h>
#include "api.h"

static double rnd(Rng *r) { return r ? rngNext(r) : frand(); }
static const char *key(int id, char b[12]) { snprintf(b, 12, "%d", id); return b; }
static bool sameSet(J *c, const char *set) { const char *s = str(c, "set"); return set ? s && !strcmp(s, set) : !s; }

int capsulePrice(J *s) {
    return num(s, "lastCapsuleDay") == num(s, "day") ? num(DATA, "CAPSULE_PRICE") : num(DATA, "CAPSULE_FIRST_OF_DAY");
}

// Pick rarity by weight, then a critter of that rarity uniformly from `set` (NULL = the base collection).
J *rollCritter(Rng *rg, const char *set) {
    double total = 0;
    JFOR(d, table("RARITY")) total += num(d, "weight");
    double r = rnd(rg) * total;
    const char *rarity = "common";
    JFOR(d, table("RARITY")) if ((r -= num(d, "weight")) < 0) { rarity = d->string; break; }
    J *pool[64]; int n = 0;
    JFOR(c, table("CRITTERS")) if (!strcmp(str(c, "rarity"), rarity) && sameSet(c, set) && n < 64) pool[n++] = c;
    return n ? pool[(int)floor(rnd(rg) * n)] : NULL;
}

// Spend a ticket (if any) or money, add critter. Duplicates are kept (for trading) until converted.
// Returns NULL if broke.
J *buyCapsule(J *s, Rng *r, const char *set) {
    int price = 0;
    if (num(s, "tickets") > 0) setnum(s, "tickets", num(s, "tickets") - 1);
    else {
        price = set ? num(DATA, "FESTIVAL_CAPSULE_PRICE") : capsulePrice(s);
        if (num(s, "money") < price) return NULL;
        setnum(s, "money", num(s, "money") - price);
        if (!set) setnum(s, "lastCapsuleDay", num(s, "day"));
    }
    J *critter = rollCritter(r, set);
    if (!critter) return NULL;
    char b[12];
    J *cs = child(s, "critters");
    key(num(critter, "id"), b);
    bool isNew = !num(cs, b);
    setnum(cs, b, num(cs, b) + 1);
    J *res = cJSON_CreateObject();
    cJSON_AddItemToObject(res, "critter", cJSON_Duplicate(critter, true));
    cJSON_AddBoolToObject(res, "isNew", isNew);
    cJSON_AddNumberToObject(res, "price", price);
    return res;
}

// Trade one extra copy (never the last one) for tokens.
int convertDuplicate(J *s, int id) {
    J *critter = NULL;
    JFOR(c, table("CRITTERS")) if (num(c, "id") == id) critter = c;
    char b[12];
    J *cs = child(s, "critters");
    if (!critter || num(cs, key(id, b)) < 2) return 0;
    setnum(cs, b, num(cs, b) - 1);
    int tokens = num(jget(table("RARITY"), str(critter, "rarity")), "tokens");
    setnum(s, "tokens", num(s, "tokens") + tokens);
    return tokens;
}

void collectionProgress(J *s, int *have, int *total) {
    char b[12];
    *have = *total = 0;
    JFOR(c, table("CRITTERS")) { ++*total; if (num(jget(s, "critters"), key(num(c, "id"), b))) ++*have; }
}
