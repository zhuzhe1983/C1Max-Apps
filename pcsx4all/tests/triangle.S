/* Self-contained PS1 GPU smoke program, authored for this port. MIT license. */
.set noreorder
.set noat
.text
.globl _start
_start:
    lui $t0,0x1f80
    sw $zero,0x1814($t0)
    lui $t1,0x0800
    ori $t1,$t1,1
    sw $t1,0x1814($t0)
    lui $t1,0x0500
    sw $t1,0x1814($t0)
    li $t1,0x06c60260
    sw $t1,0x1814($t0)
    li $t1,0x07042018
    sw $t1,0x1814($t0)
    li $t1,0x03000000
    sw $t1,0x1814($t0)
    li $t1,0xe1000400
    sw $t1,0x1810($t0)
    lui $t1,0xe300
    sw $t1,0x1810($t0)
    li $t1,0xe403bd3f
    sw $t1,0x1810($t0)
    lui $t1,0xe500
    sw $t1,0x1810($t0)
    move $s0,$zero
loop:
    li $t1,0x02302010
    sw $t1,0x1810($t0)
    sw $zero,0x1810($t0)
    li $t1,0x00f00140
    sw $t1,0x1810($t0)
    li $t1,0x200080ff
    sw $t1,0x1810($t0)
    andi $t2,$s0,63
    li $t1,0x00300078
    addu $t1,$t1,$t2
    sw $t1,0x1810($t0)
    li $t1,0x00c00038
    sw $t1,0x1810($t0)
    li $t1,0x00c00100
    sw $t1,0x1810($t0)
    addiu $s0,$s0,1
    li $t2,50000
delay:
    addiu $t2,$t2,-1
    bnez $t2,delay
    nop
    b loop
    nop
