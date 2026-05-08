#ifndef SYS_NETCONTROLLERFREEZE_H
#define SYS_NETCONTROLLERFREEZE_H

/**
 * A/B diagnostic (PORT + SSB64_NETMENU only):
 * Set env SSB64_NETPLAY_CONTROLLER_FREEZE_SNAPSHOT=1 so each `gcRunAll` pass
 * snapshots `gSYControllerDevices[]` at entry and repoints human fighters'
 * `fp->input.controller` at the snapshot. Restores live pointers when the
 * outermost `gcRunAll` returns.
 *
 * While `syNetRollbackIsResimulating()` is true, the snapshot is refreshed on
 * every nested `gcRunAll` so rollback republished inputs are still visible.
 */

extern void syNetControllerFreezeGcRunAllEnter(void);
extern void syNetControllerFreezeGcRunAllLeave(void);

#endif /* SYS_NETCONTROLLERFREEZE_H */
