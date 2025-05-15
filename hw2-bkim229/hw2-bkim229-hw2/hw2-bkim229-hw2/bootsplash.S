.code16
.globl start
start:
#movb $0x0e, %ah
#movb hello, %al  # what is moved into %al ?
#movb $0,    %bh
#movb $7,    %bl
#int $0x10

#save %dl somewhere
mov %dl, savedl

#get cursor position
movb $0x03, %ah
movb $0,    %bh
int $0x10

movb $0x13, %ah
movb $1,    %al
movb $0,    %bh
movb $7,    %bl
movw $45,   %cx #dh and dl set in 0x03 instruction
movw $olin, %bp
int $0x10

movb $0, %ah
int $0x16

# video mode 13h
movb $0,  %ah
movb $0x13, %al
int $0x10

#movw $0xfffe, %ax
#movw %ax, %bx

#movw $0xA000, %ax  # How many 0s should it have?
#movw %ax, %ds  # In i8086 you cannot move imm directly to a *s register. (movw %0xA000, %ds)
#movb $0xf, %al # A white pixel
#movw $0, %di   # The first pixel at the top-left corner of the screen
#movb %al, %ds:(%di) # After this instruction you can see the tiny white dot there. (maximize window to make it more visible)

# Writing graphics to the screen




# Copy the data from 0x7E00 to video memory at 0xA000

#xorw %ax, %ax
#movw %ax, %ds
#cld
# ES(Segment):BX(Offset)
# 0xA000:0x0
#movb $0x2, %ah
#movb $125, %al
#movb $0, %ch
#movb $2, %cl
#movb $0, %dh
#movw $0xA000, %ax
#movw %ax, %es
#xorw %bx, %bx
#int $0x13  

# Reading from disk using BIOS services

#segment:offset, DS:SI
movw $structt, %si # unknown?
movw $0x0000, %ax    
movw %ax, %ds  

#movw $structt, %si # unknown?
#movw $structt@S, %ax    
#movw %ax, %ds  


#movw $0x0000, %si # unknown?
#movw $structt, %ax    
#movw %ax, %ds  

movb $0x42, %ah # check
movb savedl, %dl    # used savedl
int $0x13

stop:
jmp stop

structt:
    .byte 0x10
    .byte 0x00
    .word 0x007d # 0x007d = 125, 0x003f = 63, 0x1 = 1, number of sectors to be read
    .word 0x0000 # offset
    .word 0xA000 #transfer buffer or 0xA000, where in ram you want it to go
    .quad 0x00000001 # 2nd sector, could be 0x1 instead? or 0x2


savedl:
.byte 0x00

hello:
.string "Hello World!"

olin:
.string "Welcome to xv6@UIC. Press any key to continue" # 45 char


.org 0x1fe
.word 0xAA55
