.section .rodata
.global user_program_second_start
.global user_program_second_end
user_program_second_start:
.incbin "build/user/second.elf"
user_program_second_end:
