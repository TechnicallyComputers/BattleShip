/*
 * playertint.c — give each player a distinct fighter color.
 *
 * A complete, single-purpose example of the Fighter query/override event
 * system. It subscribes to FighterEnvColorQueryEvent and, for every
 * fighter the engine is about to draw, forces that player's environment
 * color to a fixed per-player tint (P1 red, P2 blue, P3 green, P4 yellow).
 * Handy for telling players apart at a glance — and a clean demonstration
 * of a Fighter event whose payload is all primitives, so there are no
 * decomp-struct casts to get wrong.
 *
 * How the event works (see port/hooks/list/FighterEvent.h):
 *   - The engine fires FighterEnvColorQueryEvent from ftDisplayMainProcDisplay
 *     once per fighter per frame, with `rgba` pre-filled to 0 (= "no
 *     override, take the vanilla color").
 *   - A listener may overwrite `rgba` with any non-zero RGBA word to force
 *     that color instead. We do exactly that, keyed on `player`.
 *   - With this mod unloaded the event has zero listeners and the engine
 *     takes the vanilla path — fighters look exactly as they always did.
 *
 * RGBA byte order matches the guide's example (0xFF0000FF = opaque red):
 * 0xRRGGBBAA.
 */
#include "mod.h"

#include "port/hooks/Events.h"

/* Opaque per-player tints, indexed by the 0-based player id. */
static const uint32_t kPlayerColors[4] = {
    0xFF0000FF, /* P1 — red    */
    0x0000FFFF, /* P2 — blue   */
    0x00FF00FF, /* P3 — green  */
    0xFFFF00FF, /* P4 — yellow */
};

static ListenerID s_envColorListenerID;

static void OnEnvColorQuery(IEvent* event) {
    FighterEnvColorQueryEvent* e = (FighterEnvColorQueryEvent*)event;

    /* player is 0-based; guard the index so a 5th slot (if one ever
     * fires) falls through to the vanilla color rather than reading
     * past the palette. */
    if (e->player >= 0 && e->player < 4) {
        e->rgba = kPlayerColors[e->player];
    }
}

MOD_INIT() {
    s_envColorListenerID = REGISTER_LISTENER(FighterEnvColorQueryEvent,
                                             EVENT_PRIORITY_NORMAL,
                                             OnEnvColorQuery);
    mod_log("[PlayerTint] init OK — per-player fighter tints active\n");
}

MOD_EXIT() {
    UNREGISTER_LISTENER(FighterEnvColorQueryEvent, s_envColorListenerID);
    mod_log("[PlayerTint] exit OK\n");
}
