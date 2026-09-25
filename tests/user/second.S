.section .text
.global _start
.type _start, @function
_start:
    movl $0, %eax
    leaq message(%rip), %rdi
    movl $(message_end - message), %esi
    int $0x80
    movl $1, %eax
    int $0x80
.Lhalt:
    jmp .Lhalt
.size _start, .-_start

.section .rodata
message:
    .ascii "Second ELF program executed.\n"
message_end:
