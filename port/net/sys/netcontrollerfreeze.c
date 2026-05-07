#include <sys/netcontrollerfreeze.h>

#include <PR/os.h> /* MAXCONTROLLERS */
#include <ft/fighter.h>
#include <ft/ftdef.h>
#include <sys/controller.h>
#include <sys/objdef.h>
#include <sys/objman.h>
#include <sys/netrollback.h>

#include <stdlib.h>
#include <string.h>

static SYController s_snap[MAXCONTROLLERS];
static s32 s_depth;

static int netctrlfreeze_env_on(void)
{
	const char *v = getenv("SSB64_NETPLAY_CONTROLLER_FREEZE_SNAPSHOT");

	return (v != NULL) && (v[0] == '1') && (v[1] == '\0');
}

static void netctrlfreeze_patch_man_fighters(void)
{
	GObj *gobj;

	for (gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; gobj != NULL; gobj = gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(gobj);

		if (fp == NULL)
		{
			continue;
		}
		if (fp->pkind != nFTPlayerKindMan)
		{
			continue;
		}
		if (fp->player >= MAXCONTROLLERS)
		{
			continue;
		}
		fp->input.controller = &s_snap[fp->player];
	}
}

static void netctrlfreeze_restore_man_fighters(void)
{
	GObj *gobj;

	for (gobj = gGCCommonLinks[nGCCommonLinkIDFighter]; gobj != NULL; gobj = gobj->link_next)
	{
		FTStruct *fp = ftGetStruct(gobj);

		if (fp == NULL)
		{
			continue;
		}
		if (fp->pkind != nFTPlayerKindMan)
		{
			continue;
		}
		if (fp->player >= MAXCONTROLLERS)
		{
			continue;
		}
		fp->input.controller = &gSYControllerDevices[fp->player];
	}
}

void syNetControllerFreezeGcRunAllEnter(void)
{
	if (netctrlfreeze_env_on() == 0)
	{
		return;
	}
	s_depth++;
	if (syNetRollbackIsResimulating() != FALSE)
	{
		(void)memcpy(s_snap, gSYControllerDevices, sizeof(s_snap));
		netctrlfreeze_patch_man_fighters();
		return;
	}
	if (s_depth == 1)
	{
		(void)memcpy(s_snap, gSYControllerDevices, sizeof(s_snap));
		netctrlfreeze_patch_man_fighters();
	}
}

void syNetControllerFreezeGcRunAllLeave(void)
{
	if (netctrlfreeze_env_on() == 0)
	{
		return;
	}
	if (s_depth <= 0)
	{
		return;
	}
	s_depth--;
	if (s_depth == 0)
	{
		netctrlfreeze_restore_man_fighters();
	}
}
