#ifndef GEB_OVERLAY_H
#define GEB_OVERLAY_H

#include <psptypes.h>

#define OVERLAY_MAX_PARTS 16
#define OVERLAY_MAX_RUNS 32

typedef struct OverlayPart {
    const char *name;
    int current_hp;
    int broken;
} OverlayPart;

typedef struct OverlayEnemy {
    const char *name;
    int current_hp;
    int max_hp;
    u32 enemy_uid;
    const OverlayPart *parts;
    unsigned int part_count;
} OverlayEnemy;

//[overlay] Allocates the shared user renderer without touching game code yet.
int overlay_install(void);
void overlay_remove(void);

//[overlay] Tracks the persistent menu task separately from field visibility.
void overlay_poll(void);
void overlay_menu_enter(SceUID uid, u32 text_addr, u32 text_size);
void overlay_menu_leave(void);
void overlay_field_enter(u32 generation);
void overlay_field_leave(void);

typedef enum OverlayPublishResult {
    OVERLAY_PUBLISH_DISABLED = 0,
    OVERLAY_PUBLISH_APPLIED = 1,
    OVERLAY_PUBLISH_DEFERRED = 2
} OverlayPublishResult;

// Busy publication leaves the latest observer snapshot dirty for a later tick.
OverlayPublishResult overlay_publish(const OverlayEnemy *enemies, unsigned int count);

#endif
