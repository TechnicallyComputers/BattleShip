/*
 * Offline-build stubs for debug_tools GBI/Acmd trace APIs.
 * SSB64_NETMENU=ON links debug_tools/ instead; offline must not ship trace tooling.
 */
#include "../../debug_tools/gbi_trace/gbi_trace.h"
#include "../../debug_tools/acmd_trace/acmd_trace.h"

void gbi_trace_init(void) {}
void gbi_trace_shutdown(void) {}
void gbi_trace_set_enabled(int enabled) { (void)enabled; }
int gbi_trace_is_enabled(void) { return 0; }
void gbi_trace_begin_frame(void) {}
void gbi_trace_end_frame(void) {}
void gbi_trace_log_cmd(unsigned long long w0, unsigned long long w1, int depth)
{
    (void)w0;
    (void)w1;
    (void)depth;
}
void gbi_trace_set_max_frames(int max_frames) { (void)max_frames; }

void acmd_trace_init(void) {}
void acmd_trace_shutdown(void) {}
void acmd_trace_set_enabled(int enabled) { (void)enabled; }
int acmd_trace_is_enabled(void) { return 0; }
void acmd_trace_begin_task(void) {}
void acmd_trace_end_task(void) {}
void acmd_trace_log_cmd(uint32_t w0, uint32_t w1)
{
    (void)w0;
    (void)w1;
}
void acmd_trace_log_buffer(const void *cmd_list, int cmd_count)
{
    (void)cmd_list;
    (void)cmd_count;
}
void acmd_trace_set_max_tasks(int max_tasks) { (void)max_tasks; }
