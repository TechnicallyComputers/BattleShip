#include <sys/netplay_statusvars_bank.h>

#if defined(PORT) && defined(SSB64_NETMENU)

#include <ft/fighter.h>
#include <ft/ftdef.h>
#include <ft/fttypes.h>

#include <string.h>

extern void port_log(const char *fmt, ...);

/*
 * Sidecar storage: nFTStatusVarsOverlayCount independent overlay slots per fighter.
 * sizeof(union FTStatusVars) per slot (56 B); ~2.2 KB × 4 players — trivial next to the ring.
 * Keyed by sim player slot so rollback load / fighter respawn cannot strand state: the bank
 * outlives any one GObj allocation for the match, and SetStatus / snapshot apply retag it.
 */
static u8 s_statusvars_bank[GMCOMMON_PLAYERS_MAX][nFTStatusVarsOverlayCount][sizeof(union FTStatusVars)];
static s32 s_statusvars_live_overlay[GMCOMMON_PLAYERS_MAX];
static sb32 s_statusvars_slot_valid[GMCOMMON_PLAYERS_MAX];

static sb32 syNetplayStatusVarsBankPlayerValid(s32 player)
{
    return ((player >= 0) && (player < GMCOMMON_PLAYERS_MAX)) ? TRUE : FALSE;
}

static sb32 syNetplayStatusVarsBankOverlayValid(s32 overlay)
{
    return ((overlay >= 0) && (overlay < nFTStatusVarsOverlayCount)) ? TRUE : FALSE;
}

void syNetplayStatusVarsBankResetSession(void)
{
    memset(s_statusvars_bank, 0, sizeof(s_statusvars_bank));
    for (s32 p = 0; p < GMCOMMON_PLAYERS_MAX; p++)
    {
        s_statusvars_live_overlay[p] = nFTStatusVarsOverlayNone;
        s_statusvars_slot_valid[p] = FALSE;
    }
}

void syNetplayStatusVarsBankInitFighter(s32 player)
{
    if (syNetplayStatusVarsBankPlayerValid(player) == FALSE)
    {
        return;
    }
    memset(s_statusvars_bank[player], 0, sizeof(s_statusvars_bank[player]));
    s_statusvars_live_overlay[player] = nFTStatusVarsOverlayNone;
    s_statusvars_slot_valid[player] = TRUE;
}

/*
 * Resolve a fighter's bank row. fp->player is authoritative in VS; fall back to -1 (invalid)
 * rather than guessing so accessors degrade to the union instead of cross-wiring players.
 */
static s32 syNetplayStatusVarsBankPlayerFromFp(const FTStruct *fp)
{
    if (fp == NULL)
    {
        return -1;
    }
    return fp->player;
}

void *syNetplayStatusVarsBankSlot(FTStruct *fp, FTStatusVarsOverlay overlay)
{
    s32 player = syNetplayStatusVarsBankPlayerFromFp(fp);

    if ((syNetplayStatusVarsBankPlayerValid(player) == FALSE) ||
        (syNetplayStatusVarsBankOverlayValid((s32)overlay) == FALSE))
    {
        return NULL;
    }
    if (s_statusvars_slot_valid[player] == FALSE)
    {
        syNetplayStatusVarsBankInitFighter(player);
    }
    return &s_statusvars_bank[player][(s32)overlay][0];
}

void *syNetplayStatusVarsBankSlotOrUnion(FTStruct *fp, FTStatusVarsOverlay overlay, void *union_member)
{
    void *slot = syNetplayStatusVarsBankSlot(fp, overlay);

    return (slot != NULL) ? slot : union_member;
}

s32 syNetplayStatusVarsBankLiveOverlay(s32 player)
{
    if (syNetplayStatusVarsBankPlayerValid(player) == FALSE)
    {
        return nFTStatusVarsOverlayNone;
    }
    return s_statusvars_live_overlay[player];
}

void syNetplayStatusVarsBankSetLiveOverlay(s32 player, s32 overlay)
{
    if (syNetplayStatusVarsBankPlayerValid(player) == FALSE)
    {
        return;
    }
    s_statusvars_live_overlay[player] = overlay;
}

/*
 * Push the live overlay slot into the FTStruct union so any code path that still reads
 * fp->status_vars.* raw (hash folds that bypass accessors, diag dumps) sees the active overlay.
 * Residual raw *writers* are the C2b risk the witness + audit own; projection is one-way.
 */
void syNetplayStatusVarsBankProjectUnion(FTStruct *fp)
{
    s32 player;
    s32 overlay;

    if (fp == NULL)
    {
        return;
    }
    player = syNetplayStatusVarsBankPlayerFromFp(fp);
    overlay = (syNetplayStatusVarsBankPlayerValid(player) != FALSE) ? s_statusvars_live_overlay[player]
                                                                    : (s32)nFTStatusVarsOverlayNone;
    if (syNetplayStatusVarsBankOverlayValid(overlay) == FALSE)
    {
        return;
    }
    memcpy(&fp->status_vars, &s_statusvars_bank[player][overlay][0], sizeof(fp->status_vars));
}

void syNetplayStatusVarsBankCopyOverlayOut(s32 player, s32 overlay, void *dst, u32 len)
{
    if ((dst == NULL) || (syNetplayStatusVarsBankPlayerValid(player) == FALSE) ||
        (syNetplayStatusVarsBankOverlayValid(overlay) == FALSE))
    {
        return;
    }
    if (len > sizeof(union FTStatusVars))
    {
        len = sizeof(union FTStatusVars);
    }
    memcpy(dst, &s_statusvars_bank[player][overlay][0], len);
}

void syNetplayStatusVarsBankCopyOverlayIn(s32 player, s32 overlay, const void *src, u32 len)
{
    if ((src == NULL) || (syNetplayStatusVarsBankPlayerValid(player) == FALSE) ||
        (syNetplayStatusVarsBankOverlayValid(overlay) == FALSE))
    {
        return;
    }
    if (s_statusvars_slot_valid[player] == FALSE)
    {
        syNetplayStatusVarsBankInitFighter(player);
    }
    if (len > sizeof(union FTStatusVars))
    {
        len = sizeof(union FTStatusVars);
    }
    memcpy(&s_statusvars_bank[player][overlay][0], src, len);
}

#endif /* PORT && SSB64_NETMENU */
