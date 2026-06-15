# funchook 1.1.3 FetchContent PATCH_COMMAND. Applies three frozen-source
# fixes (funchook is archived at 1.1.3, so these never bitrot):
#
#  1. Bump funchook's hardcoded capstone download (old aquynh/capstone
#     @ 4.0.2) to maintained capstone-engine/capstone @ 5.0.1. 4.0.2
#     hard-sets `cmake_policy(CMP0048 OLD)`, which CMake >=4 refuses
#     outright (a removed policy cannot be forced OLD, and no
#     CMAKE_POLICY_VERSION_MINIMUM bump overrides it). 5.0.1 raises its
#     cmake_minimum_required to 3.15, keeps capstone's stable C disasm
#     API, and still exposes the `CAPSTONE_ARM64_SUPPORT` option name
#     funchook 1.1.3 toggles (renamed to AARCH64 only in capstone 6).
#  2. Re-point funchook's `capstone-static` dep ref (4.0.2 target name)
#     to `capstone` (5.0.1 unified target name).
#  3. Apple Silicon code-patching fix in funchook_unix.c. funchook
#     1.1.3 only ever calls mprotect(); on macOS arm64 the kernel
#     refuses to make a code-signed __TEXT page writable via mprotect
#     under any entitlement, so funchook_install fails ("Failed to
#     unprotect memory ... Permission denied"). The sanctioned route is
#     mach_vm_protect with VM_PROT_COPY (private writable COW copy of
#     the page), then restore R+X and flush i-cache — the same recipe
#     Dobby/frida use. Injected behind `#if __APPLE__ && __aarch64__`
#     so every other platform keeps funchook's stock mprotect path.
#
# Invoked as funchook's FetchContent PATCH_COMMAND so the rewrites land
# before funchook's add_subdirectory configures/builds it. Idempotent.

set(_tmpl "${CAPSTONE_TMPL}")

if(NOT EXISTS "${_tmpl}")
    message(FATAL_ERROR "funchook capstone template not found: ${_tmpl}")
endif()

file(READ "${_tmpl}" _contents)
string(REPLACE "https://github.com/aquynh/capstone.git"
                "https://github.com/capstone-engine/capstone.git"
                _contents "${_contents}")
string(REPLACE "GIT_TAG           4.0.2"
                "GIT_TAG           5.0.1"
                _contents "${_contents}")
file(WRITE "${_tmpl}" "${_contents}")

# capstone 4.0.2 exposed a `capstone-static` target; 5.0.1 unified to a
# single `capstone` target. Re-point funchook's dependency reference.
set(_cml "${FUNCHOOK_CML}")
if(NOT EXISTS "${_cml}")
    message(FATAL_ERROR "funchook CMakeLists not found: ${_cml}")
endif()
file(READ "${_cml}" _cml_contents)
string(REPLACE "FUNCHOOK_DEPS capstone-static"
                "FUNCHOOK_DEPS capstone"
                _cml_contents "${_cml_contents}")
file(WRITE "${_cml}" "${_cml_contents}")

# --- (3) Apple Silicon: mach_vm_protect/VM_PROT_COPY code patching ----------
set(_unix "${FUNCHOOK_UNIX}")
if(NOT EXISTS "${_unix}")
    message(FATAL_ERROR "funchook_unix.c not found: ${_unix}")
endif()
file(READ "${_unix}" _unix_contents)

set(_anchor_inc "#include <dlfcn.h>")
set(_helper [==[#include <dlfcn.h>
#if defined(__APPLE__) && defined(__aarch64__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <libkern/OSCacheControl.h>
/* Apple Silicon enforces W^X on code-signed __TEXT: mprotect() can never
 * make those pages writable, so funchook's stock path fails. mach_vm_protect
 * with VM_PROT_COPY forces a private writable COW copy of the page so the
 * prologue can be rewritten in place; restore R+X and flush i-cache after. */
static int funchook_mac_arm64_set_rw(void *addr, size_t size) {
    kern_return_t kr = mach_vm_protect(mach_task_self(),
                                       (mach_vm_address_t)(uintptr_t)addr,
                                       (mach_vm_size_t)size, FALSE,
                                       VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    return (kr == KERN_SUCCESS) ? 0 : -1;
}
static int funchook_mac_arm64_set_rx(void *addr, size_t size) {
    kern_return_t kr = mach_vm_protect(mach_task_self(),
                                       (mach_vm_address_t)(uintptr_t)addr,
                                       (mach_vm_size_t)size, FALSE,
                                       VM_PROT_READ | VM_PROT_EXECUTE);
    if (kr != KERN_SUCCESS) return -1;
    sys_icache_invalidate(addr, size);
    return 0;
}
#endif]==])

set(_anchor_begin "    rv = mprotect(mstate->addr, mstate->size, prot)\;")
set(_repl_begin [==[#if defined(__APPLE__) && defined(__aarch64__)
    if (funchook_mac_arm64_set_rw(mstate->addr, mstate->size) == 0) {
        funchook_log(funchook, "  unprotect memory %p (size=%"PRIuPTR", prot=read,write,copy) <- %p (size=%"PRIuPTR")\n",
                     mstate->addr, mstate->size, start, len);
        return 0;
    }
#endif
    rv = mprotect(mstate->addr, mstate->size, prot);]==])

set(_anchor_end "    int rv = mprotect(mstate->addr, mstate->size, PROT_READ | PROT_EXEC)\;")
set(_repl_end [==[#if defined(__APPLE__) && defined(__aarch64__)
    if (funchook_mac_arm64_set_rx(mstate->addr, mstate->size) == 0) {
        funchook_log(funchook, "  protect memory %p (size=%"PRIuPTR", prot=read,exec)\n",
                     mstate->addr, mstate->size);
        return 0;
    }
#endif
    int rv = mprotect(mstate->addr, mstate->size, PROT_READ | PROT_EXEC);]==])

if(NOT _unix_contents MATCHES "funchook_mac_arm64_set_rw")
    string(REPLACE "${_anchor_inc}" "${_helper}"      _unix_contents "${_unix_contents}")
    string(REPLACE "${_anchor_begin}" "${_repl_begin}" _unix_contents "${_unix_contents}")
    string(REPLACE "${_anchor_end}" "${_repl_end}"     _unix_contents "${_unix_contents}")
    file(WRITE "${_unix}" "${_unix_contents}")
    message(STATUS "funchook: patched funchook_unix.c (Apple Silicon VM_PROT_COPY code patching)")
endif()

message(STATUS "funchook: patched capstone download -> capstone-engine/capstone 5.0.1 (target: capstone)")
