// How game screens show on screens of any proportions (src/mui_screen_plan.cc):
// phones keep the phone's layouts on the whole screen, tablets get windows of
// the tablet's, a foldable's inner screen (a little narrow for one) too, a
// little smaller, a phone at a small UI size windows; a square screen
// without room for a window the tablet's on the whole screen; areas a little
// short of a layout draw it smaller; screens fitting none take the least
// shrinking; the player's settings decide their part.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "mui_screen_plan.h"

using namespace fallout;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #condition);     \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    } while (false)

namespace {

bool is(const MuiScreenPlan& plan, bool windowed, bool tablet, float scale)
{
    bool ok = plan.windowed == windowed && plan.tablet == tablet && fabsf(plan.scale - scale) < 0.005f;
    if (!ok) {
        fprintf(stderr, "  plan: windowed %d, tablet %d, scale %.3f\n", plan.windowed ? 1 : 0, plan.tablet ? 1 : 0, plan.scale);
    }
    return ok;
}

MuiScreenPlan autoPlan(float width, float height)
{
    return muiScreenPlan(width, height, kMuiWindowsAuto, kMuiLayoutAuto);
}

} // namespace

int main()
{
    // Phones: 20:9, 16:9, a foldable's outer screen.
    CHECK(is(autoPlan(800.0f, 360.0f), false, false, 1.0f));
    CHECK(is(autoPlan(640.0f, 360.0f), false, false, 1.0f));
    CHECK(is(autoPlan(960.0f, 411.0f), false, false, 1.0f));

    // Tablets 7"-11", 4:3 and 16:10; a phone at 60% (a screen of that many
    // dp).
    CHECK(is(autoPlan(960.0f, 600.0f), true, true, 1.0f));
    CHECK(is(autoPlan(1024.0f, 768.0f), true, true, 1.0f));
    CHECK(is(autoPlan(1280.0f, 800.0f), true, true, 1.0f));
    CHECK(is(autoPlan(1333.0f, 600.0f), true, true, 1.0f));

    // A foldable's inner screen: with its camera's cutout a little narrow
    // for the window - it a little smaller (643 of 680); without the
    // cutout, at its size.
    CHECK(is(autoPlan(783.0f, 750.0f), true, true, 643.0f / 680.0f));
    CHECK(is(autoPlan(832.0f, 750.0f), true, true, 1.0f));

    // Square, no room for a window: the tablet's on the whole screen (a
    // little smaller, 638 of 680).
    CHECK(is(autoPlan(720.0f, 720.0f), false, true, 638.0f / 680.0f));

    // A long strip: the phone's a little smaller.
    CHECK(is(autoPlan(1600.0f, 300.0f), false, false, 280.0f / 300.0f));

    // Fitting none at 90%: the least shrinking (a tall browser window: the
    // phone's at 418/540), at least 75% (a tiny screen).
    CHECK(is(autoPlan(500.0f, 800.0f), false, false, 418.0f / 540.0f));
    CHECK(is(autoPlan(200.0f, 200.0f), false, false, 0.75f));

    // Settings: windows never - a tablet's whole screen in the tablet's;
    // always - a phone's window in the phone's, a little smaller.
    CHECK(is(muiScreenPlan(1280.0f, 800.0f, kMuiWindowsNever, kMuiLayoutAuto), false, true, 1.0f));
    CHECK(is(muiScreenPlan(800.0f, 360.0f, kMuiWindowsAlways, kMuiLayoutAuto), true, false, 284.0f / 300.0f));

    // Layouts chosen: the phone's in a tablet's window, the tablet's on a
    // phone (made to fit as far as it can).
    CHECK(is(muiScreenPlan(1280.0f, 800.0f, kMuiWindowsAuto, kMuiLayoutPhone), true, false, 1.0f));
    CHECK(is(muiScreenPlan(800.0f, 360.0f, kMuiWindowsAuto, kMuiLayoutTablet), false, true, 340.0f / 440.0f));

    printf("mui_screen_plan_test: ok\n");
    return 0;
}
