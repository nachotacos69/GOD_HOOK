#ifndef GEB_HOOK_H
#define GEB_HOOK_H

#include <pspkernel.h>

typedef struct GebModule {
    struct GebModule *next;
    unsigned short attribute;
    unsigned char version[2];
    char modname[27];
    char terminal;
    char mod_state;
    char unk1;
    char unk2[2];
    unsigned int unk3;
    SceUID modid;
    unsigned int unk4;
    SceUID mem_id;
    unsigned int mpid_text;
    unsigned int mpid_data;
    void *ent_top;
    unsigned int ent_size;
    void *stub_top;
    unsigned int stub_size;
    unsigned int entry_addr_;
    unsigned int unk5[4];
    unsigned int entry_addr;
    unsigned int gp_value;
    unsigned int text_addr;
    unsigned int text_size;
    unsigned int data_size;
    unsigned int bss_size;
    unsigned int nsegment;
    unsigned int segmentaddr[4];
    unsigned int segmentsize[4];
} GebModule;

typedef int (*HookModuleStartHandler)(GebModule *module);

GebModule *hook_find_module_by_name(const char *name);
GebModule *hook_find_module_by_uid(SceUID uid);
GebModule *hook_find_module_by_address(unsigned int address);
int hook_register_start_handler(HookModuleStartHandler handler,
    HookModuleStartHandler *previous);
int hook_target_module_matches(const GebModule *module);
int hook_probe_start(void);
int hook_probe_poll(void);
void hook_probe_stop(void);
int hook_enemy_install(void);
void hook_enemy_poll(int maintenance_tick);
void hook_enemy_remove(void);
void hook_record_observer_work(u32 duration_us);
int hook_field_generation_matches(u32 generation);
u32 hook_find_main_user_import(const char *library, u32 nid, u32 expected_stub);

#endif
