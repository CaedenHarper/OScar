.section .rodata
.global user_program_kernel_access_start
.global user_program_kernel_access_end
user_program_kernel_access_start:
.incbin "user/kernel_access.elf"
user_program_kernel_access_end:
