#ifndef SYS_NETPLAY_SIM_QUANTIZE_H
#define SYS_NETPLAY_SIM_QUANTIZE_H

#include <PR/ultratypes.h>
#include <ssb_types.h>
#include <sys/objdef.h>

/* TRUE when netplay F32 normalization is active for this sim step. */
extern sb32 syNetplaySimQuantizeActive(void);

/* Round to a shared 1/65536 grid (double intermediate) on all peers. */
extern f32 syNetplayQuantizeF32(f32 value);
extern void syNetplayQuantizeVec3f(Vec3f *vec);

extern void syNetplayQuantizeDObjAnimScalars(DObj *dobj);
extern void syNetplayQuantizeDObjTranslate(DObj *dobj);

#endif /* SYS_NETPLAY_SIM_QUANTIZE_H */
