#include <sys/netplay_sim_quantize.h>

#include <sys/netpeer.h>
#include <sys/obj.h>

#include <math.h>
#include <stdlib.h>

extern char *getenv(const char *name);
extern int atoi(const char *s);

static sb32 sSYNetplaySimQuantizeEnvCache = -999;

static sb32 syNetplaySimQuantizeEnvEnabled(void)
{
	const char *env;

	if (sSYNetplaySimQuantizeEnvCache != -999)
	{
		return sSYNetplaySimQuantizeEnvCache;
	}
	env = getenv("SSB64_NETPLAY_SIM_F32_QUANTIZE");
	if (env == NULL)
	{
		sSYNetplaySimQuantizeEnvCache = TRUE;
		return TRUE;
	}
	sSYNetplaySimQuantizeEnvCache = (atoi(env) != 0) ? TRUE : FALSE;
	return sSYNetplaySimQuantizeEnvCache;
}

sb32 syNetplaySimQuantizeActive(void)
{
#if defined(SSB64_NETMENU)
	if (syNetplaySimQuantizeEnvEnabled() == FALSE)
	{
		return FALSE;
	}
	return syNetPeerIsVSSessionActive();
#else
	return FALSE;
#endif
}

f32 syNetplayQuantizeF32(f32 value)
{
	f64 scaled;

	if (syNetplaySimQuantizeActive() == FALSE)
	{
		return value;
	}
	scaled = (f64)value * 65536.0;
	if (scaled >= 0.0)
	{
		scaled = floor(scaled + 0.5);
	}
	else
	{
		scaled = ceil(scaled - 0.5);
	}
	return (f32)(scaled / 65536.0);
}

void syNetplayQuantizeVec3f(Vec3f *vec)
{
	if ((vec == NULL) || (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	vec->x = syNetplayQuantizeF32(vec->x);
	vec->y = syNetplayQuantizeF32(vec->y);
	vec->z = syNetplayQuantizeF32(vec->z);
}

void syNetplayQuantizeDObjAnimScalars(DObj *dobj)
{
	if ((dobj == NULL) || (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	dobj->anim_frame = syNetplayQuantizeF32(dobj->anim_frame);
	dobj->anim_wait = syNetplayQuantizeF32(dobj->anim_wait);
	dobj->anim_speed = syNetplayQuantizeF32(dobj->anim_speed);
	if (dobj->parent_gobj != NULL)
	{
		dobj->parent_gobj->anim_frame = dobj->anim_frame;
	}
}

void syNetplayQuantizeDObjTranslate(DObj *dobj)
{
	if ((dobj == NULL) || (syNetplaySimQuantizeActive() == FALSE))
	{
		return;
	}
	syNetplayQuantizeVec3f(&dobj->translate.vec.f);
}
