.section .text
.global _start
.type _start, @function
_start:
    movl $4, %eax
    int $0x80
    testq %rax, %rax
    jz .Lidentity_fail
    movl $5, %eax
    int $0x80
    testq %rax, %rax
    jz .Lidentity_fail

    movl $0, %eax
    movl $1, %edi
    leaq message(%rip), %rsi
    movl $(message_end - message), %edx
    int $0x80
    movl $1, %eax
    int $0x80
.Lhalt:
    jmp .Lhalt

.Lidentity_fail:
    movl $0, %eax
    movl $1, %edi
    leaq identity_failure(%rip), %rsi
    movl $(identity_failure_end - identity_failure), %edx
    int $0x80
    movl $1, %eax
    int $0x80
    jmp .Lhalt
.size _start, .-_start

.section .rodata
message:
    .ascii "Real ELF program executed.\n"
message_end:
identity_failure:
    .ascii "Identity syscall test failed.\n"
identity_failure_end:
