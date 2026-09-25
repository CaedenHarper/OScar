.section .text
.global _start
.type _start, @function
_start:
    movl $0, %eax
    leaq message(%rip), %rdi
    movl $(message_end - message), %esi
    int $0x80
    movabsq $0xffff900000000000, %rax
    movq (%rax), %rbx
.Lhalt:
    jmp .Lhalt
.size _start, .-_start

.section .rodata
message:
    .ascii "Starting kernel-space access test.\n"
message_end:
