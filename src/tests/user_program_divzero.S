.section .rodata
.global user_program_divzero_start
.global user_program_divzero_end
user_program_divzero_start:
.incbin "build/user/divzero.elf"
user_program_divzero_end:
