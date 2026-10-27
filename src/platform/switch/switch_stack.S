.section .text.Switch_RunOnStack,"ax",%progbits
.align 2
.global Switch_RunOnStack
.type Switch_RunOnStack, %function

// void Switch_RunOnStack(void *stackTop, void (*entry)(void *), void *arg)
// x0 = aligned alternate stack top, x1 = entry point, x2 = argument.
// Save original SP and LR on the alternate stack. The callback follows AAPCS64,
// preserving x19-x29; restore the original SP before returning to the caller.
Switch_RunOnStack:
    mov x9, sp
    mov sp, x0
    stp x9, x30, [sp, #-16]!
    mov x0, x2
    blr x1
    ldp x9, x30, [sp], #16
    mov sp, x9
    ret

.size Switch_RunOnStack, .-Switch_RunOnStack
