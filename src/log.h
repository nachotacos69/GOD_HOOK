#ifndef GEB_LOG_H
#define GEB_LOG_H

#ifndef GEB_ENABLE_LOGGING
#define GEB_ENABLE_LOGGING 0
#endif

int log_open(void);
void log_close(void);
int log_is_open(void);

#if GEB_ENABLE_LOGGING
int log_printf(const char *format, ...);
#else
/* Unevaluated arguments remain type-checked/used, with no calls or side effects. */
int geb_log_disabled_typecheck(const char *format, ...);
#define log_printf(...) ((void)sizeof(geb_log_disabled_typecheck(__VA_ARGS__)))
#endif

#endif
