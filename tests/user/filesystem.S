.section .text
.global _start
.type _start, @function
_start:
    movl $6, %eax
    leaq path(%rip), %rdi
    movl $3, %esi
    int $0x80
    testq %rax, %rax
    js .Lfail
    movq %rax, %r12

    movl $0, %eax
    movq %r12, %rdi
    leaq write_data(%rip), %rsi
    movl $(write_data_end - write_data), %edx
    int $0x80
    testq %rax, %rax
    js .Lfail_close

    movl $9, %eax
    movq %r12, %rdi
    xorl %esi, %esi
    int $0x80
    testq %rax, %rax
    js .Lfail_close

    movl $7, %eax
    movq %r12, %rdi
    leaq buffer(%rip), %rsi
    movl $(buffer_end - buffer), %edx
    int $0x80
    testq %rax, %rax
    js .Lfail_close
    movq %rax, %r13

    movl $0, %eax
    movl $1, %edi
    leaq buffer(%rip), %rsi
    movq %r13, %rdx
    int $0x80

    movl $9, %eax
    movq %r12, %rdi
    xorl %esi, %esi
    int $0x80
    testq %rax, %rax
    js .Lfail_close

    movl $8, %eax
    movq %r12, %rdi
    int $0x80
    testq %rax, %rax
    js .Lfail

    movl $12, %eax
    leaq created_path(%rip), %rdi
    int $0x80
    testq %rax, %rax
    js .Lfail

    movl $14, %eax
    leaq created_path(%rip), %rdi
    int $0x80
    testq %rax, %rax
    js .Lfail

    movl $13, %eax
    leaq created_directory(%rip), %rdi
    int $0x80
    testq %rax, %rax
    js .Lfail

    movl $15, %eax
    leaq created_directory(%rip), %rdi
    int $0x80
    testq %rax, %rax
    js .Lfail

    movl $1, %eax
    int $0x80
.Lhalt:
    jmp .Lhalt

.Lfail_close:
    movl $8, %eax
    movq %r12, %rdi
    int $0x80
.Lfail:
    movl $0, %eax
    movl $1, %edi
    leaq failure(%rip), %rsi
    movl $(failure_end - failure), %edx
    int $0x80
    movl $1, %eax
    int $0x80
    jmp .Lhalt
.size _start, .-_start

.section .rodata
path:
    .asciz "/writable.txt"
created_path:
    .asciz "/syscall-created.txt"
created_directory:
    .asciz "/syscall-directory"
write_data:
    .ascii "User syscall write test.\n"
write_data_end:
failure:
    .ascii "Filesystem syscall test failed.\n"
failure_end:

.section .bss
.balign 16
buffer:
    .space 64
buffer_end:
