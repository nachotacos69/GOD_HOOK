#include <psptypes.h>
#include <string.h>

#include "instruct.h"
#include "log.h"
#include "tr2.h"

#define TR2_MAGIC 0x3272742E
#define TR2_VERSION 0x07DF0002
#define TR2_REGISTRY_HEAD 0x08A8DB60
#define TR2_COLUMN_LIMIT 128
#define TR2_ROW_LIMIT 128
#define TR2_ARRAY_LIMIT 64
#define TR2_LINK_LIMIT 512
#define TR2_PART_RETRY_LIMIT 50

#define TR2_TYPE_UINT8 0x43
#define TR2_TYPE_UINT16 0x53
#define TR2_TYPE_UINT32 0x4C
#define TR2_TYPE_STRING 0x55

typedef struct Tr2Field {
    u32 owner;
    u32 head;
    u32 size;
    int array_count;
    int row_count;
    u8 type;
} Tr2Field;

static Tr2EnemyInfo g_enemies[TR2_ENEMY_CAPACITY];
static Tr2PartInfo g_parts[TR2_PART_CAPACITY];
static Tr2ProgressInfo g_progress[TR2_PROGRESS_CAPACITY];
static Tr2QuestEnemy g_quest_enemies[TR2_QUEST_ENEMY_CAPACITY];
static int g_enemy_count;
static int g_part_count;
static int g_progress_count;
static int g_quest_phase_count;
static int g_quest_enemy_count;
static u32 g_en_base;
static u32 g_en_trig_group;
static u32 g_progress_info;
static u32 g_quest_flow;
static int g_attempted;
static int g_ready;
static int g_parts_ready;
static int g_part_retry_count;

static int range_valid(u32 address, u32 size)
{
    return size != 0 && address >= USER_RAM_START && address + size >= address &&
        address + size <= USER_RAM_END;
}

static u8 read_u8(u32 address)
{
    return *(volatile u8 *)address;
}

static u16 read_u16(u32 address)
{
    return *(volatile u16 *)address;
}

static u32 read_u32(u32 address)
{
    return *(volatile u32 *)address;
}

static int bounded_equals(u32 address, u32 limit, const char *expected)
{
    unsigned int i;
    if (!range_valid(address, limit)) {
        return 0;
    }
    for (i = 0; expected[i] != '\0'; ++i) {
        if (i >= limit || read_u8(address + i) != (u8)expected[i]) {
            return 0;
        }
    }
    return i < limit && read_u8(address + i) == 0;
}

static u16 field_name_hash(const char *name)
{
    u32 hash = 0;
    unsigned int i;
    for (i = 0; name[i] != '\0'; ++i) {
        u32 value = (u8)name[i];
        hash = value ^ (value + (hash ^ value));
    }
    return (u16)hash;
}

static int validate_table(u32 table, const char *name)
{
    u32 info_offset;
    u32 column_count;
    if (!range_valid(table, 0x40) || read_u32(table) != TR2_MAGIC ||
        read_u32(table + 4) != TR2_VERSION || !bounded_equals(table + 8, 48, name)) {
        return 0;
    }
    info_offset = read_u32(table + 0x38);
    column_count = read_u32(table + 0x3C);
    return info_offset >= 0x40 && info_offset <= 0x10000 && column_count != 0 &&
        column_count <= TR2_COLUMN_LIMIT &&
        range_valid(table + info_offset, column_count * 20 + 4);
}

static u32 table_field_size(u32 table, u32 field)
{
    u32 info_offset = read_u32(table + 0x38);
    u32 column_count = read_u32(table + 0x3C);
    u32 i;
    for (i = 0; i < column_count; ++i) {
        u32 entry = table + info_offset + i * 20;
        u32 offset = read_u32(entry + 4);
        u32 size = read_u32(entry + 12);
        if (offset >= 0x40 && table + offset >= table && table + offset == field &&
            size >= 0x88 && size <= 0x10000 && range_valid(field, size)) {
            return size;
        }
    }
    return 0;
}

static int validate_field(Tr2Field *field, u8 expected_type)
{
    u32 metadata_size;
    u32 id_table;
    u32 previous;
    int i;
    if (field->size < 0x88 || read_u8(field->head + 0x74) != expected_type ||
        (read_u8(field->head + 0x3D) & 2) == 0) {
        return 0;
    }
    field->type = read_u8(field->head + 0x74);
    field->array_count = (int)read_u16(field->head + 0x76);
    field->row_count = (int)read_u32(field->head + 0x7C);
    if (field->array_count < 1 || field->array_count > TR2_ARRAY_LIMIT ||
        field->row_count < 1 || field->row_count > TR2_ROW_LIMIT) {
        return 0;
    }
    metadata_size = 0x80 + (u32)(field->row_count + 2) * 4;
    if (metadata_size > field->size) {
        return 0;
    }
    previous = read_u32(field->head + 0x80);
    if (previous < metadata_size || previous > field->size) {
        return 0;
    }
    for (i = 1; i <= field->row_count; ++i) {
        u32 offset = read_u32(field->head + 0x80 + i * 4);
        if (offset < previous || offset > field->size) {
            return 0;
        }
        previous = offset;
    }
    id_table = read_u32(field->head + 0x80 + (field->row_count + 1) * 4);
    return range_valid(id_table, (u32)field->row_count * 4);
}

static int find_field(const char *table_name, const char *field_name,
    u8 expected_type, Tr2Field *field)
{
    u32 node;
    u32 previous;
    u16 hash;
    int links = 0;
    if (!range_valid(TR2_REGISTRY_HEAD, 8)) {
        return 0;
    }
    node = read_u32(TR2_REGISTRY_HEAD);
    previous = TR2_REGISTRY_HEAD;
    hash = field_name_hash(field_name);
    while (node != TR2_REGISTRY_HEAD && links < TR2_LINK_LIMIT) {
        u32 next;
        u32 owner;
        u32 head;
        u32 size;
        if (!range_valid(node, 24) || read_u32(node + 4) != previous) {
            return 0;
        }
        next = read_u32(node);
        if (!range_valid(next, 8)) {
            return 0;
        }
        owner = read_u32(node + 0x10);
        head = read_u32(node + 0x14);
        if (read_u16(node + 8) == hash && range_valid(head, 0x80) &&
            bounded_equals(head, 48, field_name) && validate_table(owner, table_name)) {
            size = table_field_size(owner, head);
            if (size == 0) {
                return 0;
            }
            field->owner = owner;
            field->head = head;
            field->size = size;
            return validate_field(field, expected_type);
        }
        previous = node;
        node = next;
        ++links;
    }
    if (node != TR2_REGISTRY_HEAD || read_u32(TR2_REGISTRY_HEAD + 4) != previous) {
        return 0;
    }
    return 0;
}

static int field_row_id(const Tr2Field *field, int row, int *id)
{
    u32 id_table;
    if (row < 0 || row >= field->row_count) {
        return 0;
    }
    id_table = read_u32(field->head + 0x80 + (field->row_count + 1) * 4);
    if (!range_valid(id_table + row * 4, 4)) {
        return 0;
    }
    *id = (int)read_u32(id_table + row * 4);
    return 1;
}

static int field_row_data(const Tr2Field *field, int row, u32 minimum,
    u32 *address, u32 *end)
{
    u32 offset;
    u32 next;
    if (row < 0 || row >= field->row_count) {
        return 0;
    }
    offset = read_u32(field->head + 0x80 + row * 4);
    next = read_u32(field->head + 0x80 + (row + 1) * 4);
    if (offset > next || next > field->size || minimum > next - offset ||
        !range_valid(field->head + offset, next - offset)) {
        return 0;
    }
    *address = field->head + offset;
    *end = field->head + next;
    return 1;
}

static int field_value_address(const Tr2Field *field, int row, int element,
    u32 element_size, u32 *address)
{
    u32 data;
    u32 end;
    u32 required;
    if (element < 0 || element >= field->array_count) {
        return 0;
    }
    required = (u32)(element + 1) * element_size;
    if (!field_row_data(field, row, required, &data, &end)) {
        return 0;
    }
    (void)end;
    *address = data + element * element_size;
    return 1;
}

static int field_read_u8(const Tr2Field *field, int row, int element, int *value)
{
    u32 address;
    if (!field_value_address(field, row, element, 1, &address)) {
        return 0;
    }
    *value = read_u8(address);
    return 1;
}

static int field_read_u16(const Tr2Field *field, int row, int element, int *value)
{
    u32 address;
    if (!field_value_address(field, row, element, 2, &address)) {
        return 0;
    }
    *value = read_u16(address);
    return 1;
}

static int field_read_u32(const Tr2Field *field, int row, int element, u32 *value)
{
    u32 address;
    if (!field_value_address(field, row, element, 4, &address)) {
        return 0;
    }
    *value = read_u32(address);
    return 1;
}

static int field_copy_string(const Tr2Field *field, int row, char *output,
    unsigned int capacity)
{
    u32 address;
    u32 end;
    unsigned int i;
    if (capacity == 0 || !field_row_data(field, row, 1, &address, &end)) {
        return 0;
    }
    for (i = 0; i + 1 < capacity && address + i < end; ++i) {
        u8 ch = read_u8(address + i);
        if (ch == 0) {
            output[i] = '\0';
            return i != 0;
        }
        output[i] = (char)ch;
    }
    output[0] = '\0';
    return 0;
}

static int field_copy_optional_string(const Tr2Field *field, int row,
    char *output, unsigned int capacity)
{
    u32 address;
    u32 end;
    unsigned int i;
    if (capacity == 0 || !field_row_data(field, row, 1, &address, &end)) {
        return 0;
    }
    for (i = 0; i + 1 < capacity && address + i < end; ++i) {
        u8 ch = read_u8(address + i);
        output[i] = (char)ch;
        if (ch == 0) {
            return 1;
        }
    }
    output[0] = '\0';
    return 0;
}

static int fields_share_rows(const Tr2Field *first, const Tr2Field *second)
{
    int i;
    if (first->owner != second->owner || first->row_count != second->row_count) {
        return 0;
    }
    for (i = 0; i < first->row_count; ++i) {
        int first_id;
        int second_id;
        if (!field_row_id(first, i, &first_id) || !field_row_id(second, i, &second_id) ||
            first_id != second_id) {
            return 0;
        }
    }
    return 1;
}

static int parse_en_base(void)
{
    Tr2Field internal_name;
    Tr2Field display_name;
    Tr2Field resource_name;
    Tr2Field hp;
    Tr2Field trig_start;
    Tr2Field trig_count;
    int i;
    if (!find_field("en_base", "en_base_name", TR2_TYPE_STRING, &internal_name) ||
        !find_field("en_base", "en_base_display_name", TR2_TYPE_STRING, &display_name) ||
        !find_field("en_base", "en_base_res_name", TR2_TYPE_STRING, &resource_name) ||
        !find_field("en_base", "en_base_hp", TR2_TYPE_UINT32, &hp) || hp.array_count < 4 ||
        !find_field("en_base", "en_base_trig_group_start_id", TR2_TYPE_UINT8,
            &trig_start) ||
        !find_field("en_base", "en_base_trig_group_num", TR2_TYPE_UINT8,
            &trig_count) ||
        display_name.row_count > TR2_ENEMY_CAPACITY ||
        !fields_share_rows(&display_name, &internal_name) ||
        !fields_share_rows(&display_name, &resource_name) ||
        !fields_share_rows(&display_name, &hp) ||
        !fields_share_rows(&display_name, &trig_start) ||
        !fields_share_rows(&display_name, &trig_count)) {
        return 0;
    }
    for (i = 0; i < display_name.row_count; ++i) {
        Tr2EnemyInfo *enemy = &g_enemies[i];
        int j;
        if (!field_row_id(&display_name, i, &enemy->id) ||
            !field_copy_string(&internal_name, i, enemy->internal_name,
                sizeof(enemy->internal_name)) ||
            !field_copy_string(&display_name, i, enemy->display_name,
                sizeof(enemy->display_name)) ||
            !field_copy_string(&resource_name, i, enemy->resource_name,
                sizeof(enemy->resource_name))) {
            return 0;
        }
        for (j = 0; j < 4; ++j) {
            if (!field_read_u32(&hp, i, j, &enemy->hp[j])) {
                return 0;
            }
        }
        if (!field_read_u8(&trig_start, i, 0, &enemy->trig_group_start_id) ||
            !field_read_u8(&trig_count, i, 0, &enemy->trig_group_count)) {
            return 0;
        }
    }
    g_en_base = display_name.owner;
    g_enemy_count = display_name.row_count;
    return 1;
}

static int parse_en_trig_group(void)
{
    Tr2Field display_name;
    Tr2Field parts_hp;
    Tr2Field replace_name;
    Tr2Field break_count;
    int i;

    memset(g_parts, 0, sizeof(g_parts));
    g_en_trig_group = 0;
    g_part_count = 0;
    if (!find_field("en_trig_group", "en_trig_group_display_name",
            TR2_TYPE_STRING, &display_name) ||
        !find_field("en_trig_group", "en_trig_group_parts_hp",
            TR2_TYPE_UINT16, &parts_hp) || parts_hp.array_count < 4 ||
        !find_field("en_trig_group", "en_trig_group_replace_parts_name",
            TR2_TYPE_STRING, &replace_name) ||
        !find_field("en_trig_group", "en_trig_group_break_count",
            TR2_TYPE_UINT8, &break_count) ||
        display_name.row_count > TR2_PART_CAPACITY ||
        !fields_share_rows(&display_name, &parts_hp) ||
        !fields_share_rows(&display_name, &replace_name) ||
        !fields_share_rows(&display_name, &break_count)) {
        return 0;
    }
    for (i = 0; i < display_name.row_count; ++i) {
        Tr2PartInfo *part = &g_parts[i];
        int j;
        if (!field_row_id(&display_name, i, &part->id) ||
            !field_copy_string(&display_name, i, part->display_name,
                sizeof(part->display_name)) ||
            !field_copy_optional_string(&replace_name, i, part->replace_name,
                sizeof(part->replace_name)) ||
            !field_read_u8(&break_count, i, 0, &part->break_count)) {
            return 0;
        }
        for (j = 0; j < 4; ++j) {
            int value;
            if (!field_read_u16(&parts_hp, i, j, &value)) {
                return 0;
            }
            part->hp[j] = (u16)value;
        }
    }
    g_en_trig_group = display_name.owner;
    g_part_count = display_name.row_count;
    return 1;
}

static int parse_progress_info(void)
{
    Tr2Field quest_index;
    Tr2Field enemy_id;
    Tr2Field enemy_num;
    Tr2Field enemy_flag;
    int i;
    if (!find_field("progress_info", "progress_info_quest_index", TR2_TYPE_UINT8,
            &quest_index) ||
        !find_field("progress_info", "progress_info_enemy_id", TR2_TYPE_UINT8,
            &enemy_id) ||
        !find_field("progress_info", "progress_info_enemy_num", TR2_TYPE_UINT8,
            &enemy_num) ||
        !find_field("progress_info", "progress_info_enemy_flag", TR2_TYPE_UINT8,
            &enemy_flag) || quest_index.row_count > TR2_PROGRESS_CAPACITY ||
        !fields_share_rows(&quest_index, &enemy_id) ||
        !fields_share_rows(&quest_index, &enemy_num) ||
        !fields_share_rows(&quest_index, &enemy_flag)) {
        return 0;
    }
    for (i = 0; i < quest_index.row_count; ++i) {
        if (!field_read_u8(&quest_index, i, 0, &g_progress[i].quest_index) ||
            !field_read_u8(&enemy_id, i, 0, &g_progress[i].enemy_id) ||
            !field_read_u8(&enemy_num, i, 0, &g_progress[i].enemy_num) ||
            !field_read_u8(&enemy_flag, i, 0, &g_progress[i].enemy_flag)) {
            return 0;
        }
    }
    g_progress_info = quest_index.owner;
    g_progress_count = quest_index.row_count;
    return 1;
}

static const Tr2ProgressInfo *find_progress(int quest_index, int enemy_id)
{
    int i;
    for (i = 0; i < g_progress_count; ++i) {
        if (g_progress[i].quest_index == quest_index &&
            g_progress[i].enemy_id == enemy_id) {
            return &g_progress[i];
        }
    }
    return 0;
}

static int parse_quest_flow(void)
{
    Tr2Field enemy_ids;
    Tr2Field enemy_flags;
    Tr2Field enemy_num;
    int total = 0;
    int i;
    if (!find_field("quest_flow", "quest_flow_quest_en_id_array", TR2_TYPE_UINT16,
            &enemy_ids) ||
        !find_field("quest_flow", "quest_flow_quest_en_flag_array", TR2_TYPE_UINT16,
            &enemy_flags) ||
        !find_field("quest_flow", "quest_flow_quest_en_num", TR2_TYPE_UINT8,
            &enemy_num) || enemy_num.row_count > TR2_QUEST_PHASE_CAPACITY ||
        !fields_share_rows(&enemy_num, &enemy_ids) ||
        !fields_share_rows(&enemy_num, &enemy_flags)) {
        return 0;
    }
    for (i = 0; i < enemy_num.row_count; ++i) {
        int phase_index;
        int count;
        int j;
        if (!field_row_id(&enemy_num, i, &phase_index) ||
            !field_read_u8(&enemy_num, i, 0, &count) || count < 0 ||
            count > enemy_ids.array_count || count > enemy_flags.array_count ||
            total + count > TR2_QUEST_ENEMY_CAPACITY) {
            return 0;
        }
        for (j = 0; j < count; ++j) {
            Tr2QuestEnemy *entry = &g_quest_enemies[total];
            const Tr2ProgressInfo *progress;
            int flag;
            if (!field_read_u16(&enemy_ids, i, j, &entry->enemy_id) ||
                !field_read_u16(&enemy_flags, i, j, &flag) || entry->enemy_id <= 0) {
                return 0;
            }
            entry->ordinal = total;
            entry->phase_index = phase_index;
            entry->required = flag != 0;
            entry->quest_index = -1;
            entry->target_count = 0;
            progress = find_progress(phase_index, entry->enemy_id);
            if (entry->required && progress != 0) {
                entry->quest_index = progress->quest_index;
                entry->target_count = progress->enemy_num;
            }
            ++total;
        }
    }
    if (total == 0) {
        return 0;
    }
    g_quest_flow = enemy_num.owner;
    g_quest_phase_count = enemy_num.row_count;
    g_quest_enemy_count = total;
    return 1;
}

void tr2_reset(void)
{
    memset(g_enemies, 0, sizeof(g_enemies));
    memset(g_parts, 0, sizeof(g_parts));
    memset(g_progress, 0, sizeof(g_progress));
    memset(g_quest_enemies, 0, sizeof(g_quest_enemies));
    g_enemy_count = 0;
    g_part_count = 0;
    g_progress_count = 0;
    g_quest_phase_count = 0;
    g_quest_enemy_count = 0;
    g_en_base = 0;
    g_en_trig_group = 0;
    g_progress_info = 0;
    g_quest_flow = 0;
    g_attempted = 0;
    g_ready = 0;
    g_parts_ready = 0;
    g_part_retry_count = 0;
}

int tr2_ensure_loaded(void)
{
    if (g_ready) {
        if (!g_parts_ready && g_part_retry_count < TR2_PART_RETRY_LIMIT) {
            ++g_part_retry_count;
            if (parse_en_trig_group()) {
                g_parts_ready = 1;
                log_printf("[TR2] en_trig_group=%08X parts=%d parts_status=LATE_PASS attempts=%d\n",
                    g_en_trig_group, g_part_count, g_part_retry_count);
            }
        }
        return 1;
    }
    if (g_attempted) {
        return 0;
    }
    g_attempted = 1;
    if (!parse_en_base() || !parse_progress_info() || !parse_quest_flow()) {
        log_printf("[TR2] en_base=%08X progress_info=%08X quest_flow=%08X status=FAIL\n",
            g_en_base, g_progress_info, g_quest_flow);
        return 0;
    }
    g_parts_ready = parse_en_trig_group();
    g_part_retry_count = 1;
    g_ready = 1;
    log_printf("[TR2] en_base=%08X enemies=%d en_trig_group=%08X parts=%d parts_status=%s progress_info=%08X targets=%d quest_flow=%08X phases=%d spawns=%d status=PASS\n",
        g_en_base, g_enemy_count, g_en_trig_group, g_part_count,
        g_parts_ready ? "PASS" : "UNAVAILABLE", g_progress_info, g_progress_count,
        g_quest_flow, g_quest_phase_count, g_quest_enemy_count);
    return 1;
}

int tr2_is_ready(void)
{
    return g_ready;
}

int tr2_parts_ready(void)
{
    return g_ready && g_parts_ready;
}

const Tr2EnemyInfo *tr2_find_enemy(int id)
{
    int i;
    for (i = 0; i < g_enemy_count; ++i) {
        if (g_enemies[i].id == id) {
            return &g_enemies[i];
        }
    }
    return 0;
}

const Tr2PartInfo *tr2_find_part(int id)
{
    int i;
    if (!g_parts_ready) {
        return 0;
    }
    for (i = 0; i < g_part_count; ++i) {
        if (g_parts[i].id == id) {
            return &g_parts[i];
        }
    }
    return 0;
}

const Tr2QuestEnemy *tr2_resolve_quest_enemy(u32 enemy_uid, int enemy_id,
    int *ordinal, int *expected_enemy_id)
{
    u32 counter = enemy_uid & 0x00FFFFFF;
    int index = -1;
    if (ordinal != 0) {
        *ordinal = -1;
    }
    if (expected_enemy_id != 0) {
        *expected_enemy_id = -1;
    }
    if (!g_ready || counter < 2) {
        return 0;
    }
    index = (int)(counter - 2);
    if (ordinal != 0) {
        *ordinal = index;
    }
    if (index < 0 || index >= g_quest_enemy_count) {
        return 0;
    }
    if (expected_enemy_id != 0) {
        *expected_enemy_id = g_quest_enemies[index].enemy_id;
    }
    if (g_quest_enemies[index].enemy_id != enemy_id) {
        return 0;
    }
    return &g_quest_enemies[index];
}
