// Critter trading, gifts, score and blueprint links between two players without a server (trade.js).
// Codes are 'TAG.' + base64 of the UTF-8 JSON, byte-compatible with the web build. No camera/QR scanning here.
// ponytail: codes are not signed, so they can be forged. Fine for a cozy single-player game.
#include <stdio.h>
#include <ctype.h>
#include "api.h"

static bool eq(const char *a, const char *b) { return a && b && !strcmp(a, b); }

// ---------- base64 (btoa / atob) ----------
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char *b64enc(const unsigned char *in, size_t n) {
    char *out = malloc((n + 2) / 3 * 4 + 1), *o = out;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        *o++ = B64[v >> 18 & 63]; *o++ = B64[v >> 12 & 63];
        *o++ = i + 1 < n ? B64[v >> 6 & 63] : '=';
        *o++ = i + 2 < n ? B64[v & 63] : '=';
    }
    *o = 0;
    return out;
}
// atob: ignores ASCII whitespace, '=' only as trailing padding; NULL when invalid
static char *b64dec(const char *s) {
    size_t n = strlen(s);
    char *out = malloc(n * 3 / 4 + 4);
    uint32_t v = 0; int bits = 0; size_t o = 0, k = 0;
    bool pad = false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') continue;
        if (c == '=') { pad = true; continue; }
        const char *p = strchr(B64, c);
        if (!p || !c || pad) { free(out); return NULL; }
        v = v << 6 | (uint32_t)(p - B64); bits += 6; k++;
        if (bits >= 8) { bits -= 8; out[o++] = v >> bits & 255; }
    }
    if (k % 4 == 1) { free(out); return NULL; }
    out[o] = 0;
    return out;
}
// enc(o) = btoa(unescape(encodeURIComponent(JSON.stringify(o)))): cJSON prints UTF-8 like JSON.stringify
static char *code(const char *tag, J *o) {
    char *js = cJSON_PrintUnformatted(o);
    char *b = b64enc((const unsigned char *)js, strlen(js));
    char *out = malloc(strlen(tag) + strlen(b) + 2);
    sprintf(out, "%s.%s", tag, b);
    free(js); free(b); cJSON_Delete(o);
    return out;
}
static J *codeResult(const char *tag, J *o) { char *c = code(tag, o); J *r = cJSON_CreateObject(); cJSON_AddStringToObject(r, "code", c); free(c); return r; }
static J *err(const char *msg) { J *r = cJSON_CreateObject(); cJSON_AddStringToObject(r, "error", msg); return r; }
// Math.random().toString(36).slice(2, 10)
static void nonce(char out[9]) { for (int i = 0; i < 8; i++) out[i] = "0123456789abcdefghijklmnopqrstuvwxyz"[rand() % 36]; out[8] = 0; }
static const char *farmName(J *s) { const char *n = str(s, "farmName"); return n && n[0] ? n : "Farm"; }
static bool truthy(J *v) {
    if (!v || cJSON_IsNull(v) || cJSON_IsFalse(v)) return false;
    if (cJSON_IsNumber(v)) return v->valuedouble != 0 && v->valuedouble == v->valuedouble;
    if (cJSON_IsString(v)) return v->valuestring[0];
    return true;
}

static J *critter(J *id) {
    if (!cJSON_IsNumber(id)) return NULL;
    JFOR(c, table("CRITTERS")) if (num(c, "id") == id->valuedouble) return c;
    return NULL;
}
static J *critterN(int id) { J *n = cJSON_CreateNumber(id); J *c = critter(n); cJSON_Delete(n); return c; }
static int owned(J *s, int id) { char k[16]; snprintf(k, sizeof k, "%d", id); return num(jget(s, "critters"), k); }
static void addCritter(J *s, int id, int d) {
    char k[16]; snprintf(k, sizeof k, "%d", id);
    J *cs = child(s, "critters");
    int n = num(cs, k) + d;
    if (n) setnum(cs, k, n); else delkey(cs, k);
}
static J *trades(J *s) { J *t = child(s, "trades"); child(t, "pending"); childarr(t, "used"); return t; }
static bool usedNonce(J *s, const char *n) { JFOR(u, jget(trades(s), "used")) if (cJSON_IsString(u) && eq(u->valuestring, n)) return true; return false; }

// A offers one duplicate of `give` and asks for `want`.
J *makeOffer(J *s, int give, int want, const char *from) {
    if (owned(s, give) < 2) return err("You can only offer duplicates.");
    if (!critterN(want) || give == want) return err("Pick a different critter to ask for.");
    char n[9]; nonce(n);
    J *p = cJSON_CreateObject();
    cJSON_AddNumberToObject(p, "give", give); cJSON_AddNumberToObject(p, "want", want);
    cJSON_AddItemToObject(jget(trades(s), "pending"), n, p);
    J *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "n", n);
    cJSON_AddNumberToObject(o, "give", give); cJSON_AddNumberToObject(o, "want", want);
    cJSON_AddStringToObject(o, "from", from ? from : farmName(s));
    return codeResult("PF1O", o);
}

// ---------- blueprints: [type, x, y, dir] packed as "type,x,y,dir;…" ----------
static const char *DIRS4[4] = { "up", "right", "down", "left" };
static int machineIndex(const char *t) { int i = 0; JFOR(m, table("MACHINE_IDS")) { if (eq(m->valuestring, t)) return i; i++; } return -1; }
static int dirIndex(const char *d) { for (int i = 0; i < 4; i++) if (eq(DIRS4[i], d)) return i; return -1; }
// Number(str): trims whitespace, '' → 0, NaN when it isn't a whole number literal
static double jsNumber(const char *a, const char *b) {
    while (a < b && isspace((unsigned char)*a)) a++;
    while (b > a && isspace((unsigned char)b[-1])) b--;
    if (a == b) return 0;
    char tmp[64]; size_t n = b - a; if (n >= sizeof tmp) return NAN;
    memcpy(tmp, a, n); tmp[n] = 0;
    char *e; double v = strtod(tmp, &e);
    return *e || isalpha((unsigned char)tmp[n - 1]) ? NAN : v;   // no "inf"/"nan"/hex spellings
}
static void numStr(char *b, int n, double v) { if (v == floor(v)) snprintf(b, n, "%.0f", v); else snprintf(b, n, "%.17g", v); }
static char *packPieces(J *ps) {
    size_t cap = 64 + jlen(ps) * 48, l = 0;
    char *out = malloc(cap); out[0] = 0;
    JFOR(p, ps) {
        char x[32], y[32];
        numStr(x, sizeof x, numv(jat(p, 1))); numStr(y, sizeof y, numv(jat(p, 2)));
        l += snprintf(out + l, cap - l, "%s%d,%s,%s,%d", l ? ";" : "", machineIndex(jat(p, 0)->valuestring), x, y, dirIndex(jat(p, 3)->valuestring));
    }
    return out;
}
static J *unpackPieces(const char *s) {
    J *out = cJSON_CreateArray();
    int nmax = num(DATA, "MAX_PIECES"), k = 0, nm = jlen(table("MACHINE_IDS"));
    const char *p = s;
    while (k < nmax) {
        const char *end = strchr(p, ';'); if (!end) end = p + strlen(p);
        double v[4]; int nv = 0;
        const char *f = p;
        while (nv < 4) {
            const char *c = memchr(f, ',', end - f); if (!c) c = end;
            v[nv++] = jsNumber(f, c);
            if (c == end) break;
            f = c + 1;
        }
        k++;
        if (nv == 4) {
            double t = v[0], x = v[1], y = v[2], d = v[3];
            if (t == floor(t) && t >= 0 && t < nm && x >= 0 && y >= 0 && x < 40 && y < 40 && d == floor(d) && d >= 0 && d < 4) {
                J *a = cJSON_CreateArray();
                cJSON_AddItemToArray(a, cJSON_CreateString(jat(table("MACHINE_IDS"), (int)t)->valuestring));
                cJSON_AddItemToArray(a, cJSON_CreateNumber(x)); cJSON_AddItemToArray(a, cJSON_CreateNumber(y));
                cJSON_AddItemToArray(a, cJSON_CreateString(DIRS4[(int)d]));
                cJSON_AddItemToArray(out, a);
            }
        }
        if (!*end) break;
        p = end + 1;
    }
    return out;
}
char *blueprintCode(J *s, J *bp) {
    J *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "from", farmName(s));
    if (str(bp, "name")) cJSON_AddStringToObject(o, "name", str(bp, "name"));
    char *p = packPieces(jget(bp, "pieces"));
    cJSON_AddStringToObject(o, "p", p);
    free(p);
    return code("PF1B", o);
}

// Gifts: one a day, up to 10 of an item; machines can't be gifted.
bool giftable(const char *id) { return id && jget(table("ITEMS"), id) && strncmp(id, "machine_", 8); }

// %XX decoding (decodeURIComponent of a link's #pf= part)
static void urlDecode(char *s) {
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) { char h[3] = { p[1], p[2], 0 }; *o++ = (char)strtol(h, NULL, 16); p += 2; }
        else *o++ = *p;
    }
    *o = 0;
}

// Accepts a bare code or a whole link (pasted).
J *readCode(const char *in) {
    char *c = strdup(in ? in : ""), *body = c;
    char *m = strstr(c, "#pf=");
    if (m) { body = m + 4; body[strcspn(body, " \t\r\n&")] = 0; urlDecode(body); }
    while (isspace((unsigned char)*body)) body++;
    size_t l = strlen(body); while (l && isspace((unsigned char)body[l - 1])) body[--l] = 0;
    char *dot = strchr(body, '.');
    J *res = NULL;
    if (dot) {
        *dot = 0;
        char *rest = dot + 1, *dot2 = strchr(rest, '.'); if (dot2) *dot2 = 0;   // split('.')[1]
        const char *tag = body;
        char *js = b64dec(rest);
        J *d = js ? cJSON_Parse(js) : NULL;
        free(js);
        if (cJSON_IsObject(d)) {
            if (eq(tag, "PF1O") && critter(jget(d, "give")) && critter(jget(d, "want"))) res = d;
            else if (eq(tag, "PF1R") && truthy(jget(d, "n"))) res = d;
            else if (eq(tag, "PF1C") && truthy(jget(d, "d")) && truthy(jget(d, "n")) && truthy(jget(d, "s"))) res = d;
            else if (eq(tag, "PF1B") && truthy(jget(d, "p"))) {
                J *pv = jget(d, "p");
                char nb[32];
                const char *ps = cJSON_IsString(pv) ? pv->valuestring : cJSON_IsNumber(pv) ? (numStr(nb, sizeof nb, pv->valuedouble), nb) : "";
                J *pieces = unpackPieces(ps);
                if (jlen(pieces)) {
                    res = cJSON_CreateObject();
                    cJSON_AddStringToObject(res, "kind", "blueprint");
                    if (jget(d, "from")) cJSON_AddItemToObject(res, "from", cJSON_Duplicate(jget(d, "from"), true));
                    if (jget(d, "name")) cJSON_AddItemToObject(res, "name", cJSON_Duplicate(jget(d, "name"), true));
                    cJSON_AddItemToObject(res, "pieces", pieces);
                    cJSON_Delete(d); free(c);
                    return res;
                }
                cJSON_Delete(pieces);
            } else if (eq(tag, "PF1G") && truthy(jget(d, "n")) && giftable(str(d, "item")) && num(d, "qty") > 0 && cJSON_IsNumber(jget(d, "qty"))) res = d;
        }
        if (res) {
            // { kind, ...data }
            const char *kind = eq(tag, "PF1O") ? "offer" : eq(tag, "PF1R") ? "reply" : eq(tag, "PF1C") ? "score" : "gift";
            delkey(res, "kind");
            cJSON_InsertItemInArray(res, 0, cJSON_CreateString(kind));
            cJSON *k = cJSON_GetArrayItem(res, 0);
            k->string = strdup("kind");
        } else cJSON_Delete(d);
    }
    free(c);
    return res ? res : err("Invalid code.");
}

// B accepts A's offer: B gives `want`, receives `give`. Returns the reply code for A.
J *acceptOffer(J *s, J *offer) {
    const char *n = str(offer, "n");
    if (n && usedNonce(s, n)) return err("That trade was already done.");
    int want = num(offer, "want"), give = num(offer, "give");
    if (!owned(s, want)) { char b[128]; snprintf(b, sizeof b, "You don't have %s.", str(critterN(want), "name")); return err(b); }
    addCritter(s, want, -1);
    addCritter(s, give, 1);
    cJSON_AddItemToArray(jget(trades(s), "used"), cJSON_CreateString(n ? n : ""));
    J *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "n", n ? n : "");
    return codeResult("PF1R", o);
}

// A applies B's reply: the pending offer completes.
J *applyReply(J *s, J *reply) {
    J *pend = jget(trades(s), "pending"), *p = str(reply, "n") ? jget(pend, str(reply, "n")) : NULL;
    if (!p) return err("That reply isn't for one of your offers (or was already used).");
    int give = num(p, "give"), want = num(p, "want");
    if (owned(s, give) < 1) return err("You no longer have the offered critter.");
    addCritter(s, give, -1);
    addCritter(s, want, 1);
    cJSON_Delete(cJSON_DetachItemViaPointer(pend, p));
    J *r = cJSON_CreateObject();
    J *got = critterN(want), *gave = critterN(give);
    cJSON_AddItemToObject(r, "got", got ? cJSON_Duplicate(got, true) : cJSON_CreateNull());
    cJSON_AddItemToObject(r, "gave", gave ? cJSON_Duplicate(gave, true) : cJSON_CreateNull());
    return r;
}

// Daily challenge score: { d: date, n: farm name, s: stars, t: steps, p: pieces }.
char *scoreCode(J *s, J *sc, const char *date) {
    J *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "d", date);
    cJSON_AddStringToObject(o, "n", farmName(s));
    JFOR(k, sc) { delkey(o, k->string); cJSON_AddItemToObject(o, k->string, cJSON_Duplicate(k, true)); }
    return code("PF1C", o);
}

// Everyone who sent you a link; keeps their latest daily score.
void noteFriend(J *s, const char *name, J *score) {
    if (!name || !name[0]) return;
    J *f = child(child(s, "friendsList"), name), *old = jget(f, "score");
    if (score && (!old || !eq(str(old, "d"), str(score, "d")) || num(score, "s") > num(old, "s") || (num(score, "s") == num(old, "s") && num(score, "t") < num(old, "t")))) {
        delkey(f, "score");
        cJSON_AddItemToObject(f, "score", cJSON_Duplicate(score, true));
    }
}

J *makeGift(J *s, const char *item, int qty, const char *day) {
    if (eq(str(s, "giftDay"), day)) return err("You already sent a gift today. More tomorrow!");
    if (!giftable(item) || qty < 1 || qty > 10 || !take(s, item, qty)) return err("Can't send that gift.");
    setstr(s, "giftDay", day);
    char n[9]; nonce(n);
    J *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "n", n);
    cJSON_AddStringToObject(o, "from", farmName(s));
    cJSON_AddStringToObject(o, "item", item);
    cJSON_AddNumberToObject(o, "qty", qty);
    return codeResult("PF1G", o);
}
J *acceptGift(J *s, J *gift) {
    const char *n = str(gift, "n");
    if (n && usedNonce(s, n)) return err("That gift was already received.");
    cJSON_AddItemToArray(jget(trades(s), "used"), cJSON_CreateString(n ? n : ""));
    give(s, str(gift, "item"), fmin(10, num(gift, "qty")));
    J *r = cJSON_CreateObject();
    cJSON_AddTrueToObject(r, "ok");
    return r;
}
