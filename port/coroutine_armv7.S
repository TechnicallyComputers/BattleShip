// coroutine_armv7.S — context-switch primitive for the 32-bit ARM (AAPCS)
// coroutine backend (Android armeabi-v7a, and any other ARMv7-A platform
// that opts in). Mirror of coroutine_aarch64.S for the AArch32 ABI.
//
// The AAPCS (32-bit ARM EABI) calling convention defines this callee-saved
// set as the only registers a function must preserve across a call:
//
//   GP : r4..r11, sp(r13), lr(r14)
//   VFP: d8..d15 (s16..s31)   [armeabi-v7a mandates VFPv3-D16]
//
// Everything else (r0..r3, r12/ip, CPSR flags, d0..d7, d16..d31) is
// caller-saved — the compiler has already spilled anything it cared about
// at the call site of port_coroutine_swap, so we only save/restore the
// callee-saved set.
//
// Context layout (104 bytes, 8-byte aligned). Offsets are mirrored by
// static_asserts on the C `CoroCtx` struct in coroutine_android.cpp:
//
//      0   r4    |  20  r9    |  40  d8    |  72  d12
//      4   r5    |  24  r10   |  48  d9    |  80  d13
//      8   r6    |  28  r11   |  56  d10   |  88  d14
//     12   r7    |  32  sp    |  64  d11   |  96  d15
//     16   r8    |  36  lr    |
//
// The code is assembled in ARM state (.arm). Calls to/from Thumb C code
// (Android's default) interwork through the linker: bl to a Thumb symbol is
// rewritten to blx, and the saved lr carries the Thumb bit so bx lr returns
// to the correct instruction set.

#if defined(__arm__)

    .syntax unified
    .arm
    .fpu    vfpv3-d16
    .text
    .balign 4

// void port_coroutine_swap(CoroCtx *from, CoroCtx *to);
//   r0 = from, r1 = to.
//
// Saves callee-saved state into *from, then restores *to and returns (via
// the lr that *to had stashed). The "from" coroutine effectively pauses at
// the instruction after its caller's bl to this function; when something
// later swaps back into "from", that bx re-enters there.
    .globl  port_coroutine_swap
#if defined(__ELF__)
    .type   port_coroutine_swap, %function
#endif
port_coroutine_swap:
    // Save "from" (r0). sp/lr stored via str (not stm) to avoid the
    // ARMv7-deprecated "store of r13/r15 in a register list".
    stmia   r0, {r4-r11}            // r4..r11 at offsets 0..28
    str     sp, [r0, #32]
    str     lr, [r0, #36]
    add     r2, r0, #40
    vstmia  r2, {d8-d15}           // d8..d15 at offsets 40..96

    // Restore "to" (r1).
    add     r2, r1, #40
    vldmia  r2, {d8-d15}
    ldmia   r1, {r4-r11}
    ldr     sp, [r1, #32]
    ldr     lr, [r1, #36]
    bx      lr
#if defined(__ELF__)
    .size   port_coroutine_swap, .-port_coroutine_swap
#endif


// void port_coroutine_trampoline_armv7(void);
//
// First entry point for a freshly-created coroutine. The C side arranges
// for r4 to hold the PortCoroutine* on first entry (r4 is callee-saved, so
// it survives port_coroutine_swap). We move it into r0 as the first argument
// to the C trampoline, which invokes the user entry function and performs a
// permanent yield. The udf traps if the C trampoline ever returns, which
// would indicate a bug in the swap logic.
    .globl  port_coroutine_trampoline_armv7
#if defined(__ELF__)
    .type   port_coroutine_trampoline_armv7, %function
#endif
port_coroutine_trampoline_armv7:
    // Clear frame/link regs so a debugger unwinding via fp (r11 in ARM,
    // r7 in Thumb) stops cleanly at the trampoline.
    mov     r11, #0
    mov     r7,  #0
    mov     lr,  #0
    mov     r0,  r4
    bl      port_coroutine_trampoline_c
    udf     #0
#if defined(__ELF__)
    .size   port_coroutine_trampoline_armv7, .-port_coroutine_trampoline_armv7
#endif

#endif /* __arm__ */


// GNU/Linux ELF non-executable stack note. Android's linker rejects shared
// objects without it (W^X enforcement).
#if defined(__linux__) && defined(__ELF__)
    .section .note.GNU-stack,"",%progbits
#endif
