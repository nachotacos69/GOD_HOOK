#include <pspkernel.h>
#include <pspsysmem.h>
#include <psputils.h>
#include <stddef.h>
#include <string.h>

#include "hook.h"
#include "instruct.h"
#include "log.h"
#include "overlay.h"
#include "tr2.h"
#include "runtime.h"

#define MODULE_ID_CAPACITY 128
#define TRACKED_MODULE_CAPACITY 32
#define G999_EXPECTED_TEXT 0x08C19500
#define G999_CALLSITE_OFFSET 0x0000BB18
#define START_HANDLER_NID 0x1C90BECB
#define ENEMY_CAPTURE_CAPACITY 32
#define ENEMY_CAPTURE_BLOCK_SIZE 0x100
#define ENEMY_CAPTURE_CODE_OFFSET 0x90
#define ENEMY_CAPTURE_CODE_WORDS 20
#define ENEMY_PART_STRIDE 0x14
#define ENEMY_PART_LIMIT 64

typedef struct EnemyPartSlot {
    int dynamic_index;
    int group_id;
    int current_hp;
    int max_hp;
    int break_count;
    int break_required;
    int broken;
    u32 static_part;
    u16 base_hp[4];
    char name[33];
    char trigger_name[33];
} EnemyPartSlot;

typedef struct ExportTarget {
    const char *name;
    const char *library;
    u32 nid;
    u32 expected;
    u32 first_word;
} ExportTarget;

typedef struct WordTarget {
    const char *name;
    u32 address;
    u32 expected;
} WordTarget;

typedef struct TrackedModule {
    SceUID uid;
    char name[28];
    int seen;
} TrackedModule;

typedef struct EnemySlot {
    u32 enemy;
    u32 enemy_class;
    u32 record;
    int last_hp;
    int max_hp;
    u32 incarnation;
    u32 generation;
    u32 enemy_uid;
    int enemy_id;
    char name[33];
    char module_name[28];
    int parts_initialized;
    unsigned int part_attempts;
    int part_count_reported;
    int part_limit_reported;
    int part_count;
    EnemyPartSlot parts[OVERLAY_MAX_PARTS];
    int active;
} EnemySlot;

#if GEB_ENABLE_LOGGING
static const ExportTarget g_exports[] = {
    { "direct_logger", "obj__dev__cons_o", 0x09584BA2, 0x0881011C, 0x27BDFFC0 },
    { "gated_logger", "obj__dev__cons_o", 0x59022A7E, 0x0881001C, 0x27BDFFC0 },
    { "goddb_get", "obj__god__god_db_o", 0x30BD9003, 0x088A86F8, 0x3C0208A9 },
    { "enemy_id", "obj__god__god_db_o", 0xD0DFCDDC, 0x088A883C, 0x3C0608A4 },
    { "enemy_builder", "obj__god__god_db_o", 0xB6780245, 0x088A97A4, 0x27BDF620 },
    { "enemy_printer", "obj__god__god_db_o", 0x8EA4EC8B, 0x088AA688, 0x27BDFFD0 },
    { "mission_printer", "obj__god__god_db_o", 0x06681964, 0x088AB428, 0x27BDFFD0 },
    { "mission_loader", "obj__god__god_db_o", 0xA8FDCD6C, 0x088AC540, 0x27BDFB60 },
    { "conflict_damage", "obj__god__god_o", 0x5C583BF8, 0x088A0AA0, 0x27BDFFB0 },
    { "conflict_shoot", "obj__god__god_o", 0xD2A611BF, 0x088A0D7C, 0x27BDFF50 },
    { "oracle_phenomenon", "obj__god__god_oracle_phenomenon_o", 0xC6997B60, 0x08918E38, 0x27BDFD70 }
};

static const WordTarget g_static_words[] = {
    { "enemy_builder_call", 0x08940704, 0x0E22A5E9 },
    { "enemy_builder_delay", 0x08940708, 0x02002021 },
    { "enemy_builder_result", 0x0894070C, 0x00402821 },
    { "enemy_builder_arg2", 0x08940710, 0x02E03021 },
    { "gated_logger_mode", 0x0881006C, 0x24040005 },
    { "direct_logger_mode", 0x0881012C, 0x24040005 }
};

static const u32 g_mission_words[] = {
    0x0E32ECD2, 0xAFB40050, 0x00402021, 0x02402821,
    0x02603021, 0x0E32ECDC, 0x00408821, 0x1600000E
};
#endif

static TrackedModule g_tracked[TRACKED_MODULE_CAPACITY];
static int g_core_reported;
static int g_list_error_reported;
static u32 g_enemy_capture_read;
static EnemySlot g_enemy_slots[ENEMY_CAPTURE_CAPACITY];
static OverlayPart g_overlay_parts[ENEMY_CAPTURE_CAPACITY][OVERLAY_MAX_PARTS];
static SceUID g_enemy_capture_block = -1;
static u32 g_enemy_capture_base;
static u32 g_enemy_capture_code;
static u32 g_enemy_patch_word;
static int g_enemy_hook_installed;
static int g_overlay_dirty;
static SceUID g_field_uid = -1;
static u32 g_field_text;
static u32 g_field_generation;
static u32 g_incarnation_counter;
static u32 g_capture_drops;
static u32 g_observer_work_max;
static int g_registry_overflow_reported;
static int g_capture_once_reachable;
static SceUID g_ppsspp_owner_ids[TRACKED_MODULE_CAPACITY];
static unsigned int g_ppsspp_owner_count;
static unsigned int g_ppsspp_owner_replace;

static void reconcile_field_generation(void);
static int field_uid_live(void);

typedef char GebModuleEntOffset[(offsetof(GebModule, ent_top) == 0x40) ? 1 : -1];
typedef char GebModuleTextOffset[(offsetof(GebModule, text_addr) == 0x6C) ? 1 : -1];
typedef char GebModuleSegmentOffset[(offsetof(GebModule, segmentaddr) == 0x80) ? 1 : -1];

static void copy_module_name(char output[28], const char input[27]);
static int module_contains(const GebModule *module, u32 address, u32 size);

static GebModule *module_from_sdk(SceModule *module)
{
    union {
        SceModule *sdk;
        GebModule *geb;
    } pointer;
    pointer.sdk = module;
    return pointer.geb;
}

GebModule *hook_find_module_by_name(const char *name)
{
    return module_from_sdk(sceKernelFindModuleByName(name));
}

GebModule *hook_find_module_by_uid(SceUID uid)
{
    return module_from_sdk(sceKernelFindModuleByUID(uid));
}

GebModule *hook_find_module_by_address(unsigned int address)
{
    SceUID ids[MODULE_ID_CAPACITY];
    int count = 0;
    unsigned int i;

    if (!geb_runtime_is_ppsspp()) {
        return module_from_sdk(sceKernelFindModuleByAddress(address));
    }
    /* PPSSPP leaves the kernel address lookup unimplemented. Recheck cached UIDs. */
    for (i = 0; i < g_ppsspp_owner_count; ++i) {
        GebModule *module = hook_find_module_by_uid(g_ppsspp_owner_ids[i]);
        if (module != 0 && module->modid == g_ppsspp_owner_ids[i] &&
            module_contains(module, address, 4)) {
            return module;
        }
    }
    if (sceKernelGetModuleIdList(ids, sizeof(ids), &count) < 0 || count < 0) {
        return 0;
    }
    if (count > MODULE_ID_CAPACITY) {
        count = MODULE_ID_CAPACITY;
    }
    for (i = 0; i < (unsigned int)count; ++i) {
        SceKernelModuleInfo info;
        unsigned int segment;
        GebModule *module;
        int contains = 0;

        memset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        if (sceKernelQueryModuleInfo(ids[i], &info) < 0 || info.nsegment <= 0 ||
            info.nsegment > 4) {
            continue;
        }
        for (segment = 0; segment < (unsigned int)info.nsegment; ++segment) {
            u32 start = (u32)info.segmentaddr[segment];
            u32 size = (u32)info.segmentsize[segment];
            if (address >= start && address - start < size && size - (address - start) >= 4) {
                contains = 1;
                break;
            }
        }
        if (!contains) {
            continue;
        }
        /* Supported native UID lookup supplies the game's import/export table pointers. */
        module = hook_find_module_by_uid(ids[i]);
        if (module == 0 || module->modid != ids[i] || module->text_addr != info.text_addr ||
            !module_contains(module, address, 4)) {
            continue;
        }
        if (g_ppsspp_owner_count < TRACKED_MODULE_CAPACITY) {
            g_ppsspp_owner_ids[g_ppsspp_owner_count++] = ids[i];
        } else {
            g_ppsspp_owner_ids[g_ppsspp_owner_replace++ % TRACKED_MODULE_CAPACITY] = ids[i];
        }
        return module;
    }
    return 0;
}

static int module_contains(const GebModule *module, u32 address, u32 size)
{
    unsigned int i;
    u32 end;

    if (module == 0 || size == 0 || address + size < address) {
        return 0;
    }
    end = address + size;
    for (i = 0; i < module->nsegment && i < 4; ++i) {
        u32 start = module->segmentaddr[i];
        u32 segment_end = start + module->segmentsize[i];
        if (segment_end >= start && address >= start && end <= segment_end) {
            return 1;
        }
    }
    return 0;
}

static int library_matches(const GebModule *module, const char *actual, const char *expected)
{
    unsigned int length = (unsigned int)strlen(expected) + 1;
    if (!module_contains(module, (u32)actual, length)) {
        return 0;
    }
    return strcmp(actual, expected) == 0;
}

static u32 find_export(const GebModule *module, const char *library, u32 nid)
{
    u8 *cursor;
    u8 *end;

    if (module == 0 || module->ent_top == 0 || module->ent_size < 16) {
        return 0;
    }
    if (!module_contains(module, (u32)module->ent_top, module->ent_size)) {
        return 0;
    }

    cursor = (u8 *)module->ent_top;
    end = cursor + module->ent_size;
    while (cursor + 16 <= end) {
        SceLibraryEntryTable *entry = (SceLibraryEntryTable *)cursor;
        unsigned int entry_size = (unsigned int)entry->len * 4;
        unsigned int total = (unsigned int)entry->stubcount + entry->vstubcount;

        if (entry_size < 16 || cursor + entry_size > end) {
            break;
        }
        if (entry->libname != 0 && entry->entrytable != 0 && total <= 1024 &&
            library_matches(module, entry->libname, library) &&
            module_contains(module, (u32)entry->entrytable, total * 8)) {
            u32 *table = (u32 *)entry->entrytable;
            unsigned int i;
            for (i = 0; i < entry->stubcount; ++i) {
                if (table[i] == nid) {
                    return table[total + i];
                }
            }
        }
        cursor += entry_size;
    }
    return 0;
}

static int user_range_valid(u32 address, u32 size)
{
    return size != 0 && (address & 3) == 0 && address >= USER_RAM_START &&
        address + size >= address && address + size <= USER_RAM_END;
}

static void publish_code(const void *address, unsigned int size)
{
    sceKernelDcacheWritebackInvalidateRange(address, size);
    sceKernelIcacheInvalidateRange(address, size);
}

static int enemy_snapshot(u32 enemy, u32 *enemy_class, u32 *record,
    int *current_hp, int *max_hp, char name[33])
{
    u32 vtable;
    u32 first_function;
    u32 cls;
    u32 rec;
    int hp;
    int maximum;
    unsigned int i;

    if (!user_range_valid(enemy, 0x218)) {
        return 0;
    }
    vtable = *(volatile u32 *)enemy;
    if (!user_range_valid(vtable, 8)) {
        return 0;
    }
    first_function = *(volatile u32 *)vtable;
    if (!user_range_valid(first_function, 4)) {
        return 0;
    }
    {
        GebModule *owner = hook_find_module_by_address(first_function);
        if (owner == 0 || !module_contains(owner, first_function, 4)) {
            return 0;
        }
    }
    cls = *(volatile u32 *)(enemy + 0x134);
    if (!user_range_valid(cls, 0x140)) {
        return 0;
    }
    rec = *(volatile u32 *)(cls + 0x0C);
    if (!user_range_valid(rec, 0xC3C) || *(volatile u8 *)(rec + 2) != 6) {
        return 0;
    }
    maximum = *(volatile int *)(cls + 0xAC);
    hp = *(volatile int *)(enemy + 0x154);
    if (maximum <= 0 || maximum > 100000000 || hp < 0 || hp > maximum * 4) {
        return 0;
    }
    for (i = 0; i < 32; ++i) {
        unsigned char ch = *(volatile unsigned char *)(rec + 0x10 + i);
        if (ch == 0) {
            break;
        }
        if (ch < 0x20 || ch >= 0x7F) {
            return 0;
        }
        name[i] = (char)ch;
    }
    if (i == 0 || i == 32) {
        return 0;
    }
    name[i] = '\0';
    *enemy_class = cls;
    *record = rec;
    *current_hp = hp;
    *max_hp = maximum;
    return 1;
}

static int enemy_part_range(u32 enemy, u32 *start, int *count)
{
    u32 begin;
    u32 end;
    u32 size;

    if (!user_range_valid(enemy, 0x144)) {
        return 0;
    }
    begin = *(volatile u32 *)(enemy + 0x13C);
    end = *(volatile u32 *)(enemy + 0x140);
    if (end < begin) {
        return 0;
    }
    size = end - begin;
    if (size % ENEMY_PART_STRIDE != 0 ||
        size / ENEMY_PART_STRIDE > ENEMY_PART_LIMIT ||
        (size != 0 && !user_range_valid(begin, size))) {
        return 0;
    }
    *start = begin;
    *count = (int)(size / ENEMY_PART_STRIDE);
    return 1;
}

static int copy_static_trigger_name(u32 static_part, char output[33])
{
    unsigned int i;
    u32 address = static_part + 0xB7;
    if (!user_range_valid(static_part, 0xD8)) {
        return 0;
    }
    for (i = 0; i < 32; ++i) {
        u8 ch = *(volatile u8 *)(address + i);
        output[i] = (char)ch;
        if (ch == 0) {
            return i >= 8 && strncmp(output, "Trigger_", 8) == 0;
        }
    }
    output[0] = '\0';
    return 0;
}

static int copy_static_part_name(u32 static_part, char output[33])
{
    unsigned int input_index;
    unsigned int output_index = 0;
    if (!user_range_valid(static_part, 0x80)) {
        return 0;
    }
    for (input_index = 0; input_index < 32; ++input_index) {
        u32 codepoint = *(volatile u32 *)(static_part + input_index * 4);
        if (codepoint == 0) {
            output[output_index] = '\0';
            return output_index != 0;
        }
        if (codepoint < 0x80) {
            if (output_index + 1 >= 33) {
                break;
            }
            output[output_index++] = (char)codepoint;
        } else if (codepoint < 0x800) {
            if (output_index + 2 >= 33) {
                break;
            }
            output[output_index++] = (char)(0xC0 | (codepoint >> 6));
            output[output_index++] = (char)(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            if (output_index + 3 >= 33) {
                break;
            }
            output[output_index++] = (char)(0xE0 | (codepoint >> 12));
            output[output_index++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
            output[output_index++] = (char)(0x80 | (codepoint & 0x3F));
        } else {
            break;
        }
    }
    output[0] = '\0';
    return 0;
}

static int read_enemy_part(const EnemySlot *slot, const EnemyPartSlot *part,
    int *current_hp, int *max_hp, int *break_count, int *broken, u32 *entry_address)
{
    u32 start;
    u32 entry;
    u32 class_part;
    int count;
    int current;
    int maximum;

    if (!enemy_part_range(slot->enemy, &start, &count) ||
        part->dynamic_index < 0 || part->dynamic_index >= count) {
        return 0;
    }
    entry = start + (u32)part->dynamic_index * ENEMY_PART_STRIDE;
    class_part = *(volatile u32 *)entry;
    if (*(volatile u32 *)(entry + 4) != slot->enemy ||
        !user_range_valid(class_part, 8)) {
        return 0;
    }
    if (*(volatile u32 *)class_part != part->static_part) {
        return 0;
    }
    current = *(volatile int *)(entry + 8);
    maximum = *(volatile int *)(class_part + 4);
    if (maximum <= 0 || maximum > 100000000 || current < 0 ||
        current > maximum * 2) {
        return 0;
    }
    *current_hp = current;
    *max_hp = maximum;
    *break_count = *(volatile u8 *)(entry + 0x0E);
    *broken = *(volatile u8 *)(entry + 0x0F) != 0;
    if (entry_address != 0) {
        *entry_address = entry;
    }
    return 1;
}

static void setup_enemy_parts(EnemySlot *slot, const Tr2EnemyInfo *enemy_info)
{
    u32 start;
    int dynamic_count;
    int pending;
    int i;

    if (slot->part_attempts >= 50) {
        slot->parts_initialized = 1;
        return;
    }
    ++slot->part_attempts;
    if (!enemy_part_range(slot->enemy, &start, &dynamic_count)) {
        return;
    }
    pending = dynamic_count == 0;
    if (!slot->part_count_reported && enemy_info != 0 &&
        enemy_info->trig_group_count > 0 && dynamic_count != enemy_info->trig_group_count) {
        log_printf("[BOND] enemy=%s ptr=%08X uid=%08X runtime=%d expected=%d status=COUNT_MISMATCH\n",
            slot->name, slot->enemy, slot->enemy_uid, dynamic_count, enemy_info->trig_group_count);
        slot->part_count_reported = 1;
    }
    for (i = 0; i < dynamic_count; ++i) {
        u32 entry = start + (u32)i * ENEMY_PART_STRIDE;
        u32 class_part = *(volatile u32 *)entry;
        u32 static_part;
        const Tr2PartInfo *info = 0;
        EnemyPartSlot candidate;
        EnemyPartSlot *part = 0;
        int j;

        if (*(volatile u32 *)(entry + 4) != slot->enemy ||
            !user_range_valid(class_part, 8)) {
            pending = 1;
            continue;
        }
        static_part = *(volatile u32 *)class_part;
        if (!user_range_valid(static_part, 0xD8)) {
            pending = 1;
            continue;
        }
        for (j = 0; j < slot->part_count; ++j) {
            if (slot->parts[j].dynamic_index == i) {
                part = &slot->parts[j];
                break;
            }
        }
        if (part != 0 && part->static_part == static_part) {
            continue; /* A partial retry must not repeat an initial summary. */
        }
        memset(&candidate, 0, sizeof(candidate));
        candidate.dynamic_index = i;
        candidate.static_part = static_part;
        if (!copy_static_trigger_name(static_part, candidate.trigger_name)) {
            continue; /* Native non-Trigger regions are not visible bonds. */
        }
        if (!copy_static_part_name(static_part, candidate.name)) {
            pending = 1;
            continue;
        }
        candidate.group_id = enemy_info != 0 && i < enemy_info->trig_group_count ?
            enemy_info->trig_group_start_id + i : -1;
        if (tr2_parts_ready() && candidate.group_id > 0) {
            info = tr2_find_part(candidate.group_id);
        }
        candidate.break_required = *(volatile u8 *)(static_part + 0xB5);
        for (j = 0; j < 4; ++j) {
            candidate.base_hp[j] = info != 0 ? info->hp[j] :
                (u16)*(volatile u32 *)(static_part + 0x84 + j * 4);
        }
        if (!read_enemy_part(slot, &candidate, &candidate.current_hp, &candidate.max_hp,
                &candidate.break_count, &candidate.broken, &entry)) {
            pending = 1;
            continue;
        }
        if (part == 0) {
            if (slot->part_count >= OVERLAY_MAX_PARTS) {
                if (!slot->part_limit_reported) {
                    log_printf("[BOND] enemy=%s ptr=%08X visible=%d status=TRUNCATED\n",
                        slot->name, slot->enemy, slot->part_count);
                    slot->part_limit_reported = 1;
                }
                break;
            }
            part = &slot->parts[slot->part_count++];
        }
        *part = candidate;
        g_overlay_dirty = 1;
        log_printf("[BOND] enemy=%s ptr=%08X uid=%08X generation=%u incarnation=%u group=%d index=%d name=%s trigger=%s hp=%d max=%d base=%u/%u/%u/%u breaks=%d/%d broken=%d event=initial entry=%08X status=PASS\n",
            slot->name, slot->enemy, slot->enemy_uid, slot->generation, slot->incarnation,
            part->group_id, i, part->name, part->trigger_name, part->current_hp, part->max_hp,
            (u32)part->base_hp[0], (u32)part->base_hp[1], (u32)part->base_hp[2],
            (u32)part->base_hp[3], part->break_count, part->break_required, part->broken, entry);
    }
    slot->parts_initialized = !pending || slot->part_attempts >= 50;
}

static void refresh_enemy_parts(EnemySlot *slot)
{
    int i;
    for (i = 0; i < slot->part_count; ++i) {
        EnemyPartSlot *part = &slot->parts[i];
        int current_hp;
        int max_hp;
        int break_count;
        int broken;
        int became_broken;
        int progressed;

        if (!read_enemy_part(slot, part, &current_hp, &max_hp, &break_count, &broken, 0)) {
            continue;
        }
        became_broken = !part->broken && broken;
        progressed = break_count > part->break_count;
        if (current_hp != part->current_hp || max_hp != part->max_hp ||
            break_count != part->break_count || broken != part->broken) {
            part->current_hp = current_hp;
            part->max_hp = max_hp;
            part->break_count = break_count;
            part->broken = broken;
            g_overlay_dirty = 1;
        }
        if (became_broken || progressed) {
            log_printf("[BOND] enemy=%s ptr=%08X uid=%08X generation=%u incarnation=%u group=%d index=%d name=%s hp=%d max=%d breaks=%d/%d broken=%d event=%s\n",
                slot->name, slot->enemy, slot->enemy_uid, slot->generation, slot->incarnation,
                part->group_id, part->dynamic_index, part->name, current_hp, max_hp,
                break_count, part->break_required, broken,
                became_broken ? "broken" : "break_progress");
        }
    }
}

static EnemySlot *find_enemy_slot(u32 enemy)
{
    unsigned int i;
    for (i = 0; i < ENEMY_CAPTURE_CAPACITY; ++i) {
        if (g_enemy_slots[i].active && g_enemy_slots[i].enemy == enemy) {
            return &g_enemy_slots[i];
        }
    }
    return 0;
}

static EnemySlot *allocate_enemy_slot(void)
{
    unsigned int i;
    for (i = 0; i < ENEMY_CAPTURE_CAPACITY; ++i) {
        if (!g_enemy_slots[i].active) {
            return &g_enemy_slots[i];
        }
    }
    return 0;
}

static OverlayPublishResult publish_enemy_overlay(void)
{
    OverlayEnemy entries[ENEMY_CAPTURE_CAPACITY];
    unsigned int count = 0;
    unsigned int i;
    unsigned int j;

    //[overlay] Dead slots stay in the diagnostic registry but never enter the visual list.
    for (i = 0; i < ENEMY_CAPTURE_CAPACITY; ++i) {
        EnemySlot *slot = &g_enemy_slots[i];
        if (slot->active && slot->last_hp > 0 && count < ENEMY_CAPTURE_CAPACITY) {
            unsigned int part_index;
            entries[count].name = slot->name;
            entries[count].current_hp = slot->last_hp;
            entries[count].max_hp = slot->max_hp;
            entries[count].enemy_uid = slot->enemy_uid;
            entries[count].parts = g_overlay_parts[count];
            entries[count].part_count = (unsigned int)slot->part_count;
            for (part_index = 0; part_index < entries[count].part_count;
                ++part_index) {
                const EnemyPartSlot *part = &slot->parts[part_index];
                g_overlay_parts[count][part_index].name = part->name;
                g_overlay_parts[count][part_index].current_hp = part->current_hp;
                g_overlay_parts[count][part_index].broken = part->broken;
            }
            ++count;
        }
    }

    //[overlay] UID ordering keeps each active enemy on a stable line as HP changes.
    for (i = 1; i < count; ++i) {
        OverlayEnemy value = entries[i];
        j = i;
        while (j > 0 && entries[j - 1].enemy_uid > value.enemy_uid) {
            entries[j] = entries[j - 1];
            --j;
        }
        entries[j] = value;
    }
    return overlay_publish(entries, count);
}

static void log_enemy_despawn(const EnemySlot *slot)
{
    log_printf("[ENEMY] %s %d/%d event=despawn ptr=%08X uid=%08X generation=%u incarnation=%u\n",
        slot->name, slot->last_hp, slot->max_hp, slot->enemy, slot->enemy_uid,
        slot->generation, slot->incarnation);
}

static void clear_enemy_slots(int write_log, int flush_queue)
{
    unsigned int i;
    for (i = 0; i < ENEMY_CAPTURE_CAPACITY; ++i) {
        EnemySlot *slot = &g_enemy_slots[i];
        if (slot->active && write_log) {
            log_enemy_despawn(slot);
        }
        memset(slot, 0, sizeof(*slot));
    }
    g_overlay_dirty = 1;
    if (flush_queue) {
        __asm__ volatile("sync" ::: "memory");
        g_enemy_capture_read = user_range_valid(g_enemy_capture_base, 4) ?
            *(volatile u32 *)g_enemy_capture_base : 0;
    }
}

static void update_enemy_hp(EnemySlot *slot, int hp, int maximum)
{
    int died = slot->last_hp > 0 && hp == 0;
    int reinitialized = slot->last_hp == 0 && hp > 0;
    if (hp != slot->last_hp || maximum != slot->max_hp) {
        g_overlay_dirty = 1;
    }
    slot->last_hp = hp;
    slot->max_hp = maximum;
    if (reinitialized) {
        slot->incarnation = ++g_incarnation_counter;
        slot->parts_initialized = 0;
        slot->part_attempts = 0;
        slot->part_count_reported = 0;
        slot->part_limit_reported = 0;
        slot->part_count = 0;
        memset(slot->parts, 0, sizeof(slot->parts));
        log_printf("[ENEMY] %s %d/%d event=reinitialize ptr=%08X uid=%08X generation=%u incarnation=%u\n",
            slot->name, hp, maximum, slot->enemy, slot->enemy_uid,
            slot->generation, slot->incarnation);
        setup_enemy_parts(slot, tr2_find_enemy(slot->enemy_id));
    } else if (died) {
        /* Capture a simultaneous last bond transition before hiding the dead row. */
        if (!slot->parts_initialized) {
            setup_enemy_parts(slot, tr2_find_enemy(slot->enemy_id));
        }
        refresh_enemy_parts(slot);
        log_printf("[ENEMY] %s %d/%d event=dead ptr=%08X uid=%08X generation=%u incarnation=%u\n",
            slot->name, hp, maximum, slot->enemy, slot->enemy_uid,
            slot->generation, slot->incarnation);
    }
}

static void capture_enemy(u32 enemy)
{
    EnemySlot *slot;
    GebModule *owner;
    const Tr2EnemyInfo *enemy_info;
    u32 cls;
    u32 rec;
    u32 vtable;
    u32 first_function;
    u32 enemy_uid;
    int hp;
    int maximum;
    int enemy_id;
    char name[33];

    if (!enemy_snapshot(enemy, &cls, &rec, &hp, &maximum, name)) {
        return;
    }
    enemy_uid = *(volatile u32 *)(enemy + 0x130);
    enemy_id = *(volatile u16 *)rec;

    slot = find_enemy_slot(enemy);
    if (slot != 0 && slot->enemy_class == cls && slot->record == rec &&
        slot->enemy_uid == enemy_uid && slot->enemy_id == enemy_id) {
        update_enemy_hp(slot, hp, maximum);
        return;
    }
    if (slot != 0) {
        log_enemy_despawn(slot);
        memset(slot, 0, sizeof(*slot));
    }

    tr2_ensure_loaded();
    /* Metadata loading can write a log and yield: revalidate borrowed game pointers. */
    if (!field_uid_live() || !enemy_snapshot(enemy, &cls, &rec, &hp, &maximum, name)) {
        return;
    }
    enemy_uid = *(volatile u32 *)(enemy + 0x130);
    enemy_id = *(volatile u16 *)rec;
    enemy_info = tr2_find_enemy(enemy_id);
    if (enemy_info != 0 && enemy_info->display_name[0] != '\0') {
        strcpy(name, enemy_info->display_name);
    }
    if (slot == 0) {
        slot = allocate_enemy_slot();
    }
    if (slot == 0) {
        if (!g_registry_overflow_reported) {
            log_printf("[ENEMY] event=overflow generation=%u ptr=%08X\n", g_field_generation, enemy);
            g_registry_overflow_reported = 1;
        }
        return;
    }
    memset(slot, 0, sizeof(*slot));
    slot->enemy = enemy;
    slot->enemy_class = cls;
    slot->record = rec;
    slot->last_hp = hp;
    slot->max_hp = maximum;
    slot->enemy_uid = enemy_uid;
    slot->generation = g_field_generation;
    slot->incarnation = ++g_incarnation_counter;
    slot->enemy_id = enemy_id;
    strcpy(slot->name, name);
    vtable = *(volatile u32 *)enemy;
    if (!user_range_valid(vtable, 8)) {
        return;
    }
    first_function = *(volatile u32 *)vtable;
    if (!user_range_valid(first_function, 4)) {
        return;
    }
    owner = hook_find_module_by_address(first_function);
    if (owner == 0 || !module_contains(owner, first_function, 4)) {
        return; /* Never admit an unknown code owner as a live enemy. */
    }
    copy_module_name(slot->module_name, owner->modname);
    slot->active = 1;
    g_overlay_dirty = 1;
    log_printf("[ENEMY] %s %d/%d event=spawn module=%s ptr=%08X uid=%08X generation=%u incarnation=%u class=%08X record=%08X\n",
        slot->name, hp, maximum, slot->module_name, enemy, enemy_uid,
        slot->generation, slot->incarnation, cls, rec);
    setup_enemy_parts(slot, enemy_info);
}

static void build_enemy_capture_code(void)
{
    volatile u32 *code = (volatile u32 *)g_enemy_capture_code;
    u32 high = g_enemy_capture_base >> 16;
    u32 low = g_enemy_capture_base & 0xFFFF;

    code[0] = GEB_ENEMY_CAPTURE_ORIGINAL_0;
    code[1] = GEB_ENEMY_CAPTURE_ORIGINAL_1;
    code[2] = MIPS_SW(MIPS_REG_T0, 0x14, MIPS_REG_SP);
    code[3] = MIPS_SW(MIPS_REG_T1, 0x18, MIPS_REG_SP);
    code[4] = MIPS_SW(MIPS_REG_T2, 0x1C, MIPS_REG_SP);
    code[5] = MIPS_LUI(MIPS_REG_T0, high);
    code[6] = MIPS_ORI(MIPS_REG_T0, MIPS_REG_T0, low);
    code[7] = MIPS_LW(MIPS_REG_T1, 0, MIPS_REG_T0);
    code[8] = MIPS_ANDI(MIPS_REG_T2, MIPS_REG_T1, ENEMY_CAPTURE_CAPACITY - 1);
    code[9] = MIPS_SLL(MIPS_REG_T2, MIPS_REG_T2, 2);
    code[10] = MIPS_ADDU(MIPS_REG_T2, MIPS_REG_T2, MIPS_REG_T0);
    code[11] = MIPS_SW(MIPS_REG_S1, 4, MIPS_REG_T2);
    code[12] = MIPS_SYNC;
    code[13] = MIPS_ADDIU(MIPS_REG_T1, MIPS_REG_T1, 1);
    code[14] = MIPS_SW(MIPS_REG_T1, 0, MIPS_REG_T0);
    code[15] = MIPS_LW(MIPS_REG_T2, 0x1C, MIPS_REG_SP);
    code[16] = MIPS_LW(MIPS_REG_T1, 0x18, MIPS_REG_SP);
    code[17] = MIPS_LW(MIPS_REG_T0, 0x14, MIPS_REG_SP);
    code[18] = MIPS_J(GEB_ENEMY_CAPTURE_RESUME);
    code[19] = MIPS_NOP;
}

static int enemy_capture_code_matches(void)
{
    volatile u32 *code = (volatile u32 *)g_enemy_capture_code;
    return code[0] == GEB_ENEMY_CAPTURE_ORIGINAL_0 &&
        code[1] == GEB_ENEMY_CAPTURE_ORIGINAL_1 &&
        code[5] == MIPS_LUI(MIPS_REG_T0, g_enemy_capture_base >> 16) &&
        code[6] == MIPS_ORI(MIPS_REG_T0, MIPS_REG_T0, g_enemy_capture_base & 0xFFFF) &&
        code[18] == MIPS_J(GEB_ENEMY_CAPTURE_RESUME) && code[19] == MIPS_NOP;
}

int hook_enemy_install(void)
{
    volatile u32 *target;

    if (g_enemy_hook_installed) {
        return 0;
    }
    if (*(volatile u32 *)GEB_ENEMY_INIT_ADDRESS != GEB_ENEMY_INIT_ORIGINAL_0 ||
        *(volatile u32 *)(GEB_ENEMY_INIT_ADDRESS + 4) != GEB_ENEMY_INIT_ORIGINAL_1 ||
        *(volatile u32 *)GEB_ENEMY_HP_LOAD_ADDRESS != GEB_ENEMY_HP_LOAD_WORD ||
        *(volatile u32 *)GEB_ENEMY_CAPTURE_ADDRESS != GEB_ENEMY_CAPTURE_ORIGINAL_0 ||
        *(volatile u32 *)(GEB_ENEMY_CAPTURE_ADDRESS + 4) != GEB_ENEMY_CAPTURE_ORIGINAL_1) {
        return -1;
    }
    g_enemy_capture_block = sceKernelAllocPartitionMemory(PSP_MEMORY_PARTITION_USER,
        "GEBHpCapture", PSP_SMEM_High, ENEMY_CAPTURE_BLOCK_SIZE, 0);
    if (g_enemy_capture_block < 0) {
        return -2;
    }
    g_enemy_capture_base = (u32)sceKernelGetBlockHeadAddr(g_enemy_capture_block);
    g_enemy_capture_code = g_enemy_capture_base + ENEMY_CAPTURE_CODE_OFFSET;
    if (!user_range_valid(g_enemy_capture_base, ENEMY_CAPTURE_BLOCK_SIZE) ||
        !user_range_valid(g_enemy_capture_code, ENEMY_CAPTURE_CODE_WORDS * 4)) {
        sceKernelFreePartitionMemory(g_enemy_capture_block);
        g_enemy_capture_block = -1;
        g_enemy_capture_base = 0;
        g_enemy_capture_code = 0;
        return -3;
    }
    memset((void *)g_enemy_capture_base, 0, ENEMY_CAPTURE_BLOCK_SIZE);
    build_enemy_capture_code();
    __asm__ volatile("sync" ::: "memory");
    if (!enemy_capture_code_matches()) {
        log_printf("[HOOK] enemy_capture code=%08X ring=%08X status=BYTE_CHECK_FAIL\n",
            g_enemy_capture_code, g_enemy_capture_base);
        sceKernelFreePartitionMemory(g_enemy_capture_block);
        g_enemy_capture_block = -1;
        g_enemy_capture_base = 0;
        g_enemy_capture_code = 0;
        return -4;
    }
    publish_code((void *)g_enemy_capture_code, ENEMY_CAPTURE_CODE_WORDS * 4);
    tr2_reset();
    g_enemy_capture_read = 0;
    clear_enemy_slots(0, 1);
    g_enemy_patch_word = MIPS_J(g_enemy_capture_code);
    target = (volatile u32 *)GEB_ENEMY_CAPTURE_ADDRESS;
    {
        u32 state = sceKernelCpuSuspendIntr();
        g_capture_once_reachable = 1;
        target[0] = g_enemy_patch_word;
        target[1] = MIPS_NOP;
        publish_code((void *)GEB_ENEMY_CAPTURE_ADDRESS, 8);
        sceKernelCpuResumeIntr(state);
    }
    g_enemy_hook_installed = 1;
    log_printf("[HOOK] enemy_capture addr=%08X code=%08X ring=%08X status=PASS\n",
        GEB_ENEMY_CAPTURE_ADDRESS, g_enemy_capture_code, g_enemy_capture_base);
    return 0;
}

void hook_enemy_poll(int maintenance_tick)
{
    u32 write;
    unsigned int i;
    unsigned int remaining = ENEMY_CAPTURE_CAPACITY;

    if (!g_enemy_hook_installed) {
        return;
    }
    reconcile_field_generation();
    if (g_field_uid < 0) {
        return;
    }
    if (maintenance_tick && tr2_is_ready() && !tr2_parts_ready()) {
        tr2_ensure_loaded();
    }
    __asm__ volatile("sync" ::: "memory");
    write = *(volatile u32 *)g_enemy_capture_base;
    while (remaining != 0 && g_enemy_capture_read != write) {
        u32 latest;
        u32 slot_address;
        u32 enemy;
        if (!field_uid_live()) {
            reconcile_field_generation();
            return;
        }
        __asm__ volatile("sync" ::: "memory");
        latest = *(volatile u32 *)g_enemy_capture_base;
        if (latest - g_enemy_capture_read > ENEMY_CAPTURE_CAPACITY) {
            g_capture_drops += latest - g_enemy_capture_read - ENEMY_CAPTURE_CAPACITY;
            g_enemy_capture_read = latest - ENEMY_CAPTURE_CAPACITY;
            write = latest;
        }
        slot_address = g_enemy_capture_base + 4 +
            ((g_enemy_capture_read & (ENEMY_CAPTURE_CAPACITY - 1)) * 4);
        enemy = *(volatile u32 *)slot_address;
        ++g_enemy_capture_read;
        --remaining;
        capture_enemy(enemy);
    }

    for (i = 0; i < ENEMY_CAPTURE_CAPACITY; ++i) {
        EnemySlot *slot = &g_enemy_slots[i];
        u32 cls;
        u32 rec;
        int hp;
        int maximum;
        char name[33];
        if (!slot->active) {
            continue;
        }
        if (!field_uid_live()) {
            reconcile_field_generation();
            return;
        }
        if (!enemy_snapshot(slot->enemy, &cls, &rec, &hp, &maximum, name)) {
            log_enemy_despawn(slot);
            memset(slot, 0, sizeof(*slot));
            g_overlay_dirty = 1;
            continue;
        }
        if (cls != slot->enemy_class || rec != slot->record ||
            *(volatile u32 *)(slot->enemy + 0x130) != slot->enemy_uid ||
            *(volatile u16 *)rec != slot->enemy_id) {
            log_enemy_despawn(slot);
            memset(slot, 0, sizeof(*slot));
            g_overlay_dirty = 1;
            /* A poll observes reuse; only the initializer ring may admit a new actor. */
            continue;
        }
        update_enemy_hp(slot, hp, maximum);
        if (hp > 0) {
            if (!slot->parts_initialized && maintenance_tick) {
                tr2_ensure_loaded();
                setup_enemy_parts(slot, tr2_find_enemy(slot->enemy_id));
            }
            refresh_enemy_parts(slot);
        }
    }
    if (g_overlay_dirty || maintenance_tick) {
        g_overlay_dirty = publish_enemy_overlay() == OVERLAY_PUBLISH_DEFERRED;
    }
}

void hook_enemy_remove(void)
{
    int restored = 0;
    if (g_enemy_hook_installed) {
        u32 state = sceKernelCpuSuspendIntr();
        if (*(volatile u32 *)GEB_ENEMY_CAPTURE_ADDRESS == g_enemy_patch_word &&
            *(volatile u32 *)(GEB_ENEMY_CAPTURE_ADDRESS + 4) == MIPS_NOP) {
            *(volatile u32 *)GEB_ENEMY_CAPTURE_ADDRESS = GEB_ENEMY_CAPTURE_ORIGINAL_0;
            *(volatile u32 *)(GEB_ENEMY_CAPTURE_ADDRESS + 4) = GEB_ENEMY_CAPTURE_ORIGINAL_1;
            publish_code((void *)GEB_ENEMY_CAPTURE_ADDRESS, 8);
            restored = 1;
        }
        sceKernelCpuResumeIntr(state);
        g_enemy_hook_installed = 0;
    }
    clear_enemy_slots(log_is_open(), 1);
    tr2_reset();
    if (g_enemy_capture_block >= 0) {
        if (!g_capture_once_reachable) {
            sceKernelFreePartitionMemory(g_enemy_capture_block);
        } else {
            log_printf("[HOOK] remove enemy_patch=%s allocation=RETAINED bytes=%u reason=quiescence_unproven\n",
                restored ? "RESTORED" : "NOT_OWNED", (u32)ENEMY_CAPTURE_BLOCK_SIZE);
        }
        g_enemy_capture_block = -1;
    }
    g_enemy_capture_base = 0;
    g_enemy_capture_code = 0;
    g_enemy_patch_word = 0;
}

int hook_target_module_matches(const GebModule *module)
{
    if (module == 0 || strcmp(module->modname, "main") != 0 ||
        !module_contains(module, 0x088A97A4, 4) ||
        !module_contains(module, 0x088AA688, 4) ||
        !module_contains(module, 0x088AB428, 4) ||
        !module_contains(module, 0x08940704, 4)) {
        return 0;
    }
    if (*(volatile u32 *)0x088A97A4 != 0x27BDF620 ||
        *(volatile u32 *)0x088AA688 != 0x27BDFFD0 ||
        *(volatile u32 *)0x088AB428 != 0x27BDFFD0 ||
        *(volatile u32 *)0x08940704 != 0x0E22A5E9) {
        return 0;
    }
    return find_export(module, "obj__god__god_db_o", 0x8EA4EC8B) == 0x088AA688 &&
        find_export(module, "obj__god__god_db_o", 0xA8FDCD6C) == 0x088AC540;
}

int hook_register_start_handler(HookModuleStartHandler handler,
    HookModuleStartHandler *previous)
{
    typedef HookModuleStartHandler (*SetStartHandler)(HookModuleStartHandler handler);
    GebModule *system_control;
    SetStartHandler set_handler;
    u32 address;

    if (handler == 0 || previous == 0) {
        return -1;
    }
    system_control = hook_find_module_by_name("SystemControl");
    if (system_control == 0) {
        return -2;
    }
    address = find_export(system_control, "SystemCtrlForKernel", START_HANDLER_NID);
    if (address == 0) {
        return -3;
    }
    set_handler = (SetStartHandler)address;
    *previous = set_handler(handler);
    return 0;
}

static u32 find_import_stub(const GebModule *module, const char *library, u32 nid)
{
    u8 *cursor;
    u8 *end;

    if (module == 0 || module->stub_top == 0 || module->stub_size < 20) {
        return 0;
    }
    if (!module_contains(module, (u32)module->stub_top, module->stub_size)) {
        return 0;
    }

    cursor = (u8 *)module->stub_top;
    end = cursor + module->stub_size;
    while (cursor + 20 <= end) {
        SceLibraryStubTable *entry = (SceLibraryStubTable *)cursor;
        unsigned int entry_size = (unsigned int)entry->len * 4;
        unsigned int count = entry->stubcount;

        if (entry_size < 20 || cursor + entry_size > end) {
            break;
        }
        if (entry->libname != 0 && entry->nidtable != 0 && entry->stubtable != 0 &&
            count <= 1024 && library_matches(module, entry->libname, library) &&
            module_contains(module, (u32)entry->nidtable, count * 4) &&
            module_contains(module, (u32)entry->stubtable, count * 8)) {
            unsigned int i;
            for (i = 0; i < count; ++i) {
                if (entry->nidtable[i] == nid) {
                    return (u32)entry->stubtable + i * 8;
                }
            }
        }
        cursor += entry_size;
    }
    return 0;
}

static u32 decode_jump_target(u32 address, u32 instruction)
{
    u32 opcode = instruction >> 26;
    if (opcode != 2 && opcode != 3) {
        return 0;
    }
    return ((address + 4) & 0xF0000000) | ((instruction & 0x03FFFFFF) << 2);
}

u32 hook_find_main_user_import(const char *library, u32 nid, u32 expected_stub)
{
    GebModule *module = hook_find_module_by_name("main");
    u32 stub;
    u32 first;
    u32 second;
    u32 target;
    if (!hook_target_module_matches(module)) {
        return 0;
    }
    stub = find_import_stub(module, library, nid);
    if ((stub & 3) != 0 || !module_contains(module, stub, 8)) {
        return 0;
    }
    first = *(volatile u32 *)stub;
    second = *(volatile u32 *)(stub + 4);
    target = decode_jump_target(stub, first);
    /* Loader-bound syscall or user-code jump, never an unresolved jr ra / nop. */
    if (!((first == 0x03E00008 && (second & 0xFC00003F) == 0x0000000C) ||
          ((first >> 26) == 2 && second == MIPS_NOP && user_range_valid(target, 4)))) {
        return 0;
    }
    log_printf("[UI] user_import library=%s nid=%08X stub=%08X expected=%08X words=%08X/%08X status=RESOLVED\n",
        library, nid, stub, expected_stub, first, second);
    return stub;
}

static void copy_module_name(char output[28], const char input[27])
{
    unsigned int i;
    for (i = 0; i < 27 && input[i] != '\0'; ++i) {
        output[i] = input[i];
    }
    output[i] = '\0';
}

#if GEB_ENABLE_LOGGING
static void report_module(const char *prefix, const GebModule *module)
{
    char name[28];
    copy_module_name(name, module->modname);
    log_printf("[%s] name=%s uid=%08X text=%08X size=%08X ent=%08X/%08X stub=%08X/%08X\n",
        prefix, name, (u32)module->modid, module->text_addr, module->text_size,
        (u32)module->ent_top, module->ent_size, (u32)module->stub_top, module->stub_size);
}

static void report_word(const WordTarget *target)
{
    GebModule *owner = hook_find_module_by_address(target->address);
    if (owner == 0 || !module_contains(owner, target->address, 4)) {
        log_printf("[WORD] %s addr=%08X status=UNMAPPED\n", target->name, target->address);
        return;
    }
    {
        u32 actual = *(volatile u32 *)target->address;
        log_printf("[WORD] %s addr=%08X actual=%08X expected=%08X status=%s\n",
            target->name, target->address, actual, target->expected,
            actual == target->expected ? "PASS" : "MISMATCH");
    }
}

static void report_core(const GebModule *module)
{
    unsigned int i;
    unsigned int passed = 0;

    report_module("CORE", module);
    for (i = 0; i < sizeof(g_exports) / sizeof(g_exports[0]); ++i) {
        const ExportTarget *target = &g_exports[i];
        u32 resolved = find_export(module, target->library, target->nid);
        const char *status = "MISSING";
        u32 first_word = 0;

        if (resolved != 0) {
            GebModule *owner = hook_find_module_by_address(resolved);
            if (owner != 0 && module_contains(owner, resolved, 4)) {
                first_word = *(volatile u32 *)resolved;
                if (resolved == target->expected && first_word == target->first_word) {
                    status = "PASS";
                    ++passed;
                } else {
                    status = "MISMATCH";
                }
            } else {
                status = "UNMAPPED";
            }
        }
        log_printf("[NID] %s lib=%s nid=%08X resolved=%08X expected=%08X word=%08X status=%s\n",
            target->name, target->library, target->nid, resolved, target->expected,
            first_word, status);
    }

    for (i = 0; i < sizeof(g_static_words) / sizeof(g_static_words[0]); ++i) {
        report_word(&g_static_words[i]);
    }

    if (hook_find_module_by_address(0x08A7CD44) != 0) {
        log_printf("[DATA] gpio_effective=%08X gpio_xor=%08X\n",
            *(volatile u32 *)0x08A7CD44, *(volatile u32 *)0x08A7CD48);
    }
    log_printf("[CORE-SUMMARY] exports=%u/%u\n", passed,
        (unsigned int)(sizeof(g_exports) / sizeof(g_exports[0])));
}

static void report_import(const GebModule *module, const char *name, u32 nid,
    u32 expected_stub, u32 expected_target)
{
    u32 stub = find_import_stub(module, "obj__god__god_db_o", nid);
    u32 instruction = 0;
    u32 target = 0;
    const char *status = "MISSING";

    if (stub != 0 && module_contains(module, stub, 8)) {
        instruction = *(volatile u32 *)stub;
        target = decode_jump_target(stub, instruction);
        status = (stub == expected_stub && target == expected_target) ? "PASS" : "MISMATCH";
    }
    log_printf("[IMPORT] %s nid=%08X stub=%08X expected_stub=%08X insn=%08X target=%08X expected_target=%08X status=%s\n",
        name, nid, stub, expected_stub, instruction, target, expected_target, status);
}

static void inspect_base_module(const GebModule *module)
{
    unsigned int i;
    u32 callsite = module->text_addr + G999_CALLSITE_OFFSET;

    log_printf("[G999] text=%08X expected_text=%08X callsite=%08X expected_callsite=08C25018\n",
        module->text_addr, G999_EXPECTED_TEXT, callsite);
    report_import(module, "goddb_get", 0x30BD9003, 0x08CBB348, 0x088A86F8);
    report_import(module, "mission_loader", 0xA8FDCD6C, 0x08CBB370, 0x088AC540);

    for (i = 0; i < sizeof(g_mission_words) / sizeof(g_mission_words[0]); ++i) {
        u32 address = callsite + i * 4;
        if (!module_contains(module, address, 4)) {
            log_printf("[MISSION-WORD] addr=%08X status=UNMAPPED\n", address);
            continue;
        }
        {
            u32 actual = *(volatile u32 *)address;
            log_printf("[MISSION-WORD] addr=%08X actual=%08X expected=%08X status=%s\n",
                address, actual, g_mission_words[i],
                actual == g_mission_words[i] ? "PASS" : "MISMATCH");
        }
    }
}

#else
static void report_module(const char *prefix, const GebModule *module)
{
    (void)prefix;
    (void)module;
}
static void report_core(const GebModule *module) { (void)module; }
static void inspect_base_module(const GebModule *module) { (void)module; }
#endif

static int relevant_module_name(const char *name)
{
    return strcmp(name, "main") == 0 || strcmp(name, "system") == 0 ||
        strncmp(name, "god_", 4) == 0 || strncmp(name, "creature_", 9) == 0;
}

static TrackedModule *find_tracked(SceUID uid)
{
    unsigned int i;
    for (i = 0; i < TRACKED_MODULE_CAPACITY; ++i) {
        if (g_tracked[i].uid == uid) {
            return &g_tracked[i];
        }
    }
    return 0;
}

static TrackedModule *add_tracked(SceUID uid, const char *name)
{
    unsigned int i;
    for (i = 0; i < TRACKED_MODULE_CAPACITY; ++i) {
        if (g_tracked[i].uid < 0) {
            unsigned int j;
            g_tracked[i].uid = uid;
            for (j = 0; j + 1 < sizeof(g_tracked[i].name) && name[j] != '\0'; ++j) {
                g_tracked[i].name[j] = name[j];
            }
            g_tracked[i].name[j] = '\0';
            return &g_tracked[i];
        }
    }
    return 0;
}

static int field_uid_live(void)
{
    GebModule *module = g_field_uid >= 0 ? hook_find_module_by_uid(g_field_uid) : 0;
    return module != 0 && module->modid == g_field_uid &&
        module->text_addr == g_field_text && strcmp(module->modname, "god_g000_Field") == 0;
}

int hook_field_generation_matches(u32 generation)
{
    return generation != 0 && generation == g_field_generation && field_uid_live();
}

static u32 capture_watermark(void)
{
    __asm__ volatile("sync" ::: "memory");
    return user_range_valid(g_enemy_capture_base, 4) ?
        *(volatile u32 *)g_enemy_capture_base : 0;
}

static void report_field_revision(const GebModule *module)
{
#if GEB_ENABLE_LOGGING
    u32 ent = (u32)module->ent_top - module->text_addr;
    u32 stub = (u32)module->stub_top - module->text_addr;
    u32 export = find_export(module, "god_g000_Field", 0x0103E31E);
    u32 a = module->text_addr + 0x4178C;
    u32 b = module->text_addr + 0x41FF8;
    u32 first = user_range_valid(a, 4) && module_contains(module, a, 4) ? *(volatile u32 *)a : 0;
    u32 second = user_range_valid(b, 4) && module_contains(module, b, 4) ? *(volatile u32 *)b : 0;
    const char *revision = "unknown";
    if (export == module->text_addr + 0x3C74C) {
        if (module->text_size == 0x4FE24 && ent == 0x4E0B4 && stub == 0x4E0DC &&
            first == 0x8E440AC0 && second == 0x8E038D20) {
            revision = "base-observed";
        } else if (module->text_size == 0x546D8 && ent == 0x4E054 && stub == 0x4E07C &&
            first == 0x8E440AA0 && second == 0x8E038D00) {
            revision = "plus-dlc-observed";
        }
    }
    log_printf("[FIELD] generation=%u uid=%08X revision=%s text=%08X size=%08X ent_offset=%08X stub_offset=%08X export=%08X signatures=%08X/%08X policy=common_guarded_hooks\n",
        g_field_generation, (u32)module->modid, revision, module->text_addr,
        module->text_size, ent, stub, export, first, second);
#else
    (void)module;
#endif
}

static void reconcile_field_generation(void)
{
    /* Sample before lookup/logging so a later new spawn is never flushed accidentally. */
    u32 watermark = capture_watermark();
    GebModule *module = hook_find_module_by_name("god_g000_Field");
    if (module != 0 && module->modid == g_field_uid && module->text_addr == g_field_text) {
        overlay_field_enter(g_field_generation); /* Also retries deferred menu/field activation. */
        return;
    }
    if (g_field_uid >= 0) {
        SceUID retired_uid = g_field_uid;
        g_field_uid = -1;
        overlay_field_leave();
        if (module == 0) {
            g_enemy_capture_read = watermark;
        }
        clear_enemy_slots(log_is_open(), 0);
        log_printf("[OBS] generation=%u uid=%08X capture_dropped=%u observer_work_max_us=%u event=field_retired\n",
            g_field_generation, (u32)retired_uid, g_capture_drops, g_observer_work_max);
        tr2_reset();
        /* The first lookup's module pointer may be stale after synchronous event writes. */
        watermark = capture_watermark();
        module = hook_find_module_by_name("god_g000_Field");
    }
    if (module == 0) {
        g_enemy_capture_read = watermark;
        return;
    }
    if (!user_range_valid(module->text_addr, 4)) {
        return;
    }
    g_field_uid = module->modid;
    g_field_text = module->text_addr;
    ++g_field_generation;
    g_incarnation_counter = 0;
    g_capture_drops = 0;
    g_observer_work_max = 0;
    g_registry_overflow_reported = 0;
    report_field_revision(module);
    overlay_field_enter(g_field_generation);
}

void hook_record_observer_work(u32 duration_us)
{
    if (g_field_uid >= 0 && duration_us > g_observer_work_max) {
        g_observer_work_max = duration_us;
    }
}

static void track_modules(void)
{
    SceUID ids[MODULE_ID_CAPACITY];
    int count = 0;
    int result;
    int pass;
    int i;
    result = sceKernelGetModuleIdList(ids, sizeof(ids), &count);
    if (result < 0 || count < 0) {
        if (!g_list_error_reported) {
            log_printf("[MODULE-LIST] error=%08X\n", (u32)result);
            g_list_error_reported = 1;
        }
        return;
    }
    g_list_error_reported = 0;
    if (count > MODULE_ID_CAPACITY) {
        count = MODULE_ID_CAPACITY;
    }
    /* Retire before entry; a truncated enumeration must not retire a live UID. */
    for (i = 0; i < TRACKED_MODULE_CAPACITY; ++i) {
        GebModule *module;
        if (g_tracked[i].uid < 0) {
            continue;
        }
        module = hook_find_module_by_uid(g_tracked[i].uid);
        g_tracked[i].seen = module != 0 &&
            strcmp(module->modname, g_tracked[i].name) == 0;
        if (!g_tracked[i].seen) {
            log_printf("[MODULE-] name=%s uid=%08X\n", g_tracked[i].name, (u32)g_tracked[i].uid);
            if (strcmp(g_tracked[i].name, "god_menu") == 0) {
                overlay_menu_leave();
            }
            g_tracked[i].uid = -1;
            g_tracked[i].name[0] = '\0';
        }
    }
    reconcile_field_generation();
    for (pass = 0; pass < 2; ++pass) {
        for (i = 0; i < count; ++i) {
            GebModule *module = hook_find_module_by_uid(ids[i]);
            char name[28];
            TrackedModule *tracked;
            int is_menu;
            if (module == 0) {
                continue;
            }
            copy_module_name(name, module->modname);
            is_menu = strcmp(name, "god_menu") == 0;
            if (!relevant_module_name(name) || is_menu != (pass == 0)) {
                continue;
            }
            tracked = find_tracked(ids[i]);
            if (tracked == 0) {
                tracked = add_tracked(ids[i], name);
                if (tracked != 0) {
                    report_module("MODULE+", module);
                    if (is_menu) {
                        overlay_menu_enter(module->modid, module->text_addr, module->text_size);
                    } else if (strcmp(name, "god_g999_Base") == 0) {
                        inspect_base_module(module);
                    }
                }
            }
            if (tracked != 0) {
                tracked->seen = 1;
            }
        }
    }
    reconcile_field_generation();
}

int hook_probe_start(void)
{
    unsigned int i;
    g_core_reported = 0;
    g_list_error_reported = 0;
    g_field_uid = -1;
    g_field_text = 0;
    g_field_generation = 0;
    g_incarnation_counter = 0;
    g_capture_drops = 0;
    g_observer_work_max = 0;
    g_registry_overflow_reported = 0;
    g_ppsspp_owner_count = 0;
    g_ppsspp_owner_replace = 0;
    for (i = 0; i < TRACKED_MODULE_CAPACITY; ++i) {
        g_tracked[i].uid = -1;
        g_tracked[i].name[0] = '\0';
        g_tracked[i].seen = 0;
    }
    log_printf("[HANDLER] target=main mode=%s status=PASS\n",
        geb_runtime_is_ppsspp() ? "POLLING" : "ARK_HANDLER");
    log_printf("[PROBE] passive preflight started\n");
    return 0;
}

int hook_probe_poll(void)
{
    if (!g_core_reported) {
        GebModule *module = hook_find_module_by_name("main");
        if (!hook_target_module_matches(module)) {
            module = hook_find_module_by_address(0x088AA688);
        }
        if (hook_target_module_matches(module)) {
            log_printf("[TARGET] module=main status=PASS\n");
            report_core(module);
            g_core_reported = 1;
        }
    }
    if (g_core_reported) {
        track_modules();
    }
    return g_core_reported;
}

void hook_probe_stop(void)
{
    log_printf("[PROBE] passive preflight stopped\n");
}
