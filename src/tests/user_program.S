.section .rodata
.global user_program_start
.global user_program_end
user_program_start:
.incbin "user/basic.elf"
user_program_end:
