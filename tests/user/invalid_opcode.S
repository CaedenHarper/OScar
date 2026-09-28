.section .text
.global _start
.type _start, @function
_start:
    movl $0, %eax
    movl $1, %edi
    leaq message(%rip), %rsi
    movl $(message_end - message), %edx
    int $0x80
    ud2
.Lhalt:
    jmp .Lhalt
.size _start, .-_start

.section .rodata
message:
    .ascii "Starting invalid-opcode test.\n"
message_end:
