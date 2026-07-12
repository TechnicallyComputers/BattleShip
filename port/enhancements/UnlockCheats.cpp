#include "enhancements.h"

extern "C" {
#if defined(SSB64_NETMENU)
    /* Netplay: always unlock the full roster / stages / menus so peers share CSS/SSS. */
    int port_cheat_unlock_all()        { return 1; }
    int port_cheat_unlock_luigi()      { return 1; }
    int port_cheat_unlock_ness()       { return 1; }
    int port_cheat_unlock_captain()    { return 1; }
    int port_cheat_unlock_purin()      { return 1; }
    int port_cheat_unlock_inishie()    { return 1; }
    int port_cheat_unlock_soundtest()  { return 1; }
    int port_cheat_unlock_itemswitch() { return 1; }
#else
    int port_cheat_unlock_all()        { return port_enhancement_cvar_get_integer("gCheats.UnlockAll", 0); }
    int port_cheat_unlock_luigi()      { return port_enhancement_cvar_get_integer("gCheats.UnlockLuigi", 0); }
    int port_cheat_unlock_ness()       { return port_enhancement_cvar_get_integer("gCheats.UnlockNess", 0); }
    int port_cheat_unlock_captain()    { return port_enhancement_cvar_get_integer("gCheats.UnlockCaptain", 0); }
    int port_cheat_unlock_purin()      { return port_enhancement_cvar_get_integer("gCheats.UnlockPurin", 0); }
    int port_cheat_unlock_inishie()    { return port_enhancement_cvar_get_integer("gCheats.UnlockInishie", 0); }
    int port_cheat_unlock_soundtest()  { return port_enhancement_cvar_get_integer("gCheats.UnlockSoundTest", 0); }
    int port_cheat_unlock_itemswitch() { return port_enhancement_cvar_get_integer("gCheats.UnlockItemSwitch", 0); }
#endif
}
