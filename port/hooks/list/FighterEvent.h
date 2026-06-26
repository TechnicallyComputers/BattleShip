/*
 * FighterEvent.h — Fighter-system query/notify events for BattleShip mods.
 *
 * These are mid-function override points inside decomp fighter code where
 * no discrete engine symbol exists to detour (the surrounding functions are
 * thousand-line procs or fkind-keyed macros). Each event carries a mutable
 * payload: the engine fires it with vanilla defaults filled in, listeners
 * (e.g. CharacterEngine) overwrite the out-field, and the engine reads the
 * payload back after the CALL_EVENT. With zero listeners every event is a
 * no-op and the engine takes the vanilla path — behavior is byte-identical
 * without mods.
 *
 * Engine firing sites (all under #ifdef PORT in decomp/src):
 *   - FighterEnvColorQueryEvent      — ftdisplaymain.c ftDisplayMainProcDisplay.
 *                                      rgba in/out: 0 = no override, else the
 *                                      RGBA word forced as the env color.
 *   - FighterDamageDirectionApplyEvent — ftmain.c
 *                                      ftMainProcessHitCollisionStatsMain, after
 *                                      the engine computes default damage_lr.
 *                                      Listener derives the hitbox id from the
 *                                      attacker's attack_colls and overwrites
 *                                      this_fp->damage_lr when its override slot
 *                                      is set. Pointers are FTStruct* /
 *                                      FTAttackColl* passed as void* so this
 *                                      header stays decomp-type-free.
 *   - FighterHitboxSlotResetEvent    — ftmain.c ftMainParseMotionEvent
 *                                      make-attack path; notify-only. Listener
 *                                      zeroes its per-player per-hitbox override
 *                                      slot so stale state from a previous
 *                                      attack with the same id doesn't bleed in.
 *   - FighterParentKindResolveEvent  — ftcommonattack1/100.c fkind whitelists.
 *                                      resolved_fkind in/out: initialized to
 *                                      fkind; a listener that owns the
 *                                      synth->parent map rewrites it so a synth
 *                                      passes its parent's jab/rapid-jab gates.
 *   - FighterRapidJabStatusQueryEvent — ftcommonattack100.c Start/Loop/End
 *                                      SetStatus helpers. status_id in/out:
 *                                      -1 = use the vanilla fkind switch; else
 *                                      the per-character SR rapid-jab status id
 *                                      for the given phase (0=begin, 1=loop,
 *                                      2=end).
 *
 * Whole-function override points deliberately have NO event here — mods
 * detour the engine symbol via mod_install_hook instead:
 *   - ftMainSetStatus            (SR change_action_ per-status reset)
 *   - ftParamClearAttackCollAll  (SR end_hitbox_ reset-all)
 *   - ftSamusSpecialNGetChargeShotPosition (SR ball_position_fix_)
 *
 * Convention matches EngineEvent.h: REGISTER_EVENT in PortRegisterEvents
 * (port/hooks/Events.cpp) allocates the EventID at engine init; CALL_EVENT
 * fires it; mods subscribe with REGISTER_LISTENER.
 */
#pragma once

#include "ship/events/EventTypes.h"
#include "libultraship/bridge/eventsbridge.h"

DEFINE_EVENT(FighterEnvColorQueryEvent,
             int32_t player;
             uint32_t rgba;);

DEFINE_EVENT(FighterDamageDirectionApplyEvent,
             void* this_fp;
             void* attacker_fp;
             void* attack_coll;);

DEFINE_EVENT(FighterHitboxSlotResetEvent,
             int32_t player;
             int32_t slot;);

DEFINE_EVENT(FighterParentKindResolveEvent,
             int32_t fkind;
             int32_t resolved_fkind;);

DEFINE_EVENT(FighterRapidJabStatusQueryEvent,
             int32_t fkind;
             int32_t phase;
             int32_t status_id;);
