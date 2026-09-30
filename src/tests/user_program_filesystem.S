.section .rodata
.global user_program_filesystem_start
.global user_program_filesystem_end
user_program_filesystem_start:
.incbin "user/filesystem.elf"
user_program_filesystem_end:
