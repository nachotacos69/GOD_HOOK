#include "log.h"

#if GEB_ENABLE_LOGGING
#include "log_format.h"

static void append_char(char *output, unsigned int capacity, unsigned int *length, char ch)
{
    if (*length + 1 < capacity) {
        output[(*length)++] = ch;
    }
}

int geb_log_vformat(char *output, unsigned int capacity, const char *format, va_list args)
{
    unsigned int length = 0;

    if (output == 0 || capacity == 0 || format == 0) {
        return -1;
    }
    while (*format != '\0') {
        unsigned int width = 0;
        unsigned int count = 0;
        unsigned int value;
        unsigned int base;
        int negative = 0;
        char padding = ' ';
        char specifier;
        char digits[32];
        const char *alphabet;

        if (*format != '%') {
            append_char(output, capacity, &length, *format++);
            continue;
        }
        ++format;
        if (*format == '%') {
            append_char(output, capacity, &length, *format++);
            continue;
        }
        if (*format == '0') {
            padding = '0';
            ++format;
        }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (unsigned int)(*format++ - '0');
            if (width > 64) {
                output[length] = '\0';
                return -1;
            }
        }
        specifier = *format;
        if (specifier == '\0') {
            output[length] = '\0';
            return -1;
        }
        ++format;
        if (specifier == 's') {
            const char *text = va_arg(args, const char *);
            const char *cursor = text != 0 ? text : "(null)";
            while (*cursor != '\0') {
                ++count;
                ++cursor;
            }
            while (width > count) {
                append_char(output, capacity, &length, ' ');
                --width;
            }
            cursor = text != 0 ? text : "(null)";
            while (*cursor != '\0') {
                append_char(output, capacity, &length, *cursor++);
            }
            continue;
        }
        if (specifier == 'c') {
            char ch = (char)va_arg(args, int);
            while (width > 1) {
                append_char(output, capacity, &length, ' ');
                --width;
            }
            append_char(output, capacity, &length, ch);
            continue;
        }
        if (specifier == 'd' || specifier == 'i') {
            int signed_value = va_arg(args, int);
            negative = signed_value < 0;
            value = negative ? 0u - (unsigned int)signed_value : (unsigned int)signed_value;
            base = 10;
        } else if (specifier == 'u' || specifier == 'x' || specifier == 'X') {
            value = va_arg(args, unsigned int);
            base = specifier == 'u' ? 10 : 16;
        } else {
            output[length] = '\0';
            return -1;
        }
        alphabet = specifier == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
        do {
            digits[count++] = alphabet[value % base];
            value /= base;
        } while (value != 0);
        if (negative && padding == '0') {
            append_char(output, capacity, &length, '-');
        }
        while (width > count + (unsigned int)negative) {
            append_char(output, capacity, &length, padding);
            --width;
        }
        if (negative && padding != '0') {
            append_char(output, capacity, &length, '-');
        }
        while (count != 0) {
            append_char(output, capacity, &length, digits[--count]);
        }
    }
    output[length] = '\0';
    return (int)length;
}
#endif
