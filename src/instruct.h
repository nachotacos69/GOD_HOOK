#ifndef GEB_INSTRUCT_H
#define GEB_INSTRUCT_H

#include <psptypes.h>

#define USER_RAM_START 0x08800000
#define USER_RAM_END   0x0C000000

#define GEB_ENEMY_INIT_ADDRESS       0x0892D3A8
#define GEB_ENEMY_INIT_ORIGINAL_0    0x27BDFFE0
#define GEB_ENEMY_INIT_ORIGINAL_1    0xAFB20008
#define GEB_ENEMY_HP_LOAD_ADDRESS    0x0892D3CC
#define GEB_ENEMY_HP_LOAD_WORD       0x8CA20134
#define GEB_ENEMY_CAPTURE_ADDRESS    0x0892D3D8
#define GEB_ENEMY_CAPTURE_RESUME     0x0892D3E0
#define GEB_ENEMY_CAPTURE_ORIGINAL_0 0x00001021
#define GEB_ENEMY_CAPTURE_ORIGINAL_1 0xAE230154

//[overlay] Persistent god_menu task with a live event-3 render path.
#define GEB_MENU_RENDER_CALLBACK_OFFSET     0x00000000
#define GEB_MENU_RENDER_ORIGINAL_0          0x27BDFFE0
#define GEB_MENU_RENDER_ORIGINAL_1          0x24030003
#define GEB_MENU_RENDER_EVENT3_OFFSET       0x000001F8
#define GEB_MENU_RENDER_EVENT3_ORIGINAL_0   0x8C820084
#define GEB_MENU_RENDER_EVENT3_ORIGINAL_1   0x8C440018
#define GEB_MENU_RENDER_EVENT3_ORIGINAL_2   0x1080001C
#define GEB_MENU_RENDER_EVENT3_ORIGINAL_3   0x260600C0

//[overlay] EBOOT game-font exports used only from the user render context.
#define GEB_FONT_CREATE_ADDRESS       0x0883E61C
#define GEB_FONT_CREATE_WORD          0x3C0508A4
#define GEB_FONT_CLEAR_ADDRESS        0x0883E908
#define GEB_FONT_CLEAR_WORD           0x27BDFFF0
#define GEB_FONT_APPEND_ADDRESS       0x0883EBAC
#define GEB_FONT_APPEND_WORD          0x27BDFFE0
#define GEB_FONT_DRAW_ADDRESS         0x0883ED38
#define GEB_FONT_DRAW_RESUME          0x0883ED40
#define GEB_FONT_DRAW_ORIGINAL_0      0x27BDFF70
#define GEB_FONT_DRAW_ORIGINAL_1      0xAFB20038
#define GEB_FONT_DESTROY_ADDRESS      0x0883F850
#define GEB_FONT_DESTROY_WORD         0x27BDFFF0
#define GEB_FONT_MEASURE_ADDRESS      0x0883F8F8
#define GEB_FONT_MEASURE_WORD         0x27BDFFC0

#define MIPS_NOP                     0x00000000
#define MIPS_SYNC                    0x0000000F
#define MIPS_J(address)              (0x08000000 | ((((u32)(address)) >> 2) & 0x03FFFFFF))
#define MIPS_JAL(address)            (0x0C000000 | ((((u32)(address)) >> 2) & 0x03FFFFFF))
#define MIPS_LUI(rt, value)          (0x3C000000 | ((u32)(rt) << 16) | ((u32)(value) & 0xFFFF))
#define MIPS_ORI(rt, rs, value)      (0x34000000 | ((u32)(rs) << 21) | ((u32)(rt) << 16) | ((u32)(value) & 0xFFFF))
#define MIPS_LW(rt, offset, base)    (0x8C000000 | ((u32)(base) << 21) | ((u32)(rt) << 16) | ((u32)(offset) & 0xFFFF))
#define MIPS_SW(rt, offset, base)    (0xAC000000 | ((u32)(base) << 21) | ((u32)(rt) << 16) | ((u32)(offset) & 0xFFFF))
#define MIPS_ANDI(rt, rs, value)     (0x30000000 | ((u32)(rs) << 21) | ((u32)(rt) << 16) | ((u32)(value) & 0xFFFF))
#define MIPS_SLL(rd, rt, shift)      (((u32)(rt) << 16) | ((u32)(rd) << 11) | ((u32)(shift) << 6))
#define MIPS_ADDU(rd, rs, rt)        (((u32)(rs) << 21) | ((u32)(rt) << 16) | ((u32)(rd) << 11) | 0x21)
#define MIPS_ADDIU(rt, rs, value)    (0x24000000 | ((u32)(rs) << 21) | ((u32)(rt) << 16) | ((u32)(value) & 0xFFFF))

#define MIPS_REG_V0 2
#define MIPS_REG_T0 8
#define MIPS_REG_T1 9
#define MIPS_REG_T2 10
#define MIPS_REG_S1 17
#define MIPS_REG_SP 29

#endif
