.section .rodata
.global user_program_terminal_start
.global user_program_terminal_end
user_program_terminal_start:
    .incbin "build/user/terminal.elf"
user_program_terminal_end:
