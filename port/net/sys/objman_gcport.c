#include <sys/objman_gcport.h>

/*
 * Placeholder traversal metrics: full gcRunAll-faithful hashing would need the
 * process queue walk in objman.c (private there). Zeros keep NetSync lines valid
 * when GC traversal diag is enabled.
 */
u32 gcPortHashGcRunAllTraversalFingerprint(void)
{
	return 0U;
}

void gcPortGcRunAllTraversalFingerprintEx(u32 *gch, u32 *ngobj, u32 *ngobj_run, u32 *nproc_run)
{
	if (gch != NULL)
	{
		*gch = 0U;
	}
	if (ngobj != NULL)
	{
		*ngobj = 0U;
	}
	if (ngobj_run != NULL)
	{
		*ngobj_run = 0U;
	}
	if (nproc_run != NULL)
	{
		*nproc_run = 0U;
	}
}

void gcPortSnprintGcRunAllTraversalHeadPairs(char *buf, size_t bufsize, int max_pairs)
{
	(void)max_pairs;

	if ((buf != NULL) && (bufsize > 0U))
	{
		buf[0] = '\0';
	}
}
