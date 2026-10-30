// Bottom-screen UI (hud.js + the DOM in index.html): HUD, hotbar, menus, album, toasts, floaters, fade.
// Menus are built like the JS openMenu(title, items): begin, add rows, end. A row's callback gets a copy of
// `arg` (closure data) and returns true to keep the menu open (it is then rebuilt with menu_refresh's builder).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"

// text: fonts "px" (Jersey 10, titles/numbers), "body" (Nunito), "small". ':name:' tokens draw UI icons.
int text_width(const char *font, const char *s);
int text_draw(const char *font, const char *s, int x, int y, uint32_t color, int maxw);   // wraps at maxw (0 = no wrap); returns height
int text_lineh(const char *font);
// icon specs: "heart" (UI icon), "item:<id>", "tool:<id>", "critter:<id>", "sprite:<name>"
void drawIcon(const char *spec, int x, int y, int scale);

void toast(const char *fmt, ...);
void floatText(double wx, double wy, const char *fmt, ...);
void hud_fade(void (*fn)(void));
bool hud_busy(void);              // a menu / album / gacha / fade / title is up: the world pauses
void hud_back(void);              // B while busy
void hud_render(void);            // money, energy, day… changed (cheap: the HUD redraws every frame anyway)
void hud_buildHotbar(const char **tools, int n);
void hud_frame(double dt);        // update + draw the bottom screen (and the top-screen overlays)
void hud_drawFloaters(double camx, double camy, int k);   // k = world zoom
void hud_decor(bool on);
void hud_boot(void);
void hud_setQuests(const char **titles, const char **progress, int n);   // tracker rows (tap → quest log)
void hud_objective(const char *icon, const char *label, bool tappable);  // NULL label hides the chip
void hud_hint(const char *tool, bool menuButton);                        // pulse a hotbar tool / the menu button

typedef struct Row Row;
typedef bool (*RowFn)(void *arg);
void menu_begin(const char *title);
void menu_refresh(void (*fn)(void *), const void *arg, size_t n);
void menu_text(const char *fmt, ...);
Row *menu_row(const char *label, const char *sub, RowFn fn, const void *arg, size_t n);
void row_icon(Row *r, const char *icon);
void row_price(Row *r, int price);
void row_disabled(Row *r);
void row_stay(Row *r);            // don't close after the click
void menu_say(const char *name, const char *portrait, const char *text);  // speech box, types itself out
void menu_title(const char *title);                                       // section header inside the body
// slot grid: tap = onPick if given, else select and show name/info (+ an action button)
void menu_slot(const char *icon, int count, const char *name, const char *shortName, const char *info, bool selected);
void slot_pick(RowFn fn, const void *arg, size_t n);
void slot_action(const char *label, RowFn fn, const void *arg, size_t n);
void slot_dim(void);              // silhouette (album: not owned yet)
// a row of item tiles joined by arrows: icon specs, "→" or "↓" between them
void menu_diagram(const char **icons, const char **labels, int n);
// text box (3DS keyboard) + button
void menu_input(const char *placeholder, const char *value, const char *button, void (*fn)(const char *text, void *arg), const void *arg, size_t n);
// custom drawn block (Ponder scenes, previews): draw(x, y, w, arg) paints into the bottom screen
void menu_canvas(int h, void (*draw)(int x, int y, int w, void *arg), const void *arg, size_t n);
void menu_end(void);
void hud_closeMenu(void);
bool hud_isMenuOpen(void);
void hud_setOverlay(bool (*frame)(double dt));   // full-screen scene (gacha): returns false when done

bool plat_keyboard(const char *hint, const char *initial, char *out, int n);   // system keyboard
