.section .rodata
.global user_program_prime_start
.global user_program_prime_end
user_program_prime_start:
.incbin "user/prime.elf"
user_program_prime_end:
