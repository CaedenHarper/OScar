.section .text

.global gdt_load
.type gdt_load, @function
gdt_load:
    lgdt (%rdi)
    pushq $0x08
    leaq .Lgdt_reload(%rip), %rax
    pushq %rax
    lretq
.Lgdt_reload:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs
    ret
.size gdt_load, .-gdt_load
