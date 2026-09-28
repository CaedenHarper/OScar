.section .text
.global _start
.type _start, @function
_start:
    movl $0, %eax
    movl $1, %edi
    leaq message(%rip), %rsi
    movl $(message_end - message), %edx
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
