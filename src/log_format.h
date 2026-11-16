#ifndef GEB_LOG_FORMAT_H
#define GEB_LOG_FORMAT_H

#include <stdarg.h>

/* Bounded debug formats: %% / %s / %c / %d / %i / %u / %x / %X, optional width/zero pad. */
int geb_log_vformat(char *output, unsigned int capacity, const char *format, va_list args);

#endif
