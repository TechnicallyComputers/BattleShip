#if defined(__ANDROID__)

#include "android_debug_restart.h"

static volatile int s_in_process_restart;

void ssb64_android_begin_in_process_restart(void)
{
	s_in_process_restart = 1;
}

int ssb64_android_consume_in_process_restart(void)
{
	if (s_in_process_restart == 0) {
		return 0;
	}
	s_in_process_restart = 0;
	return 1;
}

#endif /* __ANDROID__ */
