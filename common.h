/*
Using macros for now instead of functions because it simplifies the linker script.

But the downsides are severe:

-   no symbols to help debugging

-   impossible to step over method calls: you have to step into everything

-   larger output, supposing I can get linker gc for unused functions working,
    see --gc-section, which is for now uncertain.
    If I can get this working, I'll definitely move to function calls.

    The problem is that if I don't, every image will need a stage 2 loader.
    That is not too serious though, it could be added to BEGIN.

## Conventions

Every "function-like macro" should maintain register state
(flags currently not maintained).

%sp cannot be used to pass most arguments.

We don't care about setting %bp.
*/

/*
I really want this for the local labels.

The major downside is that every register passed as argument requires `<>`:
http://stackoverflow.com/questions/19776992/gas-altmacro-macro-with-a-percent-sign-in-a-default-parameter-fails-with-oper/
*/
.altmacro

/* Helpers */

/* Push regiesters ax, bx, cx and dx. Lightweight `pusha`. */
.macro PUSH_ADX
    push %ax
    push %bx
    push %cx
    push %dx
.endm

/*
Pop registers dx, cx, bx, ax. Inverse order from PUSH_ADX,
so this cancels that one.
*/
.macro POP_DAX
    pop %dx
    pop %cx
    pop %bx
    pop %ax
.endm

.macro PUSH_EADX
    push %eax
    push %ebx
    push %ecx
    push %edx
.endm

.macro POP_EDAX
    pop %edx
    pop %ecx
    pop %ebx
    pop %eax
.endm

/*
Convert the low nibble of a r8 reg to ASCII of 8-bit in-place.
reg: r8 to be converted
Output: stored in reg itself. Letters are uppercase.
*/
.macro HEX_NIBBLE reg
    LOCAL letter, end
    cmp $10, \reg
    jae letter
    add $'0, \reg
    jmp end
letter:
    /* 0x37 == 'A' - 10 */
    add $0x37, \reg
end:
.endm

/*
Convert a byte to hex ASCII value.
c: r/m8 byte to be converted
Output: two ASCII characters, is stored in `ah:al`
http://stackoverflow.com/questions/3853730/printing-hexadecimal-digits-with-assembly
*/
.macro HEX c
    mov \c, %al
    mov \c, %ah
    shr $4, %al
    HEX_NIBBLE <%al>
    and $0x0F, %ah
    HEX_NIBBLE <%ah>
.endm

/* Structural. */

/*
Setup a sane initial state.

Should be the first thing in every file.
*/
.macro BEGIN
    .code16
    cli
    /* Set %cs to 0. TODO Is that really needed? */
    ljmp $0, $1f
    1:
    xor %ax, %ax
    /* We must zero %ds for any data access. */
    mov %ax, %ds
    /* TODO is it really need to clear all those segment registers, e.g. for BIOS calls? */
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    /*
    TODO What to move into BP and SP?
    http://stackoverflow.com/questions/10598802/which-value-should-be-used-for-sp-for-booting-process
    */
    mov 0x0000, %bp
    /* Automatically disables interrupts until the end of the next instruction. */
    mov %ax, %ss
    /* We should set SP because BIOS calls may depend on that. TODO confirm. */
    mov %bp, %sp
.endm

/*
Load stage2 from disk to memory, and jump to it.

To be used when the program does not fit in the 512 bytes.

Sample usage:

    STAGE2
    Stage 2 code here.
*/
.macro STAGE2
    mov $2, %ah
    /*
    TODO get working with linker script.
    Above my paygrade for now, so I just load a bunch of sectors instead.
    */
    /* mov __stage2_size, %al;\ */
    mov $9, %al
    mov $0x80, %dl
    mov $0, %ch
    mov $0, %dh
    mov $2, %cl
    mov $1f, %bx
    int $0x13
    jmp 1f
    .section .stage2
    1:
.endm

/*
Enter protected mode.

Use the simplest GDT possible.
*/
.macro PROTECTED_MODE
    /* Must come before they are used. */
    .equ CODE_SEG, 8
    .equ DATA_SEG, gdt_data - gdt_start

    /* Tell the processor where our Global Descriptor Table is in memory. */
    lgdt gdt_descriptor

    /*
    Set PE (Protection Enable) bit in CR0 (Control Register 0),
    effectively entering protected mode.
    */
    mov %cr0, %eax
    orl $0x1, %eax
    mov %eax, %cr0

    ljmp $CODE_SEG, $protected_mode
/*
Our GDT contains:
- a null entry to fill the unusable entry 0:
  http://stackoverflow.com/questions/33198282/why-have-the-first-segment-descriptor-of-the-global-descriptor-table-contain-onl
- a code and data. Both are necessary, because:
  - it is impossible to write to the code segment
  - it is impossible execute the data segment
  Both start at 0 and span the entire memory,
  allowing us to access anything without problems.
A real OS might have 2 extra segments: user data and code.
This is better than modifying the privilege bit of the GDT
as we'd have to reload it several times, losing cache.
*/
gdt_start:
gdt_null:
    .long 0x0
    .long 0x0
gdt_code:
    .word 0xffff
    .word 0x0
    .byte 0x0
    .byte 0b10011010
    .byte 0b11001111
    .byte 0x0
gdt_data:
    .word 0xffff
    .word 0x0
    .byte 0x0
    .byte 0b10010010
    .byte 0b11001111
    .byte 0x0
gdt_end:
gdt_descriptor:
    .word gdt_end - gdt_start
    .long gdt_start
vga_current_line:
    .long 0
.code32
protected_mode:
    /*
    Setup the other segments.
    Those movs are mandatory because they update the descriptor cache:
    http://wiki.osdev.org/Descriptor_Cache
    */
    mov $DATA_SEG, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    /*
    TODO detect the last memory address available properly.
    It depends on how much RAM we have.
    */
    mov $0X7000, %ebp
    mov %ebp, %esp
.endm

/* BIOS */

.macro CURSOR_POSITION x=$0, y=$0
    PUSH_ADX
    mov $0x02, %ah
    mov $0x00, %bh
    mov \x, %dh
    mov \y, %dl
    int $0x10
    POP_DAX
.endm

/* Clear the screen, move to position 0, 0. */
.macro CLEAR
    PUSH_ADX
    mov $0x0600, %ax
    mov $0x7, %bh
    mov $0x0, %cx
    mov $0x184f, %dx
    int $0x10
    CURSOR_POSITION
    POP_DAX
.endm

/*
Print a 8 bit ASCII value at current cursor position.

- c  r/m/imm8  ASCII value to be printed.

Usage:

    PUTC $'a

prints `'a'` to the screen.
*/
.macro PUTC c=$0x20
    push %ax
    mov \c, %al
    mov $0x0E, %ah
    int $0x10
    pop %ax
.endm

/*
Print a byte as two hexadecimal digits.

- reg: 1 byte register.
*/
.macro PRINT_HEX reg=<%al>
    push %ax
    HEX <\reg>
    PUTC <%al>
    PUTC <%ah>
    pop %ax
.endm

/*
Print a 16-bit number

- in: r/m/imm16
*/
.macro PRINT_WORD_HEX in=<%ax>
    push %ax
    mov \in, %ax
    PRINT_HEX <%ah>
    PRINT_HEX <%al>
    pop %ax
.endm

.macro PRINT_NEWLINE
    PUTC $'\n
    PUTC $'\r
.endm

/*
Print a null terminated string.

Use as:

        PRINT $s
        hlt
    s:
        .asciz "string"
*/
.macro PRINT s
    LOCAL end, loop
    mov s, %si
    mov $0x0e, %ah
    cld
loop:
    lodsb
    or %al, %al
    jz end
    int $0x10
    jmp loop
end:
.endm

/*
Dump memory.

- s: starting address
- n: number of bytes to dump
*/
.macro PRINT_BYTES s, n=$16
    LOCAL end, loop, no_newline
    PUSH_ADX
    push %di
    mov s, %si
    mov \n, %cx
    mov $0, %di
    cld
loop:
    cmp $0, %cx
    je end
    dec %cx
    lodsb
    PRINT_HEX
    PUTC
    /* Print a newline for every 8 bytes. */
    mov $0, %dx
    mov %di, %ax
    mov $8, %bx
    div %bx
    cmp $7, %dx
    jne no_newline
    PRINT_NEWLINE
no_newline:
    inc %di
    jmp loop
end:
    pop %di
    POP_DAX
.endm

/* VGA */

/*
Print a NULL terminated string to position 0 in VGA.

s: 32-bit register or memory containing the address of the string to print.

Clobbers: none.

Uses and updates vga_current_line to decide the current line.
Loops around the to the top.
*/
.macro VGA_PRINT_STRING s
    LOCAL loop, end
    PUSH_EADX
    mov \s, %ecx
    mov vga_current_line, %eax
    mov $0, %edx
    /* Number of horizontal lines. */
    mov $25, %ebx
    div %ebx
    mov %edx, %eax
    /* 160 == 80 * 2 == line width * bytes per character on screen */
    mov $160, %edx
    mul %edx
    /* 0xb8000 == magic video memory address which shows on the screen. */
    lea 0xb8000(%eax), %edx
    /* White on black. */
    mov $0x0f, %ah
loop:
    mov (%ecx), %al
    cmp $0, %al
    je end
    mov %ax, (%edx)
    add $1, %ecx
    add $2, %edx
    jmp loop
end:
    incl vga_current_line
    POP_EDAX
.endm

/*
Print a 32-bit register in hex.

Sample usage:

    mov $12345678, %eax
    VGA_PRINT_REG <%eax>

Expected output on screen:

    12345678
*/
.macro VGA_PRINT_REG reg=<%eax>
    LOCAL loop
    PUSH_EADX
    /* Null terminator. */
    mov \reg, %ecx

    /* Write ASCII representation to memory. */
    push $0
    mov $2, %ebx
loop:
    HEX <%cl>
    mov %ax, %dx
    shl $16, %edx
    HEX <%ch>
    mov %ax, %dx
    push %edx
    shr $16, %ecx
    dec %ebx
    cmp $0, %ebx
    jne loop

    /* Print it. */
    mov %esp, %edx
    VGA_PRINT_STRING <%edx>

    /* Restore the stack. We have pushed 3 * 4 bytes. */
    add $12, %esp
    POP_EDAX
.endm

.macro IDT_START
    idt_start:
.endm

.macro IDT_END
idt_end:
/* Exact same structure as gdt_descriptor. */
idt_descriptor:
    .word idt_end - idt_start
    .long idt_start
.endm

.macro IDT_ENTRY
    /* Low handler address.
    It is impossible to write:
    .word (handler & 0x0000FFFF)
    as we would like:
    http://stackoverflow.com/questions/18495765/invalid-operands-for-binary-and
    So this must be done at runtime.
    */
    .word 0
    /* Segment selector: byte offset into the GDT. */
    .word CODE_SEG
    /* Reserved 0. */
    .byte 0
    /*
    Flags. Format:
    - 1 bit: present. If 0 and this happens, triple fault.
    - 2 bits: ring level we will be called from.
    - 5 bits: fixed to 0xE.
    */
    .byte 0x8E
    /* High word of base. */
    .word 0
.endm

/*
- index: r/m/imm32 Index of the entry to setup.
- handler: r/m/imm32 Address of the handler function.
*/
.macro IDT_SETUP_ENTRY index, handler
    mov \index, %eax
    mov \handler, %ebx
    mov %bx, idt_start(%eax, 8)
    shr $16, %ebx
    mov $6, %ecx
    mov %bx, idt_start(%ecx, %eax, 8)
.endm
