#include <pspkernel.h>
#include <pspsysmem.h>
#include <psputils.h>
#include <stddef.h>
#include <string.h>

#include "hook.h"
#include "instruct.h"
#include "log.h"
#include "overlay.h"
#include "overlay_layout.h"

#define OVERLAY_MAGIC 0x47454255
#define OVERLAY_BLOCK_SIZE 0x6000
#define OVERLAY_RUN_CHARS 64
#define OVERLAY_TIMEOUT_TICKS 20
#define OVERLAY_MAIN_SCALE 0.65f
//[overlay] Bond text uses one proportional subordinate row per enemy.
#define OVERLAY_BOND_SCALE 0.60f
#define OVERLAY_START_Y 172.0f
#define OVERLAY_MAIN_STEP 11.0f
#define OVERLAY_BOND_LINE_STEP 12.0f
#define OVERLAY_BOND_INDENT 4.0f
#define OVERLAY_BOND_GAP 3.0f
//[overlay] Calibrates fallback advances against observed PSP game-font widths.
#define OVERLAY_BOND_WIDTH_FACTOR 1.25f
#define OVERLAY_BOTTOM_Y 268.0f

typedef struct OverlayRunData {
    u32 length;
    u32 flags;
    u32 x_bits;
    u32 y_bits;
    u32 width_bits;
    u32 text[OVERLAY_RUN_CHARS];
} OverlayRunData;

typedef struct OverlayPageData {
    u32 count;
    u32 sequence;
    OverlayRunData runs[OVERLAY_MAX_RUNS];
} OverlayPageData;

typedef struct OverlayContext {
    volatile u32 magic;
    volatile u32 enabled;
    volatile u32 capture_enable;
    volatile u32 template_ready;
    volatile u32 active_page;
    volatile u32 hook_calls;
    volatile u32 event3_calls;
    volatile u32 render_frames;
    volatile u32 draw_attempts;
    volatile u32 draw_count;
    volatile u32 append_failures;
    volatile u32 draw_failures;
    volatile u32 font_handle;
    volatile u32 cleanup_request;
    volatile u32 cleanup_ack;
    volatile u32 capture_caller;
    volatile u32 capture_font;
    volatile u32 field_generation;
    u32 font_create;
    u32 font_clear;
    u32 font_append;
    u32 font_measure;
    u32 font_draw;
    u32 font_destroy;
    u32 main_scale_bits;
    u32 bond_scale_bits;
    u32 max_width_bits;
    volatile u32 menu_active;
    u32 start_y_bits;
    u32 main_step_bits;
    u32 bond_step_bits;
    u32 bottom_y_bits;
    volatile u32 layout_truncated;
    volatile u32 prep_failures;
    volatile u32 measure_fallbacks;
    volatile u32 font_failures;
    volatile u32 style[16];
    volatile u32 transform[12];
    OverlayPageData pages[2];
    volatile u32 gate;
    u32 intr_suspend;
    u32 intr_resume;
} OverlayContext;

typedef char OverlayRunSize[(sizeof(OverlayRunData) == OVERLAY_RUN_SIZE) ? 1 : -1];
typedef char OverlayPageSize[(sizeof(OverlayPageData) == OVERLAY_PAGE_SIZE) ? 1 : -1];
typedef char OverlayContextSize[(sizeof(OverlayContext) == OVERLAY_CONTEXT_SIZE) ? 1 : -1];
typedef char OverlayStyleOffset[(offsetof(OverlayContext, style) == OVERLAY_CONTEXT_STYLE) ? 1 : -1];
typedef char OverlayTransformOffset[(offsetof(OverlayContext, transform) == OVERLAY_CONTEXT_TRANSFORM) ? 1 : -1];
typedef char OverlayGateOffset[(offsetof(OverlayContext, gate) == OVERLAY_CONTEXT_GATE) ? 1 : -1];
typedef char OverlaySuspendOffset[(offsetof(OverlayContext, intr_suspend) == OVERLAY_CONTEXT_INTR_SUSPEND) ? 1 : -1];
typedef char OverlayResumeOffset[(offsetof(OverlayContext, intr_resume) == OVERLAY_CONTEXT_INTR_RESUME) ? 1 : -1];
typedef char OverlayPageOffset[(offsetof(OverlayContext, pages) == OVERLAY_CONTEXT_PAGE0) ? 1 : -1];

extern u8 overlay_capture_blob_start[];
extern u8 overlay_capture_blob_end[];
extern u8 overlay_capture_context_lui[];
extern u8 overlay_capture_context_ori[];
extern u8 overlay_menu_blob_start[];
extern u8 overlay_menu_blob_end[];
extern u8 overlay_menu_context_lui[];
extern u8 overlay_menu_context_ori[];
extern u8 overlay_menu_entry_context_lui[];
extern u8 overlay_menu_entry_context_ori[];
extern u8 overlay_menu_resume_jump[];

static SceUID g_overlay_block = -1;
static OverlayContext *g_overlay;
static u32 g_overlay_base;
static u32 g_capture_code;
static u32 g_menu_code;
static u32 g_capture_patch;
static u32 g_menu_patch;
static u32 g_menu_callback;
static u32 g_menu_text;
static u32 g_menu_text_size;
static SceUID g_menu_uid = -1;
static u32 g_page_sequence;
static u32 g_generation;
static u32 g_menu_start_hook_calls;
static u32 g_menu_start_event3_calls;
static u32 g_start_render_frames;
static u32 g_start_draw_attempts;
static u32 g_start_draw_count;
static u32 g_start_append_failures;
static u32 g_start_draw_failures;
static u32 g_start_prep_failures;
static u32 g_start_measure_fallbacks;
static u32 g_start_font_failures;
static u32 g_start_cleanup_ack;
static int g_installed;
static int g_menu_hook_installed;
static int g_font_probe_installed;
static int g_field_active;
static int g_menu_reported;
static int g_menu_timeout_reported;
static int g_template_reported;
static int g_overlay_reported;
static int g_draw_reported;
static int g_cleanup_reported;
static int g_template_timeout_reported;
static int g_draw_timeout_reported;
static int g_run_limit_reported;
static int g_layout_limit_reported;
static int g_menu_poll_ticks;
static int g_field_poll_ticks;
static int g_draw_wait_ticks;
static int g_menu_active_state;
static int g_once_reachable;
static SceUID g_bound_menu_uid = -1;
static u32 g_bound_menu_text;
static int g_menu_reload_reported;
static int g_field_pending;
static u32 g_pending_generation;
static u32 g_retired_generation;
static u32 g_publication_deferrals;
static u32 g_start_publication_deferrals;
static int g_field_summary_pending;

static int user_range_valid(u32 address, u32 size)
{
    return size != 0 && address >= USER_RAM_START && address + size >= address &&
        address + size <= USER_RAM_END;
}

static u32 align_up(u32 value, u32 alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static u32 float_bits(float value)
{
    union {
        float f;
        u32 u;
    } bits;
    bits.f = value;
    return bits.u;
}

static float bits_float(u32 value)
{
    union {
        float f;
        u32 u;
    } bits;
    bits.u = value;
    return bits.f;
}

static void publish_code(const void *address, unsigned int size)
{
    sceKernelDcacheWritebackInvalidateRange(address, size);
    sceKernelIcacheInvalidateRange(address, size);
}

static void publish_data(const void *address, unsigned int size)
{
    sceKernelDcacheWritebackInvalidateRange(address, size);
    __asm__ volatile("sync" ::: "memory");
}

/* One CPU, one shared user address: only the short claim/release is IRQ-protected. */
static int producer_try_acquire(void)
{
    u32 state = sceKernelCpuSuspendIntr();
    int acquired = g_overlay->gate == OVERLAY_GATE_IDLE;
    if (acquired) {
        g_overlay->gate = OVERLAY_GATE_PRODUCER;
    }
    __asm__ volatile("sync" ::: "memory");
    sceKernelCpuResumeIntr(state);
    return acquired;
}

static void producer_release(void)
{
    u32 state = sceKernelCpuSuspendIntr();
    __asm__ volatile("sync" ::: "memory");
    g_overlay->gate = OVERLAY_GATE_IDLE;
    __asm__ volatile("sync" ::: "memory");
    sceKernelCpuResumeIntr(state);
}

static void write_code_pair(u32 address, u32 first, u32 second)
{
    u32 interrupt_state = sceKernelCpuSuspendIntr();
    volatile u32 *target = (volatile u32 *)address;

    //[overlay] Publish each hook pair while game threads cannot see a partial patch.
    target[0] = first;
    target[1] = second;
    publish_code((void *)address, 8);
    sceKernelCpuResumeIntr(interrupt_state);
}

static void patch_context_address(u32 code, const u8 *blob_start,
    const u8 *lui_label, const u8 *ori_label, u32 context)
{
    u32 lui_offset = (u32)(lui_label - blob_start);
    u32 ori_offset = (u32)(ori_label - blob_start);
    volatile u32 *lui_word = (volatile u32 *)(code + lui_offset);
    volatile u32 *ori_word = (volatile u32 *)(code + ori_offset);

    *lui_word = (*lui_word & 0xFFFF0000) | ((context >> 16) & 0xFFFF);
    *ori_word = (*ori_word & 0xFFFF0000) | (context & 0xFFFF);
}

static void patch_menu_resume(u32 callback)
{
    u32 offset = (u32)(overlay_menu_resume_jump - overlay_menu_blob_start);
    *(volatile u32 *)(g_menu_code + offset) = MIPS_J(callback + 8);
}

static int eboot_fingerprints_match(void)
{
    return *(volatile u32 *)GEB_FONT_CREATE_ADDRESS == GEB_FONT_CREATE_WORD &&
        *(volatile u32 *)GEB_FONT_CLEAR_ADDRESS == GEB_FONT_CLEAR_WORD &&
        *(volatile u32 *)GEB_FONT_APPEND_ADDRESS == GEB_FONT_APPEND_WORD &&
        *(volatile u32 *)GEB_FONT_MEASURE_ADDRESS == GEB_FONT_MEASURE_WORD &&
        *(volatile u32 *)GEB_FONT_DRAW_ADDRESS == GEB_FONT_DRAW_ORIGINAL_0 &&
        *(volatile u32 *)(GEB_FONT_DRAW_ADDRESS + 4) == GEB_FONT_DRAW_ORIGINAL_1 &&
        *(volatile u32 *)GEB_FONT_DESTROY_ADDRESS == GEB_FONT_DESTROY_WORD;
}

static int menu_fingerprints_match(u32 text, u32 callback)
{
    u32 event3 = text + GEB_MENU_RENDER_EVENT3_OFFSET;
    return *(volatile u32 *)callback == GEB_MENU_RENDER_ORIGINAL_0 &&
        *(volatile u32 *)(callback + 4) == GEB_MENU_RENDER_ORIGINAL_1 &&
        *(volatile u32 *)event3 == GEB_MENU_RENDER_EVENT3_ORIGINAL_0 &&
        *(volatile u32 *)(event3 + 4) == GEB_MENU_RENDER_EVENT3_ORIGINAL_1 &&
        *(volatile u32 *)(event3 + 8) == GEB_MENU_RENDER_EVENT3_ORIGINAL_2 &&
        *(volatile u32 *)(event3 + 12) == GEB_MENU_RENDER_EVENT3_ORIGINAL_3;
}

static int menu_module_live(void)
{
    GebModule *module;
    if (g_menu_uid < 0) {
        return 0;
    }
    module = hook_find_module_by_uid(g_menu_uid);
    return module != 0 && module->modid == g_menu_uid &&
        module->text_addr == g_menu_text && module->text_size == g_menu_text_size &&
        strncmp(module->modname, "god_menu", 27) == 0;
}

static int install_font_probe(void)
{
    u32 first = *(volatile u32 *)GEB_FONT_DRAW_ADDRESS;
    u32 second = *(volatile u32 *)(GEB_FONT_DRAW_ADDRESS + 4);

    if (g_font_probe_installed) {
        return 1;
    }
    if (first == GEB_FONT_DRAW_ORIGINAL_0 && second == GEB_FONT_DRAW_ORIGINAL_1) {
        g_once_reachable = 1;
        write_code_pair(GEB_FONT_DRAW_ADDRESS, g_capture_patch, MIPS_NOP);
    } else if (first != g_capture_patch || second != MIPS_NOP) {
        return 0;
    }
    g_font_probe_installed = 1;
    return 1;
}

static int restore_code_pair(u32 address, u32 patch, u32 original_first, u32 original_second)
{
    u32 state = sceKernelCpuSuspendIntr();
    volatile u32 *words = (volatile u32 *)address;
    int restored = 0;
    if (words[0] == patch && words[1] == MIPS_NOP) {
        words[0] = original_first;
        words[1] = original_second;
        publish_code((void *)address, 8);
        restored = 1;
    } else if (words[0] == original_first && words[1] == original_second) {
        restored = 1;
    }
    sceKernelCpuResumeIntr(state);
    return restored;
}

static int restore_font_probe(void)
{
    if (!g_font_probe_installed) {
        return 1;
    }
    if (!restore_code_pair(GEB_FONT_DRAW_ADDRESS, g_capture_patch,
            GEB_FONT_DRAW_ORIGINAL_0, GEB_FONT_DRAW_ORIGINAL_1)) {
        return 0;
    }
    g_font_probe_installed = 0;
    return 1;
}

static int restore_menu_hook(void)
{
    if (!g_menu_hook_installed) {
        return 1;
    }
    if (!menu_module_live()) {
        g_menu_hook_installed = 0;
        return 2;
    }
    if (!restore_code_pair(g_menu_callback, g_menu_patch,
            GEB_MENU_RENDER_ORIGINAL_0, GEB_MENU_RENDER_ORIGINAL_1)) {
        return 0;
    }
    g_menu_hook_installed = 0;
    return 1;
}

static int template_valid(void)
{
    float sx;
    float sy;
    unsigned int i;

    if (!user_range_valid(g_overlay->capture_caller, 4) ||
        !user_range_valid(g_overlay->capture_font, 4)) {
        return 0;
    }
    sx = bits_float(g_overlay->transform[0]);
    sy = bits_float(g_overlay->transform[1]);
    if (!(sx >= 0.05f && sx <= 2.0f && sy >= 0.05f && sy <= 2.0f)) {
        return 0;
    }
    for (i = 4; i < 8; ++i) {
        float component = bits_float(g_overlay->transform[i]);
        if (!(component >= 0.0f && component <= 1.0f)) {
            return 0;
        }
    }
    return 1;
}

static unsigned int active_page_count(void)
{
    u32 index = g_overlay->active_page & 1;
    u32 count = g_overlay->pages[index].count;
    return count > OVERLAY_MAX_RUNS ? OVERLAY_MAX_RUNS : count;
}

static unsigned int append_codepoint(OverlayRunData *run, unsigned int length,
    u32 codepoint)
{
    if (length + 1 < OVERLAY_RUN_CHARS) {
        run->text[length++] = codepoint;
    }
    return length;
}

static u32 read_utf8_codepoint(const char **input)
{
    const u8 *text = (const u8 *)*input;
    u32 value;

    if (text[0] < 0x80) {
        *input += text[0] != 0 ? 1 : 0;
        return text[0];
    }
    if ((text[0] & 0xE0) == 0xC0 && (text[1] & 0xC0) == 0x80) {
        value = ((u32)(text[0] & 0x1F) << 6) | (text[1] & 0x3F);
        *input += 2;
        return value >= 0x80 ? value : '?';
    }
    if ((text[0] & 0xF0) == 0xE0 && (text[1] & 0xC0) == 0x80 &&
        (text[2] & 0xC0) == 0x80) {
        value = ((u32)(text[0] & 0x0F) << 12) | ((u32)(text[1] & 0x3F) << 6) |
            (text[2] & 0x3F);
        *input += 3;
        return value >= 0x800 ? value : '?';
    }
    *input += 1;
    return '?';
}

static unsigned int append_decimal(OverlayRunData *run, unsigned int length,
    unsigned int value)
{
    u32 reverse[10];
    unsigned int count = 0;
    unsigned int i;

    do {
        reverse[count++] = '0' + value % 10;
        value /= 10;
    } while (value != 0 && count < sizeof(reverse) / sizeof(reverse[0]));
    for (i = 0; i < count; ++i) {
        length = append_codepoint(run, length, reverse[count - i - 1]);
    }
    return length;
}

static unsigned int append_text(OverlayRunData *run, unsigned int length,
    const char *text, unsigned int reserve)
{
    while (text != 0 && *text != '\0' &&
        length + reserve < OVERLAY_RUN_CHARS) {
        length = append_codepoint(run, length, read_utf8_codepoint(&text));
    }
    return length;
}

static float estimate_bond_width(const OverlayRunData *run)
{
    float width = 0.0f;
    unsigned int i;
    //[overlay] Fallback glyph advances are rendered-pixel estimates at bond scale 0.60.
    for (i = 0; i < run->length; ++i) {
        u32 ch = run->text[i];
        if (ch == ' ') {
            width += 2.5f;
        } else if (ch == '[' || ch == ']' || ch == ',' || ch == ':' ||
            ch == '.' || ch == '/' || ch == '\'' || ch == '-') {
            width += 3.0f;
        } else if (ch >= '0' && ch <= '9') {
            width += 4.5f;
        } else if (ch == 'i' || ch == 'l' || ch == 'I') {
            width += 2.5f;
        } else if (ch == 'M' || ch == 'W' || ch == 'm' || ch == 'w') {
            width += 6.5f;
        } else if (ch >= 'A' && ch <= 'Z') {
            width += 5.5f;
        } else {
            width += 4.5f;
        }
    }
    return width;
}

static int append_enemy_runs(OverlayPageData *page, const OverlayEnemy *enemy,
    float *last_y, int *first_enemy)
{
    OverlayRunData *run;
    unsigned int length = 0;
    unsigned int i;
    float bond_x = OVERLAY_BOND_INDENT;
    float main_y = *first_enemy ? OVERLAY_START_Y : *last_y + OVERLAY_MAIN_STEP;
    float final_y = enemy->part_count != 0 ?
        main_y + OVERLAY_BOND_LINE_STEP : main_y;

    if (enemy->part_count > OVERLAY_MAX_PARTS ||
        page->count + 1 + enemy->part_count > OVERLAY_MAX_RUNS ||
        final_y > OVERLAY_BOTTOM_Y) {
        return 0;
    }
    run = &page->runs[page->count++];
    run->flags = OVERLAY_RUN_FLAG_MAIN;
    run->x_bits = float_bits(0.0f);
    run->y_bits = float_bits(main_y);
    run->width_bits = float_bits(160.0f);
    length = append_text(run, length, enemy->name, 24);
    length = append_codepoint(run, length, ':');
    length = append_codepoint(run, length, ' ');
    length = append_decimal(run, length, (unsigned int)enemy->current_hp);
    length = append_codepoint(run, length, '/');
    length = append_decimal(run, length, (unsigned int)enemy->max_hp);
    if (enemy->part_count != 0) {
        length = append_codepoint(run, length, '.');
        length = append_codepoint(run, length, ' ');
    }
    run->text[length] = 0;
    run->length = length;

    for (i = 0; i < enemy->part_count; ++i) {
        const OverlayPart *part = &enemy->parts[i];
        run = &page->runs[page->count++];
        run->flags = OVERLAY_RUN_FLAG_BOND |
            (part->broken ? OVERLAY_RUN_FLAG_BROKEN : 0);
        length = 0;
        if (i == 0) {
            length = append_codepoint(run, length, '[');
        }
        length = append_text(run, length, part->name, 16);
        length = append_codepoint(run, length, ':');
        length = append_codepoint(run, length, ' ');
        length = append_decimal(run, length, (unsigned int)part->current_hp);
        if (i + 1 == enemy->part_count) {
            length = append_codepoint(run, length, ']');
        } else {
            length = append_codepoint(run, length, ',');
            length = append_codepoint(run, length, ' ');
        }
        run->text[length] = 0;
        run->length = length;
        {
            float width = estimate_bond_width(run) * OVERLAY_BOND_WIDTH_FACTOR;
            run->x_bits = float_bits(bond_x);
            run->y_bits = float_bits(main_y + OVERLAY_BOND_LINE_STEP);
            run->width_bits = float_bits(width);
            bond_x += width + OVERLAY_BOND_GAP;
        }
    }
    *last_y = final_y;
    *first_enemy = 0;
    return 1;
}

static void reset_field_diagnostics(void)
{
    g_start_render_frames = g_overlay->render_frames;
    g_start_draw_attempts = g_overlay->draw_attempts;
    g_start_draw_count = g_overlay->draw_count;
    g_start_append_failures = g_overlay->append_failures;
    g_start_draw_failures = g_overlay->draw_failures;
    g_start_prep_failures = g_overlay->prep_failures;
    g_start_measure_fallbacks = g_overlay->measure_fallbacks;
    g_start_font_failures = g_overlay->font_failures;
    g_start_cleanup_ack = g_overlay->cleanup_ack;
    g_start_publication_deferrals = g_publication_deferrals;
    g_template_reported = 0;
    g_overlay_reported = 0;
    g_draw_reported = 0;
    g_cleanup_reported = 0;
    g_template_timeout_reported = 0;
    g_draw_timeout_reported = 0;
    g_run_limit_reported = 0;
    g_layout_limit_reported = 0;
    g_field_poll_ticks = 0;
    g_draw_wait_ticks = 0;
}

int overlay_install(void)
{
    unsigned int capture_size = (unsigned int)(overlay_capture_blob_end -
        overlay_capture_blob_start);
    unsigned int menu_size = (unsigned int)(overlay_menu_blob_end -
        overlay_menu_blob_start);
    u32 capture_offset;
    u32 menu_offset;
    u32 intr_suspend;
    u32 intr_resume;

    if (g_installed) {
        return 0;
    }
    if (!eboot_fingerprints_match()) {
        return -1;
    }
    if (capture_size == 0 || capture_size > 0x400 ||
        menu_size == 0 || menu_size > 0x1400) {
        return -2;
    }
    intr_suspend = hook_find_main_user_import("Kernel_Library", 0x092968F4, 0x08A0F028);
    intr_resume = hook_find_main_user_import("Kernel_Library", 0x5F10D406, 0x08A0F060);
    if (intr_suspend == 0 || intr_resume == 0) {
        log_printf("[UI] gate_imports suspend=%08X resume=%08X status=UNAVAILABLE\n",
            intr_suspend, intr_resume);
        return -5;
    }
    g_overlay_block = sceKernelAllocPartitionMemory(PSP_MEMORY_PARTITION_USER,
        "GEBHpOverlay", PSP_SMEM_High, OVERLAY_BLOCK_SIZE, 0);
    if (g_overlay_block < 0) {
        return -3;
    }
    g_overlay_base = (u32)sceKernelGetBlockHeadAddr(g_overlay_block);
    capture_offset = align_up(sizeof(OverlayContext), 64);
    menu_offset = align_up(capture_offset + capture_size, 64);
    g_capture_code = g_overlay_base + capture_offset;
    g_menu_code = g_overlay_base + menu_offset;
    if (!user_range_valid(g_overlay_base, OVERLAY_BLOCK_SIZE) ||
        !user_range_valid(g_capture_code, capture_size) ||
        !user_range_valid(g_menu_code, menu_size) ||
        menu_offset + menu_size > OVERLAY_BLOCK_SIZE) {
        sceKernelFreePartitionMemory(g_overlay_block);
        g_overlay_block = -1;
        g_overlay_base = 0;
        return -4;
    }

    memset((void *)g_overlay_base, 0, OVERLAY_BLOCK_SIZE);
    g_overlay = (OverlayContext *)g_overlay_base;
    g_overlay->magic = OVERLAY_MAGIC;
    g_overlay->gate = OVERLAY_GATE_IDLE;
    g_overlay->intr_suspend = intr_suspend;
    g_overlay->intr_resume = intr_resume;
    g_overlay->font_create = GEB_FONT_CREATE_ADDRESS;
    g_overlay->font_clear = GEB_FONT_CLEAR_ADDRESS;
    g_overlay->font_append = GEB_FONT_APPEND_ADDRESS;
    g_overlay->font_measure = GEB_FONT_MEASURE_ADDRESS;
    g_overlay->font_draw = GEB_FONT_DRAW_ADDRESS;
    g_overlay->font_destroy = GEB_FONT_DESTROY_ADDRESS;
    g_overlay->main_scale_bits = float_bits(OVERLAY_MAIN_SCALE);
    g_overlay->bond_scale_bits = float_bits(OVERLAY_BOND_SCALE);
    g_overlay->max_width_bits = float_bits(160.0f);
    g_overlay->menu_active = 0;
    g_overlay->start_y_bits = float_bits(OVERLAY_START_Y);
    g_overlay->main_step_bits = float_bits(OVERLAY_MAIN_STEP);
    g_overlay->bond_step_bits = float_bits(OVERLAY_BOND_LINE_STEP);
    g_overlay->bottom_y_bits = float_bits(OVERLAY_BOTTOM_Y);

    memcpy((void *)g_capture_code, overlay_capture_blob_start, capture_size);
    memcpy((void *)g_menu_code, overlay_menu_blob_start, menu_size);
    patch_context_address(g_capture_code, overlay_capture_blob_start,
        overlay_capture_context_lui, overlay_capture_context_ori, g_overlay_base);
    patch_context_address(g_menu_code, overlay_menu_blob_start,
        overlay_menu_context_lui, overlay_menu_context_ori, g_overlay_base);
    patch_context_address(g_menu_code, overlay_menu_blob_start,
        overlay_menu_entry_context_lui, overlay_menu_entry_context_ori, g_overlay_base);
    publish_data(g_overlay, sizeof(*g_overlay));
    publish_code((void *)g_capture_code, capture_size);
    publish_code((void *)g_menu_code, menu_size);

    g_capture_patch = MIPS_J(g_capture_code);
    g_menu_patch = MIPS_J(g_menu_code);
    g_menu_uid = -1;
    g_menu_callback = 0;
    g_menu_text = 0;
    g_menu_text_size = 0;
    g_page_sequence = 0;
    g_generation = 0;
    g_menu_hook_installed = 0;
    g_font_probe_installed = 0;
    g_field_active = 0;
    g_field_pending = 0;
    g_once_reachable = 0;
    g_bound_menu_uid = -1;
    g_bound_menu_text = 0;
    g_menu_reload_reported = 0;
    g_publication_deferrals = 0;
    g_field_summary_pending = 0;
    g_menu_reported = 0;
    g_menu_timeout_reported = 0;
    g_installed = 1;
    reset_field_diagnostics();
    log_printf("[UI] renderer menu_code=%08X capture_code=%08X sizes=%u/%u status=PASS\n",
        g_menu_code, g_capture_code, menu_size, capture_size);
    return 0;
}

void overlay_menu_enter(SceUID uid, u32 text_addr, u32 text_size)
{
    u32 callback;

    if (!g_installed || g_overlay == 0) {
        return;
    }
    if (g_bound_menu_uid >= 0 &&
        (g_bound_menu_uid != uid || g_bound_menu_text != text_addr)) {
        if (!g_menu_reload_reported) {
            log_printf("[UI] menu_reload uid=%08X status=UNSUPPORTED observer=ACTIVE reason=unproven_wrapper_retirement\n",
                (u32)uid);
            g_menu_reload_reported = 1;
        }
        return;
    }
    if (g_menu_hook_installed && g_menu_uid == uid && g_menu_text == text_addr) {
        return;
    }
    if (g_menu_hook_installed) {
        overlay_menu_leave();
        if (g_menu_hook_installed) {
            return;
        }
    }
    if (!user_range_valid(text_addr, text_size) ||
        text_size < GEB_MENU_RENDER_EVENT3_OFFSET + 16) {
        log_printf("[UI] menu_hook uid=%08X text=%08X size=%08X status=RANGE_FAIL\n",
            (u32)uid, text_addr, text_size);
        return;
    }
    callback = text_addr + GEB_MENU_RENDER_CALLBACK_OFFSET;
    if (!menu_fingerprints_match(text_addr, callback)) {
        log_printf("[UI] menu_hook uid=%08X text=%08X callback=%08X words=%08X/%08X status=MISMATCH\n",
            (u32)uid, text_addr, callback, *(volatile u32 *)callback,
            *(volatile u32 *)(callback + 4));
        return;
    }

    g_menu_uid = uid;
    g_menu_text = text_addr;
    g_menu_text_size = text_size;
    g_menu_callback = callback;
    patch_menu_resume(callback);
    publish_code((void *)g_menu_code,
        (unsigned int)(overlay_menu_blob_end - overlay_menu_blob_start));
    g_bound_menu_uid = uid;
    g_bound_menu_text = text_addr;
    g_once_reachable = 1;
    write_code_pair(callback, g_menu_patch, MIPS_NOP);
    g_menu_hook_installed = 1;
    g_menu_start_hook_calls = g_overlay->hook_calls;
    g_menu_start_event3_calls = g_overlay->event3_calls;
    g_menu_reported = 0;
    g_menu_timeout_reported = 0;
    g_menu_poll_ticks = 0;
    log_printf("[UI] menu_hook uid=%08X text=%08X callback=%08X code=%08X status=PASS\n",
        (u32)uid, text_addr, callback, g_menu_code);
}

void overlay_menu_leave(void)
{
    int restore_status;

    if (!g_installed || g_overlay == 0 || !g_menu_hook_installed) {
        return;
    }
    overlay_field_leave();
    g_overlay->enabled = 0;
    g_overlay->cleanup_request = 1;
    publish_data((void *)&g_overlay->enabled, 0x38);
    restore_status = restore_menu_hook();
    log_printf("[UI] menu status=OFF hook=%s\n",
        restore_status == 1 ? "RESTORED" :
        (restore_status == 2 ? "GONE" : "RESTORE_FAIL"));
    if (restore_status != 0) {
        g_menu_uid = -1;
        g_menu_callback = 0;
        g_menu_text = 0;
        g_menu_text_size = 0;
    }
}

static void report_field_summary(void)
{
    u32 frames;
    u32 attempts;
    u32 draws;
    u32 append_fail;
    u32 font_fail;
    u32 draw_fail;
    if (!g_field_summary_pending || g_overlay->cleanup_request ||
        g_overlay->font_handle || !producer_try_acquire()) {
        return;
    }
    frames = g_overlay->render_frames - g_start_render_frames;
    attempts = g_overlay->draw_attempts - g_start_draw_attempts;
    draws = g_overlay->draw_count - g_start_draw_count;
    append_fail = g_overlay->append_failures - g_start_append_failures;
    font_fail = g_overlay->font_failures - g_start_font_failures;
    draw_fail = g_overlay->draw_failures - g_start_draw_failures;
    g_field_summary_pending = 0;
    producer_release();
    log_printf("[UI] field_summary generation=%u frames=%u attempts=%u draws=%u publication_deferred=%u append_fail=%u font_fail=%u draw_fail=%u status=%s\n",
        g_retired_generation, frames, attempts, draws,
        g_publication_deferrals - g_start_publication_deferrals,
        append_fail, font_fail, draw_fail,
        append_fail || font_fail || draw_fail ? "FAIL" : "PASS");
}

static void try_field_enter(void)
{
    if (!g_field_pending || !g_menu_hook_installed ||
        !hook_field_generation_matches(g_pending_generation) ||
        g_overlay->cleanup_request || g_overlay->font_handle) {
        return;
    }
    report_field_summary();
    if (g_field_summary_pending ||
        !hook_field_generation_matches(g_pending_generation) || !producer_try_acquire()) {
        return;
    }
    g_generation = g_pending_generation;
    g_field_active = 1;
    g_field_pending = 0;
    g_overlay->enabled = 0;
    g_overlay->capture_enable = 0;
    g_overlay->template_ready = 0;
    g_overlay->capture_caller = 0;
    g_overlay->capture_font = 0;
    g_overlay->menu_active = 0;
    g_overlay->layout_truncated = 0;
    g_overlay->field_generation = g_generation;
    memset((void *)g_overlay->style, 0, sizeof(g_overlay->style));
    memset((void *)g_overlay->transform, 0, sizeof(g_overlay->transform));
    memset(g_overlay->pages, 0, sizeof(g_overlay->pages));
    g_overlay->active_page = 0;
    g_page_sequence = 0;
    g_menu_active_state = 0;
    reset_field_diagnostics();
    g_overlay->capture_enable = 1;
    publish_data(g_overlay, sizeof(*g_overlay));
    producer_release();
    if (!install_font_probe()) {
        g_overlay->capture_enable = 0;
        publish_data((void *)&g_overlay->capture_enable, sizeof(g_overlay->capture_enable));
        log_printf("[UI] font_probe addr=%08X words=%08X/%08X status=MISMATCH\n",
            GEB_FONT_DRAW_ADDRESS, *(volatile u32 *)GEB_FONT_DRAW_ADDRESS,
            *(volatile u32 *)(GEB_FONT_DRAW_ADDRESS + 4));
        return;
    }
    log_printf("[UI] field generation=%u menu=READY font_template=PENDING\n", g_generation);
}

void overlay_field_enter(u32 generation)
{
    if (!g_installed || g_overlay == 0 || generation == 0) {
        return;
    }
    if (g_field_active && g_generation == generation) {
        return;
    }
    if (g_field_active || (g_field_pending && g_pending_generation != generation)) {
        overlay_field_leave();
    }
    g_pending_generation = generation;
    g_field_pending = 1;
    try_field_enter();
}

void overlay_field_leave(void)
{
    int probe_status;
    int was_active = g_field_active;
    u32 state;
    if (!g_installed || g_overlay == 0 ||
        (!g_field_active && !g_field_pending && !g_font_probe_installed)) {
        return;
    }
    g_retired_generation = was_active ? g_generation : g_pending_generation;
    g_field_active = 0;
    g_field_pending = 0;
    g_start_cleanup_ack = g_overlay->cleanup_ack;
    g_cleanup_reported = 0;
    if (was_active) {
        g_field_summary_pending = 1;
    }
    state = sceKernelCpuSuspendIntr();
    g_overlay->enabled = 0;
    g_overlay->capture_enable = 0;
    g_overlay->field_generation = 0;
    /* A USER currently inside font_create may publish a handle after this request. */
    g_overlay->cleanup_request = 1;
    __asm__ volatile("sync" ::: "memory");
    sceKernelCpuResumeIntr(state);
    publish_data((void *)&g_overlay->enabled, 0x44);
    probe_status = restore_font_probe();
    log_printf("[UI] field status=OFF generation=%u font_probe=%s font=%08X\n",
        g_retired_generation, probe_status ? "RESTORED" : "RESTORE_FAIL", g_overlay->font_handle);
}

void overlay_poll(void)
{
    u32 hook_calls;
    u32 event3_calls;
    u32 draw_count;
    unsigned int runs;

    if (!g_installed || g_overlay == 0) {
        return;
    }
    if (g_menu_hook_installed) {
        if (!menu_module_live()) {
            overlay_menu_leave();
        } else {
            ++g_menu_poll_ticks;
            hook_calls = g_overlay->hook_calls - g_menu_start_hook_calls;
            event3_calls = g_overlay->event3_calls - g_menu_start_event3_calls;
            if (!g_menu_reported && event3_calls != 0) {
                log_printf("[UI] menu_render calls=%u event3=%u status=PASS\n",
                    hook_calls, event3_calls);
                g_menu_reported = 1;
            } else if (!g_menu_reported && !g_menu_timeout_reported &&
                g_menu_poll_ticks >= OVERLAY_TIMEOUT_TICKS) {
                log_printf("[UI] menu_render calls=%u event3=%u status=TIMEOUT\n",
                    hook_calls, event3_calls);
                g_menu_timeout_reported = 1;
            }
        }
    }

    if (!g_cleanup_reported && g_overlay->cleanup_ack != g_start_cleanup_ack) {
        log_printf("[UI] cleanup ack=%u font=%08X status=PASS\n",
            g_overlay->cleanup_ack - g_start_cleanup_ack, g_overlay->font_handle);
        g_cleanup_reported = 1;
    }
    report_field_summary();
    try_field_enter();
    if (!g_field_active) {
        return;
    }
    /* Presence alone cannot identify a same-name/same-address replacement. */
    if (!hook_field_generation_matches(g_generation)) {
        overlay_field_leave();
        return;
    }

    ++g_field_poll_ticks;
    if ((g_overlay->menu_active != 0) != g_menu_active_state) {
        g_menu_active_state = g_overlay->menu_active != 0;
        log_printf("[UI] pause state=%s overlay=%s\n",
            g_menu_active_state ? "ON" : "OFF",
            g_menu_active_state ? "SUPPRESSED" : "ACTIVE");
    }
    if (!g_template_reported && g_overlay->template_ready) {
        g_overlay->capture_enable = 0;
        publish_data((void *)&g_overlay->capture_enable,
            sizeof(g_overlay->capture_enable));
        if (!restore_font_probe()) {
            g_overlay->enabled = 0;
            publish_data((void *)&g_overlay->enabled, sizeof(g_overlay->enabled));
            log_printf("[UI] font_template caller=%08X status=RESTORE_FAIL\n",
                g_overlay->capture_caller);
            g_template_reported = -1;
        } else if (!template_valid()) {
            log_printf("[UI] font_template caller=%08X font=%08X scale=%08X/%08X status=INVALID\n",
                g_overlay->capture_caller, g_overlay->capture_font,
                g_overlay->transform[0], g_overlay->transform[1]);
            g_template_reported = -1;
        } else {
            log_printf("[UI] font_template caller=%08X font=%08X scale=%08X/%08X rgba=%08X/%08X/%08X/%08X status=PASS\n",
                g_overlay->capture_caller, g_overlay->capture_font,
                g_overlay->transform[0], g_overlay->transform[1],
                g_overlay->transform[4], g_overlay->transform[5],
                g_overlay->transform[6], g_overlay->transform[7]);
            g_template_reported = 1;
        }
    } else if (!g_template_reported && !g_template_timeout_reported &&
        g_field_poll_ticks >= OVERLAY_TIMEOUT_TICKS) {
        log_printf("[UI] font_template status=TIMEOUT\n");
        g_template_timeout_reported = 1;
    }

    if (g_template_reported > 0 && g_menu_reported && !g_overlay_reported) {
        g_overlay->enabled = 1;
        publish_data((void *)&g_overlay->enabled, sizeof(g_overlay->enabled));
        log_printf("[UI] overlay x=0 y=172 width=160 height=100 runs=%d scale=0.65/0.60 state=ENABLED\n",
            OVERLAY_MAX_RUNS);
        g_overlay_reported = 1;
    }

    if (g_overlay->layout_truncated && !g_layout_limit_reported) {
        log_printf("[UI] layout bottom=268 status=TRUNCATED\n");
        g_layout_limit_reported = 1;
    } else if (!g_overlay->layout_truncated) {
        g_layout_limit_reported = 0;
    }
    runs = active_page_count();
    draw_count = g_overlay->draw_count - g_start_draw_count;
    if (!g_draw_reported && draw_count != 0) {
        log_printf("[UI] draw frames=%u attempts=%u runs=%u prep_fail=%u measure_fallback=%u append_fail=%u font_fail=%u draw_fail=%u status=PASS\n",
            g_overlay->render_frames - g_start_render_frames,
            g_overlay->draw_attempts - g_start_draw_attempts, draw_count,
            g_overlay->prep_failures - g_start_prep_failures,
            g_overlay->measure_fallbacks - g_start_measure_fallbacks,
            g_overlay->append_failures - g_start_append_failures,
            g_overlay->font_failures - g_start_font_failures,
            g_overlay->draw_failures - g_start_draw_failures);
        g_draw_reported = 1;
    } else if (!g_draw_reported && !g_draw_timeout_reported &&
        g_overlay->enabled && runs != 0) {
        ++g_draw_wait_ticks;
        if (g_draw_wait_ticks >= OVERLAY_TIMEOUT_TICKS) {
            log_printf("[UI] draw frames=%u attempts=%u runs=0 prep_fail=%u measure_fallback=%u append_fail=%u font_fail=%u draw_fail=%u status=TIMEOUT\n",
                g_overlay->render_frames - g_start_render_frames,
                g_overlay->draw_attempts - g_start_draw_attempts,
                g_overlay->prep_failures - g_start_prep_failures,
                g_overlay->measure_fallbacks - g_start_measure_fallbacks,
                g_overlay->append_failures - g_start_append_failures,
                g_overlay->font_failures - g_start_font_failures,
                g_overlay->draw_failures - g_start_draw_failures);
            g_draw_timeout_reported = 1;
        }
    }
}

OverlayPublishResult overlay_publish(const OverlayEnemy *enemies, unsigned int count)
{
    OverlayPageData *page;
    u32 page_index;
    unsigned int i;
    unsigned int run_count;
    float last_y = 0.0f;
    int first_enemy = 1;
    int truncated = 0;
    int report_truncated = 0;

    if (!g_installed || g_overlay == 0) {
        return OVERLAY_PUBLISH_DISABLED;
    }
    if (!g_field_active) {
        if (g_field_pending) {
            ++g_publication_deferrals;
            return OVERLAY_PUBLISH_DEFERRED;
        }
        return OVERLAY_PUBLISH_DISABLED;
    }
    if (!hook_field_generation_matches(g_generation)) {
        overlay_field_leave();
        return OVERLAY_PUBLISH_DEFERRED;
    }
    if (!producer_try_acquire()) {
        ++g_publication_deferrals;
        return OVERLAY_PUBLISH_DEFERRED;
    }
    page_index = (g_overlay->active_page ^ 1) & 1;
    page = &g_overlay->pages[page_index];
    memset(page, 0, sizeof(*page));
    page->sequence = ++g_page_sequence;
    for (i = 0; i < count; ++i) {
        if (!append_enemy_runs(page, &enemies[i], &last_y, &first_enemy)) {
            truncated = 1;
            report_truncated = !g_run_limit_reported;
            g_run_limit_reported = 1;
            break;
        }
    }
    if (i == count) {
        g_run_limit_reported = 0;
    }
    run_count = page->count;
    g_overlay->layout_truncated = truncated;
    publish_data((void *)&g_overlay->layout_truncated, sizeof(g_overlay->layout_truncated));
    publish_data(page, sizeof(*page));
    g_overlay->active_page = page_index;
    publish_data((void *)&g_overlay->active_page, sizeof(g_overlay->active_page));
    producer_release();
    if (report_truncated) {
        log_printf("[UI] active=%u runs=%u max_runs=%d status=TRUNCATED\n",
            count, run_count, OVERLAY_MAX_RUNS);
    }
    return OVERLAY_PUBLISH_APPLIED;
}

void overlay_remove(void)
{
    int probe_status;
    int menu_status;
    u32 cleanup_start;

    if (!g_installed) {
        return;
    }
    cleanup_start = g_overlay != 0 ? g_overlay->cleanup_ack : 0;
    if (g_overlay != 0) {
        g_overlay->enabled = 0;
        g_overlay->capture_enable = 0;
        if (g_once_reachable) {
            g_overlay->cleanup_request = 1;
        }
        g_overlay->field_generation = 0;
        g_field_pending = 0;
        publish_data((void *)&g_overlay->enabled, 0x3C);
    }
    //[overlay] Keep the menu callback live briefly so it can destroy the font object.
    sceKernelDelayThread(100000);
    if (g_overlay != 0 && g_once_reachable &&
        g_overlay->cleanup_ack == cleanup_start && g_overlay->cleanup_request) {
        log_printf("[UI] cleanup font=%08X status=TIMEOUT\n", g_overlay->font_handle);
    }
    probe_status = restore_font_probe();
    menu_status = restore_menu_hook();
    if (!probe_status || menu_status == 0) {
        log_printf("[UI] remove font_probe=%s menu_hook=%s action=retain_block\n",
            probe_status ? "RESTORED" : "RESTORE_FAIL",
            menu_status != 0 ? "RESTORED" : "RESTORE_FAIL");
        return;
    }

    if (g_overlay != 0) {
        g_overlay->magic = 0;
        publish_data((void *)&g_overlay->magic, sizeof(g_overlay->magic));
    }
    /* Neither IDLE nor a grace delay proves a wrapper has left its copied code. */
    if (g_overlay_block >= 0) {
        if (g_once_reachable) {
            log_printf("[UI] remove allocation=RETAINED bytes=%u reason=quiescence_unproven\n",
                (u32)OVERLAY_BLOCK_SIZE);
        } else {
            sceKernelFreePartitionMemory(g_overlay_block);
        }
    }
    g_overlay_block = -1;
    g_overlay = 0;
    g_overlay_base = 0;
    g_capture_code = 0;
    g_menu_code = 0;
    g_capture_patch = 0;
    g_menu_patch = 0;
    g_menu_callback = 0;
    g_menu_text = 0;
    g_menu_text_size = 0;
    g_menu_uid = -1;
    g_installed = 0;
    g_menu_hook_installed = 0;
    g_font_probe_installed = 0;
    g_field_active = 0;
}
