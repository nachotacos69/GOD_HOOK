#ifndef GEB_TR2_H
#define GEB_TR2_H

#include <psptypes.h>

#define TR2_ENEMY_CAPACITY 64
#define TR2_PROGRESS_CAPACITY 32
#define TR2_QUEST_PHASE_CAPACITY 64
#define TR2_QUEST_ENEMY_CAPACITY 256
#define TR2_PART_CAPACITY 128

typedef struct Tr2EnemyInfo {
    int id;
    char internal_name[33];
    char display_name[33];
    char resource_name[65];
    u32 hp[4];
    int trig_group_start_id;
    int trig_group_count;
} Tr2EnemyInfo;

typedef struct Tr2PartInfo {
    int id;
    char display_name[33];
    char replace_name[33];
    u16 hp[4];
    int break_count;
} Tr2PartInfo;

typedef struct Tr2ProgressInfo {
    int quest_index;
    int enemy_id;
    int enemy_num;
    int enemy_flag;
} Tr2ProgressInfo;

typedef struct Tr2QuestEnemy {
    int ordinal;
    int phase_index;
    int enemy_id;
    int required;
    int quest_index;
    int target_count;
} Tr2QuestEnemy;

void tr2_reset(void);
int tr2_ensure_loaded(void);
int tr2_is_ready(void);
int tr2_parts_ready(void);
const Tr2EnemyInfo *tr2_find_enemy(int id);
const Tr2PartInfo *tr2_find_part(int id);
const Tr2QuestEnemy *tr2_resolve_quest_enemy(u32 enemy_uid, int enemy_id,
    int *ordinal, int *expected_enemy_id);

#endif
