.section .text

.equ CONTEXT_RSP, 0
.equ CONTEXT_RIP, 8
.equ CONTEXT_RFLAGS, 16
.equ CONTEXT_RBX, 24
.equ CONTEXT_RBP, 32
.equ CONTEXT_R12, 40
.equ CONTEXT_R13, 48
.equ CONTEXT_R14, 56
.equ CONTEXT_R15, 64

.global switch_context
.type switch_context, @function
switch_context:
    # Save the outgoing kernel context. The saved RIP points after this switch
    # so resuming it returns from switch_context to its original caller.
    movq %rsp, CONTEXT_RSP(%rdi)
    leaq .Lcontext_resume(%rip), %rax
    movq %rax, CONTEXT_RIP(%rdi)
    pushfq
    popq CONTEXT_RFLAGS(%rdi)
    movq %rbx, CONTEXT_RBX(%rdi)
    movq %rbp, CONTEXT_RBP(%rdi)
    movq %r12, CONTEXT_R12(%rdi)
    movq %r13, CONTEXT_R13(%rdi)
    movq %r14, CONTEXT_R14(%rdi)
    movq %r15, CONTEXT_R15(%rdi)

    # Restore the incoming context without changing its address space.
    movq CONTEXT_RBX(%rsi), %rbx
    movq CONTEXT_RBP(%rsi), %rbp
    movq CONTEXT_R12(%rsi), %r12
    movq CONTEXT_R13(%rsi), %r13
    movq CONTEXT_R14(%rsi), %r14
    movq CONTEXT_R15(%rsi), %r15
    pushq CONTEXT_RFLAGS(%rsi)
    popfq
    movq CONTEXT_RSP(%rsi), %rsp
    jmp *CONTEXT_RIP(%rsi)

.Lcontext_resume:
    ret
.size switch_context, .-switch_context

.global context_start
.type context_start, @function
context_start:
    # context::initialize places the entry function and argument in r12/r13.
    movq %r13, %rdi
    call *%r12
    ud2
.size context_start, .-context_start
