#ifndef GEB_RUNTIME_H
#define GEB_RUNTIME_H

/* Probe once at module_start; an unsupported probe selects the hardware path. */
int geb_runtime_detect(void);
int geb_runtime_is_ppsspp(void);

#endif
