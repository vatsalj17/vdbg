.global _start

// standard executable section
.section .text
_start:
    mov $60, %rax
    xor %rdi, %rdi
    syscall

// extremely long section name
.section .this_is_a_very_very_long_section_name_that_exceeds_standard_lengths_and_tests_string_table_parsing_robustness, "a", @progbits
.asciz "Long name test"

// section with massive alignment
.section .massive_alignment, "a", @progbits
.balign 0x10000
.quad 0xdeadbeef

// unallocated section but executable (weird flags)
.section .weird_flags_x, "x", @progbits
    nop

// writable and executable section
.section .weird_flags_awx, "awx", @progbits
    nop

// empty section
.section .empty_section, "a", @progbits

// nobits section with large size
.section .large_bss, "aw", @nobits
.space 0x10000000

// custom note section
.section .note.custom, "a", @note
.long 4, 4, 1
.asciz "FOO"
.long 0x11223344

// section with overlapping name prefixes in shstrtab
.section .test_prefix, "a", @progbits
.byte 1
.section .test_prefix.foo, "a", @progbits
.byte 2
.section .test_prefix.foo.bar, "a", @progbits
.byte 3

// section with @init_array type
.section .my_init, "aw", @init_array
.quad _start

// section with tls flag
.section .my_tls, "awT", @progbits
.long 0x42
