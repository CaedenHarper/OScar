.section .rodata
.global user_program_init_start
.global user_program_init_end
user_program_init_start:
.incbin "build/user/init.elf"
user_program_init_end:
