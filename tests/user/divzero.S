.section .text
.global _start
.type _start, @function
_start:
    movl $0, %eax
    movl $1, %edi
    leaq message(%rip), %rsi
    movl $(message_end - message), %edx
    int $0x80
    movl $1, %eax
    xorl %edx, %edx
    divl %edx
.Lhalt:
    jmp .Lhalt
.size _start, .-_start

.section .rodata
message:
    .ascii "Starting divide-by-zero test.\n"
message_end:
