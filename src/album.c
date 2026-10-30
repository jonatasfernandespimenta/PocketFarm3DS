// hud.js openAlbum / albumHelp / showCritter: the critter album, built with the menu API (index.html #album).
#include <stdio.h>
#include "api.h"

static J *critterById(int id) { JFOR(c, table("CRITTERS")) if (num(c, "id") == id) return c; return NULL; }
static int owned(int id) { char k[12]; snprintf(k, sizeof k, "%d", id); return num(jget(state, "critters"), k); }

static bool toAlbum(void *arg) { openAlbum(); return false; }
static bool toHelp(void *arg) { albumHelp(); return false; }
static bool noop(void *arg) { return false; }

static bool setMascot(void *arg) {
    int id = *(int *)arg;
    if (num(state, "mascot") == id && cJSON_IsNumber(jget(state, "mascot"))) cJSON_ReplaceItemInObject(state, "mascot", cJSON_CreateNull());
    else setnum(state, "mascot", id);
    changed();
    openAlbum();
    return false;
}
static bool convert(void *arg) { convertDuplicate(state, *(int *)arg); changed(); openAlbum(); return false; }

static bool showCritter(void *arg) {
    int id = *(int *)arg, n = owned(id);
    J *c = critterById(id), *r = jget(table("RARITY"), str(c, "rarity"));
    bool isMascot = cJSON_IsNumber(jget(state, "mascot")) && num(state, "mascot") == id;
    char b[200], ic[24];
    menu_begin(str(c, "name"));
    snprintf(b, sizeof b, "%s · you have %d", str(r, "name"), n);
    snprintf(ic, sizeof ic, "critter:%d", id);
    Row *row = menu_row(b, str(c, "desc"), NULL, NULL, 0);
    row_icon(row, ic); row_disabled(row);
    snprintf(b, sizeof b, "Bonus: %s. Your mascot follows you everywhere.", str(table("BONUSES"), str(c, "bonus")));
    row_stay(menu_row(isMascot ? "Remove as mascot" : "Set as mascot", b, setMascot, &id, sizeof id));
    char l[64]; snprintf(l, sizeof l, "Trade 1 duplicate for %d tokens", (int)num(r, "tokens"));
    row = menu_row(l, n > 1 ? "Keep duplicates to trade with friends" : "Need at least 2", convert, &id, sizeof id);
    row_stay(row);
    if (n < 2) row_disabled(row);
    row_stay(menu_row("Back", NULL, toAlbum, NULL, 0));   // web: closes the menu, back to the album under it
    menu_end();
    return false;
}

void openAlbum(void) {
    menu_begin("Critter Album");
    int have, total;
    collectionProgress(state, &have, &total);
    // #album-progress; ponytail: no meter bar (menu_canvas rows grow every frame in hud.c for now)
    menu_text("%d / %d  ·  %d tokens · :ticket: %d", have, total, (int)num(state, "tokens"), (int)num(state, "tickets"));
    Row *r = menu_row("What are critters for?", NULL, toHelp, NULL, 0);   // the header's "?" button
    row_icon(r, "question"); row_stay(r);
    const char *lastSet = "";
    JFOR(c, table("CRITTERS")) {
        const char *set = str(c, "set") ? str(c, "set") : NULL;
        if (strcmp(set ? set : "", lastSet ? lastSet : "") || c == table("CRITTERS")->child) {
            char t[64];
            if (set) {
                const char *name = set;
                JFOR(se, table("SEASONS")) if (!strcmp(str(se, "id"), set)) name = str(se, "name");
                snprintf(t, sizeof t, "%s (festival only)", name);
            } else snprintf(t, sizeof t, "Town collection");
            menu_title(t);
            lastSet = set ? set : "";
        }
        int id = num(c, "id"), n = owned(id);
        bool mascot = cJSON_IsNumber(jget(state, "mascot")) && num(state, "mascot") == id;
        char ic[24], info[200];
        snprintf(ic, sizeof ic, "critter:%d", id);
        J *rr = jget(table("RARITY"), str(c, "rarity"));
        // ponytail: the mascot cell's gold outline + star → the slot selector; rarity color → info line
        if (n) snprintf(info, sizeof info, "%s%s · %s", mascot ? ":star: Mascot · " : "", str(rr, "name"), str(table("BONUSES"), str(c, "bonus")));
        else snprintf(info, sizeof info, "%s", str(rr, "name"));
        menu_slot(ic, n, n ? str(c, "name") : "???", NULL, info, mascot);
        if (n) slot_pick(showCritter, &id, sizeof id);
        else { slot_pick(noop, NULL, 0); slot_dim(); }
    }
    menu_end();
}

void albumHelp(void) {
    menu_begin("What are critters for?");
    menu_text(":book: Collection — 20 town critters (12 common, 6 rare, 2 legendary) + 20 seasonal ones, only from the festival machine (day 13 of each season). Missing ones show as silhouettes.");
    menu_text(":star: Mascot — tap a critter and pick \"mascot\". It follows you on every map and gives a small bonus:");
    JFOR(bo, table("BONUSES")) {
        char names[400] = "";
        JFOR(c, table("CRITTERS")) if (str(c, "bonus") && !strcmp(str(c, "bonus"), bo->string)) {
            if (names[0]) strncat(names, ", ", sizeof names - strlen(names) - 1);
            strncat(names, str(c, "name"), sizeof names - strlen(names) - 1);
        }
        menu_text("   • %s: %s", bo->valuestring, names);
    }
    menu_text(":trophy: Collection bonus — all commons: +5%% sales. Full album: +3 max energy.");
    menu_text(":rotate: Duplicates — trade 1 duplicate for tokens (common 1, rare 5, legendary 20). At the Inn, 10 tokens = 1 capsule ticket. Your last copy is never traded.");
    menu_text(":mail: Trade with friends — Menu → Trade critters: offer a duplicate, send the code, get the reply.");
    menu_text("No critter is required: bonuses are just a treat.");
    row_stay(menu_row("Got it", NULL, toAlbum, NULL, 0));
    menu_end();
}
