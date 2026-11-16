#include "overlay_layout.h"

    .set noreorder
    .text
    .align 4

//[overlay] Captures one live game-font draw, then continues through displaced code.
    .globl overlay_capture_blob_start
    .globl overlay_capture_blob_end
    .globl overlay_capture_context_lui
    .globl overlay_capture_context_ori
overlay_capture_blob_start:
    addiu   $sp, $sp, -0x40
    sw      $a0, 0x10($sp)
    sw      $a1, 0x14($sp)
    sw      $a2, 0x18($sp)
    sw      $a3, 0x1C($sp)
    sw      $s0, 0x20($sp)
    sw      $ra, 0x24($sp)
    sw      $t9, 0x28($sp)
overlay_capture_context_lui:
    lui     $s0, 0
overlay_capture_context_ori:
    ori     $s0, $s0, 0
    lw      $t0, OVERLAY_CONTEXT_MAGIC($s0)
    lui     $t1, 0x4745
    ori     $t1, $t1, 0x4255
    bne     $t0, $t1, 99f
    nop
    lw      $t0, OVERLAY_CONTEXT_CAPTURE_ENABLE($s0)
    beq     $t0, $zero, 99f
    nop
    lw      $t0, OVERLAY_CONTEXT_TEMPLATE_READY($s0)
    bne     $t0, $zero, 99f
    nop
    lw      $t0, OVERLAY_CONTEXT_FIELD_GENERATION($s0)
    sw      $t0, 0x2C($sp)

    // Tiny admission section: save the mask before reusing v0.
    lw      $t9, OVERLAY_CONTEXT_INTR_SUSPEND($s0)
    jalr    $t9
    nop
    sw      $v0, 0x30($sp)
    sw      $zero, 0x34($sp)
    lw      $t0, OVERLAY_CONTEXT_GATE($s0)
    bne     $t0, $zero, 10f
    nop
    addiu   $t0, $zero, OVERLAY_GATE_USER
    sw      $t0, OVERLAY_CONTEXT_GATE($s0)
    addiu   $t0, $zero, 1
    sw      $t0, 0x34($sp)
10:
    sync
    lw      $a0, 0x30($sp)
    lw      $t9, OVERLAY_CONTEXT_INTR_RESUME($s0)
    jalr    $t9
    nop
    lw      $t0, 0x34($sp)
    beq     $t0, $zero, 99f
    nop
    lw      $t0, OVERLAY_CONTEXT_FIELD_GENERATION($s0)
    lw      $t1, 0x2C($sp)
    bne     $t0, $t1, 90f
    nop
    lw      $t0, OVERLAY_CONTEXT_CAPTURE_ENABLE($s0)
    beq     $t0, $zero, 90f
    nop
    lw      $t0, OVERLAY_CONTEXT_TEMPLATE_READY($s0)
    bne     $t0, $zero, 90f
    nop

    // Only word-aligned complete user spans can be copied.
    lw      $t0, 0x14($sp)
    andi    $t1, $t0, 3
    bne     $t1, $zero, 90f
    nop
    lui     $t1, 0x0880
    sltu    $t2, $t0, $t1
    bne     $t2, $zero, 90f
    nop
    lui     $t1, 0x0C00
    addiu   $t1, $t1, -0x40
    sltu    $t2, $t1, $t0
    bne     $t2, $zero, 90f
    nop
    lw      $t0, 0x18($sp)
    andi    $t1, $t0, 3
    bne     $t1, $zero, 90f
    nop
    lui     $t1, 0x0880
    sltu    $t2, $t0, $t1
    bne     $t2, $zero, 90f
    nop
    lui     $t1, 0x0C00
    addiu   $t1, $t1, -0x30
    sltu    $t2, $t1, $t0
    bne     $t2, $zero, 90f
    nop
    lw      $t0, 0x24($sp)
    sw      $t0, OVERLAY_CONTEXT_CAPTURE_CALLER($s0)
    lw      $t0, 0x10($sp)
    sw      $t0, OVERLAY_CONTEXT_CAPTURE_FONT($s0)
    addiu   $t1, $s0, OVERLAY_CONTEXT_STYLE
    lw      $t2, 0x14($sp)
    addiu   $t3, $zero, 16
1:
    lw      $t0, 0($t2)
    sw      $t0, 0($t1)
    addiu   $t2, $t2, 4
    addiu   $t1, $t1, 4
    addiu   $t3, $t3, -1
    bne     $t3, $zero, 1b
    nop
    addiu   $t1, $s0, OVERLAY_CONTEXT_TRANSFORM
    lw      $t2, 0x18($sp)
    addiu   $t3, $zero, 12
2:
    lw      $t0, 0($t2)
    sw      $t0, 0($t1)
    addiu   $t2, $t2, 4
    addiu   $t1, $t1, 4
    addiu   $t3, $t3, -1
    bne     $t3, $zero, 2b
    nop
    lw      $t0, OVERLAY_CONTEXT_FIELD_GENERATION($s0)
    lw      $t1, 0x2C($sp)
    bne     $t0, $t1, 90f
    nop
    lw      $t0, OVERLAY_CONTEXT_CAPTURE_ENABLE($s0)
    beq     $t0, $zero, 90f
    nop
    sync
    addiu   $t0, $zero, 1
    sw      $t0, OVERLAY_CONTEXT_TEMPLATE_READY($s0)
90:
    lw      $t9, OVERLAY_CONTEXT_INTR_SUSPEND($s0)
    jalr    $t9
    nop
    sw      $v0, 0x30($sp)
    sync
    sw      $zero, OVERLAY_CONTEXT_GATE($s0)
    sync
    lw      $a0, 0x30($sp)
    lw      $t9, OVERLAY_CONTEXT_INTR_RESUME($s0)
    jalr    $t9
    nop
99:
    lw      $a0, 0x10($sp)
    lw      $a1, 0x14($sp)
    lw      $a2, 0x18($sp)
    lw      $a3, 0x1C($sp)
    lw      $s0, 0x20($sp)
    lw      $ra, 0x24($sp)
    lw      $t9, 0x28($sp)
    addiu   $sp, $sp, 0x40
    addiu   $sp, $sp, -0x90
    sw      $s2, 0x38($sp)
    j       0x0883ED40
    nop
overlay_capture_blob_end:

    .align 4

//[overlay] Wraps the persistent god_menu task and renders after its event-3 work.
    .globl overlay_menu_blob_start
    .globl overlay_menu_blob_end
    .globl overlay_menu_context_lui
    .globl overlay_menu_context_ori
    .globl overlay_menu_resume_jump
    .globl overlay_menu_entry_context_lui
    .globl overlay_menu_entry_context_ori
overlay_menu_blob_start:
    addiu   $sp, $sp, -0xC0
    sw      $a0, 0x70($sp)
    sw      $a1, 0x74($sp)
    sw      $a2, 0x78($sp)
    sw      $a3, 0x7C($sp)
    sw      $s0, 0x90($sp)
    sw      $s1, 0x94($sp)
    sw      $s2, 0x98($sp)
    sw      $s3, 0x9C($sp)
    sw      $s4, 0xA0($sp)
    sw      $s5, 0xA4($sp)
    sw      $s6, 0xA8($sp)
    sw      $s7, 0xAC($sp)
    sw      $ra, 0xB0($sp)
    sw      $zero, 0x88($sp)

// Capture entry epoch without changing the original callback's saved registers.
overlay_menu_entry_context_lui:
    lui     $t4, 0
overlay_menu_entry_context_ori:
    ori     $t4, $t4, 0
    lw      $t0, OVERLAY_CONTEXT_FIELD_GENERATION($t4)
    sw      $t0, 0x84($sp)
    bal     90f
    nop
    sw      $v0, 0x80($sp)

overlay_menu_context_lui:
    lui     $s0, 0
overlay_menu_context_ori:
    ori     $s0, $s0, 0
    lw      $t0, OVERLAY_CONTEXT_MAGIC($s0)
    lui     $t1, 0x4745
    ori     $t1, $t1, 0x4255
    bne     $t0, $t1, 60f
    nop

    lw      $t0, OVERLAY_CONTEXT_HOOK_CALLS($s0)
    addiu   $t0, $t0, 1
    sw      $t0, OVERLAY_CONTEXT_HOOK_CALLS($s0)
    lw      $s2, 0x74($sp)

    lw      $t0, OVERLAY_CONTEXT_CLEANUP_REQUEST($s0)
    bne     $t0, $zero, 76f
    nop
    addiu   $t0, $zero, 3
    bne     $s2, $t0, 60f
    nop
76:
    bal     80f
    nop
    beq     $v0, $zero, 60f
    nop
    lw      $t0, OVERLAY_CONTEXT_CLEANUP_REQUEST($s0)
    beq     $t0, $zero, 1f
    nop
    bal     70f
    nop
    b       60f
    nop

1:
    addiu   $t0, $zero, 3
    bne     $s2, $t0, 60f
    nop
    lw      $t0, OVERLAY_CONTEXT_FIELD_GENERATION($s0)
    lw      $t1, 0x84($sp)
    bne     $t0, $t1, 60f
    nop
    lw      $t0, OVERLAY_CONTEXT_EVENT3_CALLS($s0)
    addiu   $t0, $t0, 1
    sw      $t0, OVERLAY_CONTEXT_EVENT3_CALLS($s0)

//[overlay] Do not re-enter game-font rendering while the full pause/menu SSA is active.
    lw      $t0, 0x70($sp)
    lw      $t0, 0x84($t0)
    lw      $t0, 0x18($t0)
    beq     $t0, $zero, 2f
    nop
    addiu   $t1, $zero, 1
    sw      $t1, OVERLAY_CONTEXT_MENU_ACTIVE($s0)
    b       60f
    nop
2:
    sw      $zero, OVERLAY_CONTEXT_MENU_ACTIVE($s0)
    lw      $t0, OVERLAY_CONTEXT_ENABLED($s0)
    beq     $t0, $zero, 60f
    nop
    lw      $t0, OVERLAY_CONTEXT_TEMPLATE_READY($s0)
    beq     $t0, $zero, 60f
    nop

    lw      $t0, OVERLAY_CONTEXT_RENDER_FRAMES($s0)
    addiu   $t0, $t0, 1
    sw      $t0, OVERLAY_CONTEXT_RENDER_FRAMES($s0)
    lw      $t0, OVERLAY_CONTEXT_ACTIVE_PAGE($s0)
    andi    $t0, $t0, 1
    addiu   $s3, $s0, OVERLAY_CONTEXT_PAGE0
    beq     $t0, $zero, 4f
    nop
    addiu   $s3, $s3, OVERLAY_PAGE_SIZE
4:
    lw      $s4, OVERLAY_PAGE_COUNT($s3)
    sltiu   $t0, $s4, 33
    bne     $t0, $zero, 5f
    nop
    addiu   $s4, $zero, 32
5:
    beq     $s4, $zero, 50f
    addiu   $s3, $s3, OVERLAY_PAGE_RUNS

    move    $s5, $zero
    lw      $s6, OVERLAY_CONTEXT_FONT_HANDLE($s0)
    bne     $s6, $zero, 8f
    nop
    lw      $t9, OVERLAY_CONTEXT_FONT_CREATE($s0)
    beq     $t9, $zero, 43f
    nop
    jalr    $t9
    nop
    move    $s6, $v0
    beq     $s6, $zero, 43f
    nop
    lui     $t0, 0x0880
    sltu    $t1, $s6, $t0
    bne     $t1, $zero, 43f
    nop
    lui     $t0, 0x0C00
    sltu    $t1, $s6, $t0
    beq     $t1, $zero, 43f
    nop
    sw      $s6, OVERLAY_CONTEXT_FONT_HANDLE($s0)

8:
9:
    lw      $t0, OVERLAY_CONTEXT_CLEANUP_REQUEST($s0)
    beq     $t0, $zero, 91f
    nop
    bal     70f
    nop
    b       60f
    nop
91:
    lw      $t0, OVERLAY_CONTEXT_ENABLED($s0)
    beq     $t0, $zero, 50f
    nop
    lw      $t0, OVERLAY_CONTEXT_FIELD_GENERATION($s0)
    lw      $t1, 0x84($sp)
    bne     $t0, $t1, 50f
    nop
    beq     $s5, $s4, 50f
    nop
    lw      $s7, OVERLAY_RUN_LENGTH($s3)
    beq     $s7, $zero, 48f
    nop
    sltiu   $t0, $s7, 64
    beq     $t0, $zero, 46f
    nop
    lw      $t0, OVERLAY_CONTEXT_DRAW_ATTEMPTS($s0)
    addiu   $t0, $t0, 1
    sw      $t0, OVERLAY_CONTEXT_DRAW_ATTEMPTS($s0)

    lw      $t9, OVERLAY_CONTEXT_FONT_CLEAR($s0)
    beq     $t9, $zero, 46f
    move    $a0, $s6
    jalr    $t9
    nop
    beq     $v0, $zero, 46f
    nop

    lw      $t9, OVERLAY_CONTEXT_FONT_APPEND($s0)
    beq     $t9, $zero, 47f
    move    $a0, $s6
    addiu   $a1, $s3, OVERLAY_RUN_TEXT
    move    $a2, $s7
    jalr    $t9
    nop
    beq     $v0, $zero, 47f
    nop

    lw      $t9, OVERLAY_CONTEXT_FONT_MEASURE($s0)
    beq     $t9, $zero, 46f
    move    $a0, $s6
    addiu   $a1, $s3, OVERLAY_RUN_TEXT
    move    $a2, $s7
    jalr    $t9
    nop

//[overlay] Restore the proven copy-before-scale draw sequence from version 3.6.
    addiu   $t0, $s0, OVERLAY_CONTEXT_STYLE
    move    $t1, $sp
    addiu   $t2, $zero, 16
10:
    lw      $t3, 0($t0)
    sw      $t3, 0($t1)
    addiu   $t0, $t0, 4
    addiu   $t1, $t1, 4
    addiu   $t2, $t2, -1
    bne     $t2, $zero, 10b
    nop

    addiu   $t0, $s0, OVERLAY_CONTEXT_TRANSFORM
    addiu   $t1, $sp, 0x40
    addiu   $t2, $zero, 12
11:
    lw      $t3, 0($t0)
    sw      $t3, 0($t1)
    addiu   $t0, $t0, 4
    addiu   $t1, $t1, 4
    addiu   $t2, $t2, -1
    bne     $t2, $zero, 11b
    nop

    lw      $t4, OVERLAY_RUN_FLAGS($s3)
    andi    $t0, $t4, OVERLAY_RUN_FLAG_BOND
    beq     $t0, $zero, 12f
    nop
    lwc1    $f2, OVERLAY_CONTEXT_BOND_SCALE_BITS($s0)
    b       13f
    nop
12:
    lwc1    $f2, OVERLAY_CONTEXT_MAIN_SCALE_BITS($s0)
13:
    mov.s   $f14, $f2
    c.eq.s  $f0, $f0
    bc1f    17f
    nop
    mtc1    $zero, $f8
    c.le.s  $f0, $f8
    bc1t    17f
    nop
    lwc1    $f4, OVERLAY_RUN_WIDTH_BITS($s3)
    mul.s   $f6, $f0, $f2
    c.le.s  $f6, $f4
    bc1t    18f
    nop
    div.s   $f2, $f4, $f0
    b       18f
    nop
17:
    lw      $t0, OVERLAY_CONTEXT_MEASURE_FALLBACKS($s0)
    addiu   $t0, $t0, 1
    sw      $t0, OVERLAY_CONTEXT_MEASURE_FALLBACKS($s0)
18:
    swc1    $f2, 0x40($sp)
    swc1    $f14, 0x44($sp)
    lw      $t0, OVERLAY_RUN_X_BITS($s3)
    sw      $t0, 0x30($sp)
    lw      $t0, OVERLAY_RUN_Y_BITS($s3)
    sw      $t0, 0x34($sp)

//[overlay] Bonds are translucent; broken bonds use RGB 255/153/153.
    andi    $t1, $t4, OVERLAY_RUN_FLAG_BROKEN
    bne     $t1, $zero, 19f
    nop
    andi    $t1, $t4, OVERLAY_RUN_FLAG_BOND
    bne     $t1, $zero, 20f
    nop
    lui     $t0, 0x3F80
    sw      $t0, 0x50($sp)
    sw      $t0, 0x54($sp)
    sw      $t0, 0x58($sp)
    sw      $t0, 0x5C($sp)
    b       16f
    nop
19:
    lui     $t0, 0x3F80
    sw      $t0, 0x50($sp)
    lui     $t1, 0x3F19
    ori     $t1, $t1, 0x999A
    sw      $t1, 0x54($sp)
    sw      $t1, 0x58($sp)
    lui     $t1, 0x3F59
    ori     $t1, $t1, 0x999A
    sw      $t1, 0x5C($sp)
    b       16f
    nop
20:
    lui     $t0, 0x3F80
    sw      $t0, 0x50($sp)
    sw      $t0, 0x54($sp)
    sw      $t0, 0x58($sp)
    lui     $t1, 0x3F33
    ori     $t1, $t1, 0x3333
    sw      $t1, 0x5C($sp)
16:

    lw      $t9, OVERLAY_CONTEXT_FONT_DRAW($s0)
    beq     $t9, $zero, 44f
    move    $a0, $s6
    move    $a1, $sp
    addiu   $a2, $sp, 0x40
    jalr    $t9
    nop
    beq     $v0, $zero, 44f
    nop
    lw      $t0, OVERLAY_CONTEXT_DRAW_COUNT($s0)
    addiu   $t0, $t0, 1
    sw      $t0, OVERLAY_CONTEXT_DRAW_COUNT($s0)
    b       48f
    nop

43:
    lw      $t0, OVERLAY_CONTEXT_PREP_FAILURES($s0)
    addiu   $t0, $t0, 1
    sw      $t0, OVERLAY_CONTEXT_PREP_FAILURES($s0)
    lw      $t0, OVERLAY_CONTEXT_DRAW_FAILURES($s0)
    addiu   $t0, $t0, 1
    sw      $t0, OVERLAY_CONTEXT_DRAW_FAILURES($s0)
    b       50f
    nop
44:
    lw      $t0, OVERLAY_CONTEXT_FONT_FAILURES($s0)
    addiu   $t0, $t0, 1
    sw      $t0, OVERLAY_CONTEXT_FONT_FAILURES($s0)
    b       49f
    nop
46:
    lw      $t0, OVERLAY_CONTEXT_PREP_FAILURES($s0)
    addiu   $t0, $t0, 1
    sw      $t0, OVERLAY_CONTEXT_PREP_FAILURES($s0)
    b       49f
    nop
47:
    lw      $t0, OVERLAY_CONTEXT_APPEND_FAILURES($s0)
    addiu   $t0, $t0, 1
    sw      $t0, OVERLAY_CONTEXT_APPEND_FAILURES($s0)
    b       49f
    nop
49:
    lw      $t0, OVERLAY_CONTEXT_DRAW_FAILURES($s0)
    addiu   $t0, $t0, 1
    sw      $t0, OVERLAY_CONTEXT_DRAW_FAILURES($s0)
    b       48f
    nop
48:
    addiu   $s3, $s3, OVERLAY_RUN_SIZE
    addiu   $s5, $s5, 1
    b       9b
    nop

50:
    lw      $s6, OVERLAY_CONTEXT_FONT_HANDLE($s0)
    beq     $s6, $zero, 60f
    nop
    lw      $t9, OVERLAY_CONTEXT_FONT_CLEAR($s0)
    beq     $t9, $zero, 60f
    move    $a0, $s6
    jalr    $t9
    nop

60:
    lw      $t0, 0x88($sp)
    beq     $t0, $zero, 61f
    nop
    bal     85f
    nop
61:
    lw      $v0, 0x80($sp)
    lw      $s0, 0x90($sp)
    lw      $s1, 0x94($sp)
    lw      $s2, 0x98($sp)
    lw      $s3, 0x9C($sp)
    lw      $s4, 0xA0($sp)
    lw      $s5, 0xA4($sp)
    lw      $s6, 0xA8($sp)
    lw      $s7, 0xAC($sp)
    lw      $ra, 0xB0($sp)
    jr      $ra
    addiu   $sp, $sp, 0xC0

//[overlay] Releases only the overlay-owned font object from the menu game thread.
70:
    move    $s7, $ra
    sw      $zero, OVERLAY_CONTEXT_ENABLED($s0)
    lw      $s6, OVERLAY_CONTEXT_FONT_HANDLE($s0)
    beq     $s6, $zero, 73f
    nop
    lw      $t9, OVERLAY_CONTEXT_FONT_CLEAR($s0)
    beq     $t9, $zero, 71f
    move    $a0, $s6
    jalr    $t9
    nop
71:
    lw      $t9, OVERLAY_CONTEXT_FONT_DESTROY($s0)
    beq     $t9, $zero, 73f
    move    $a0, $s6
    jalr    $t9
    nop
73:
    sw      $zero, OVERLAY_CONTEXT_FONT_HANDLE($s0)
    sw      $zero, OVERLAY_CONTEXT_CLEANUP_REQUEST($s0)
    lw      $t0, OVERLAY_CONTEXT_CLEANUP_ACK($s0)
    addiu   $t0, $t0, 1
    sw      $t0, OVERLAY_CONTEXT_CLEANUP_ACK($s0)
    jr      $s7
    nop

// Atomic admission/release through the game's validated existing user imports.
// s1 is private to this wrapper and remains saved in its outer frame.
overlay_menu_gate_acquire:
80:
    move    $s1, $ra
    lw      $t9, OVERLAY_CONTEXT_INTR_SUSPEND($s0)
    jalr    $t9
    nop
    sw      $v0, 0x8C($sp)
    sw      $zero, 0x88($sp)
    lw      $t0, OVERLAY_CONTEXT_GATE($s0)
    bne     $t0, $zero, 81f
    nop
    addiu   $t0, $zero, OVERLAY_GATE_USER
    sw      $t0, OVERLAY_CONTEXT_GATE($s0)
    addiu   $t0, $zero, 1
    sw      $t0, 0x88($sp)
81:
    sync
    lw      $a0, 0x8C($sp)
    lw      $t9, OVERLAY_CONTEXT_INTR_RESUME($s0)
    jalr    $t9
    nop
    lw      $v0, 0x88($sp)
    jr      $s1
    nop
overlay_menu_gate_release:
85:
    move    $s1, $ra
    lw      $t9, OVERLAY_CONTEXT_INTR_SUSPEND($s0)
    jalr    $t9
    nop
    sw      $v0, 0x8C($sp)
    sync
    sw      $zero, OVERLAY_CONTEXT_GATE($s0)
    sync
    lw      $a0, 0x8C($sp)
    lw      $t9, OVERLAY_CONTEXT_INTR_RESUME($s0)
    jalr    $t9
    nop
    jr      $s1
    nop

//[overlay] Executes both displaced god_menu words before resuming its callback.
90:
    addiu   $sp, $sp, -0x20
    addiu   $v1, $zero, 3
overlay_menu_resume_jump:
    .word   0x08000000
    nop
overlay_menu_blob_end:

    .set reorder
