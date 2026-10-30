// Soil + crops (farm.js). state.soil is { "x,y": { wet, crop?, age } }.
#include <stdio.h>
#include "game.h"

J *soilAt(J *st, int x, int y) {
    char k[16]; snprintf(k, sizeof k, "%d,%d", x, y);
    return jget(jget(st, "soil"), k);
}

// 0..2 growing, 3 = ready to harvest
int cropStage(J *tile) {
    int days = num(jget(table("CROPS"), str(tile, "crop")), "days"), age = num(tile, "age");
    return age >= days ? 3 : (int)floor(age * 3.0 / days);
}

bool isReady(J *tile) { return tile && str(tile, "crop") && cropStage(tile) == 3; }

// Night: watered crops grow one day, all soil dries (sprinklers re-water after this).
void sleepCrops(J *st) {
    JFOR(tile, jget(st, "soil")) {
        if (str(tile, "crop") && has(tile, "wet")) setnum(tile, "age", num(tile, "age") + 1);
        setbool(tile, "wet", false);
    }
}

void drawSoil(J *st, Img *dry[16], Img *wet[16]) {
    JFOR(tile, jget(st, "soil")) {
        int x, y;
        sscanf(tile->string, "%d,%d", &x, &y);
        int m = !!soilAt(st, x, y - 1) | !!soilAt(st, x, y + 1) << 1 | !!soilAt(st, x + 1, y) << 2 | !!soilAt(st, x - 1, y) << 3;
        cv_draw((has(tile, "wet") ? wet : dry)[m], x * 16, y * 16);
    }
}

// Crops.png: each crop row is 32px tall; cols 2..5 are growth stages, drawn 16x32 so tall stages overflow
// upward. Row 0 has no space above, so it is drawn 16x16.
void drawCrop(Img *img, int x, int y, J *tile) {
    int row = num(jget(table("CROPS"), str(tile, "crop")), "row"), sx = (2 + cropStage(tile)) * 16;
    if (row == 0) cv_image(img, sx, 0, 16, 16, x * 16, y * 16, 16, 16);
    else cv_image(img, sx, row * 32 - 16, 16, 32, x * 16, y * 16 - 16, 16, 32);
}
