.section .rodata
.global user_program_invalid_opcode_start
.global user_program_invalid_opcode_end
user_program_invalid_opcode_start:
.incbin "user/invalid_opcode.elf"
user_program_invalid_opcode_end:
