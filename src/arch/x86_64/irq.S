.section .text

.extern timer_irq_handler
.extern scheduler_interrupt_exit

.global irq0
.type irq0, @function
irq0:
    cld
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    call timer_irq_handler
    call scheduler_interrupt_exit
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    iretq
.size irq0, .-irq0

.section .rodata
.global irq_stub_table
.type irq_stub_table, @object
irq_stub_table:
    .quad irq0
.size irq_stub_table, .-irq_stub_table
