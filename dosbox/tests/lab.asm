; C1Max DOS Lab: original 16-bit DOS program, MIT license.
; VGA mode 13h, BIOS keyboard, mouse driver and a score saved via DOS INT 21h.
bits 16
org 100h
start:
    push cs
    pop ds
    mov ax,13h
    int 10h
    xor ax,ax
    int 33h
    mov ax,0a000h
    mov es,ax
    ; Clear only once. Clearing VGA RAM every tick exposes blank/partial
    ; frames to real VGA scanout and to DOSBox's video callback.
    xor di,di
    mov ax,0101h
    mov cx,32000
    rep stosw
    mov si,title
    xor dx,dx
    call text
    mov si,help
    mov dx,0100h
    call text
main:
    call render_changes
    jmp read_input
render_changes:
    mov ax,[x]
    cmp ax,[oldx]
    jne .redraw
    mov ax,[y]
    cmp ax,[oldy]
    jne .redraw
    mov ax,[starx]
    cmp ax,[oldstarx]
    jne .redraw
    mov ax,[stary]
    cmp ax,[oldstary]
    je .done
.redraw:
    cmp word [oldx],0ffffh
    je .paint
    mov ax,[oldx]
    mov dx,[oldy]
    mov bl,1
    call box
    mov ax,[oldstarx]
    mov dx,[oldstary]
    mov bl,1
    call box
.paint:
    mov ax,[x]
    mov dx,[y]
    mov [oldx],ax
    mov [oldy],dx
    mov bl,10
    call box
    mov ax,[starx]
    mov dx,[stary]
    mov [oldstarx],ax
    mov [oldstary],dx
    mov bl,14
    call box
    mov al,[score]
    cmp al,[oldscore]
    je .done
    mov [oldscore],al
    add al,'0'
    mov [scoretext+7],al
    mov si,scoretext
    mov dx,0200h
    call text
.done:
    ret
read_input:
    mov ax,3
    int 33h
    test bx,1
    jz keyboard
    shr cx,1
    mov [x],cx
    mov [y],dx
keyboard:
    mov ah,1
    int 16h
    jz clamp
    xor ah,ah
    int 16h
    mov [lastkey],ax
    cmp al,27
    je finish
    cmp al,'w'
    je up
    cmp ah,48h
    je up
    cmp al,'s'
    je down
    cmp ah,50h
    je down
    cmp al,'a'
    je left
    cmp ah,4bh
    je left
    cmp al,'d'
    je right
    cmp ah,4dh
    je right
    jmp clamp
up: sub word [y],8
    jmp clamp
down: add word [y],8
    jmp clamp
left: sub word [x],8
    jmp clamp
right: add word [x],8
clamp:
    cmp word [x],0
    jge xhigh
    mov word [x],0
xhigh:
    cmp word [x],309
    jle ylow
    mov word [x],309
ylow:
    cmp word [y],32
    jge yhigh
    mov word [y],32
yhigh:
    cmp word [y],189
    jle check
    mov word [y],189
check:
    mov ax,[x]
    sub ax,[starx]
    add ax,9
    cmp ax,18
    ja waittick
    mov ax,[y]
    sub ax,[stary]
    add ax,9
    cmp ax,18
    ja waittick
    inc byte [score]
    cmp byte [score],10
    jb nextstar
    mov byte [score],0
nextstar:
    add word [starx],67
    cmp word [starx],295
    jb star_y
    sub word [starx],270
star_y:
    add word [stary],37
    cmp word [stary],185
    jb waittick
    sub word [stary],135
waittick:
    mov ah,0
    int 1ah
    mov bx,dx
again:
    mov ah,0
    int 1ah
    cmp dx,bx
    je again
    jmp main
finish:
    mov ax,3
    int 10h
    mov ah,3ch
    xor cx,cx
    mov dx,filename
    int 21h
    jc exit
    mov bx,ax
    mov ah,40h
    mov cx,7
    mov dx,score
    int 21h
    mov ah,3eh
    int 21h
exit:
    mov ax,4c00h
    int 21h
text:
    mov ah,2
    xor bh,bh
    int 10h
.next:
    lodsb
    test al,al
    jz .done
    mov ah,0eh
    mov bx,15
    int 10h
    jmp .next
.done: ret
box:
    push ax
    mov ax,dx
    mov cx,320
    mul cx
    pop dx
    add ax,dx
    mov di,ax
    mov al,bl
    mov dx,10
.row:
    mov cx,10
    rep stosb
    add di,310
    dec dx
    jnz .row
    ret
title db 'C1MAX DOS LAB - collect yellow squares',0
help db 'WASD/arrows or touch. ESC: save & return',0
scoretext db 'Score: 0',0
filename db 'RESULT.DAT',0
score db 0
x dw 80
y dw 88
lastkey dw 0
starx dw 240
stary dw 120
oldx dw 0ffffh
oldy dw 0
oldstarx dw 0
oldstary dw 0
oldscore db 0ffh
