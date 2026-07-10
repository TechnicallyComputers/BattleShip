/*
 * Offline-build stubs for debug_tools Acmd trace APIs.
 * GBI trace (debug_tools/gbi_trace/) links in all PORT builds for SSB64_GBI_TRACE A/B.
 * Acmd trace remains netmenu-only (SSB64_NETMENU=ON links debug_tools/acmd_trace/).
 */
#include "../../debug_tools/acmd_trace/acmd_trace.h"

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
