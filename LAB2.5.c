/* MinGW: стандартные форматы C, включая настоящий long double. */
#if defined(__MINGW32__) && !defined(__USE_MINGW_ANSI_STDIO)
#define __USE_MINGW_ANSI_STDIO 1
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>
#include <wchar.h>
#include <math.h>
#include <float.h>

typedef enum { OK, ERR_ARG, ERR_RANGE, ERR_MEM, ERR_IO } Status;

/* Читает int в указанном основании, накапливая отрицательное значение для INT_MIN. */
static Status parse_base(const char *s, int base, const bool upper, int *const result) {
    if (!s || !*s || !result) return ERR_ARG;
    if (base < 2 || base > 36) base = 10;
    const bool negative = *s == '-';
    if (*s == '+' || *s == '-') ++s;
    if (!*s) return ERR_ARG;
    const int limit = negative ? INT_MIN : -INT_MAX;
    int value = 0;
    for (; *s; ++s) {
        int digit = *s >= '0' && *s <= '9' ? *s - '0' : -1;
        if (upper && *s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
        if (!upper && *s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
        if (digit < 0 || digit >= base) return ERR_ARG;
        if (value < (limit + digit) / base) return ERR_RANGE;
        value = value * base - digit;
    }
    *result = negative ? value : -value;
    return OK;
}

/* Переводит int без опасного abs(INT_MIN), результат помещается в указанную ёмкость. */
static Status format_base(int value, int base, const bool upper, char *const result, const size_t capacity) {
    if (!result || capacity < sizeof(int) * CHAR_BIT + 2) return ERR_ARG;
    if (base < 2 || base > 36) base = 10;
    const bool negative = value < 0;
    if (value > 0) value = -value;
    char reversed[sizeof(int) * CHAR_BIT + 1];
    size_t n = 0;
    do {
        const int digit = -(value % base);
        reversed[n++] = (char)(digit < 10 ? '0' + digit : (upper ? 'A' : 'a') + digit - 10);
        value /= base;
    } while (value);
    size_t pos = 0;
    if (negative) result[pos++] = '-';
    while (n) result[pos++] = reversed[--n];
    result[pos] = '\0'; return OK;
}

/* Обычная запись до 3999; скобки умножают группу на 1000: (V) = 5000.
   Внутри каждой группы I, X, C, M не повторяются больше трёх раз подряд. */
static Status roman_text(const int value, char **const result) {
    if (!result) return ERR_ARG;
    *result = NULL;
    unsigned magnitude = value < 0 ? 0u - (unsigned)value : (unsigned)value;
    char *thousands = NULL;
    if (magnitude >= 4000) {
        Status st = roman_text((int)(magnitude / 1000u), &thousands);
        if (st != OK) return st;
        magnitude %= 1000u;
    }
    const size_t high_length = thousands ? strlen(thousands) : 0;
    if (high_length > SIZE_MAX - 32) { free(thousands); return ERR_RANGE; }
    char *text = malloc(high_length + 32);
    if (!text) { free(thousands); return ERR_MEM; }
    size_t pos = 0;
    if (value < 0) text[pos++] = '-';
    if (thousands) {
        text[pos++] = '(';
        memcpy(text + pos, thousands, high_length); pos += high_length;
        text[pos++] = ')';
    }
    free(thousands);
    const unsigned values[] = {1000,900,500,400,100,90,50,40,10,9,5,4,1};
    const char *const symbols[] = {"M","CM","D","CD","C","XC","L","XL","X","IX","V","IV","I"};
    if (!value) text[pos++] = 'N';
    for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
        while (magnitude >= values[i]) {
            for (size_t j = 0; symbols[i][j]; ++j) text[pos++] = symbols[i][j];
            magnitude -= values[i];
        }
    text[pos] = '\0'; *result = text; return OK;
}

typedef struct { char *data; size_t length, capacity; } Text;

/* Добавляет байты в растущий буфер и оставляет завершающий ноль. */
static Status append_text(Text *const out, const char *const text, const size_t count) {
    if (!out || (!text && count)) return ERR_ARG;
    if (count > (size_t)INT_MAX - out->length) return ERR_RANGE;
    const size_t need = out->length + count + 1;
    if (need > out->capacity) {
        size_t capacity = out->capacity ? out->capacity : 128;
        while (capacity < need) {
            if (capacity > SIZE_MAX / 2) { capacity = need; break; }
            capacity *= 2;
        }
        char *next = realloc(out->data, capacity);
        if (!next) return ERR_MEM;
        out->data = next; out->capacity = capacity;
    }
    if (count) memcpy(out->data + out->length, text, count);
    out->length += count; out->data[out->length] = '\0'; return OK;
}

/* Жадно выбирает числа Фибоначчи, затем пишет коэффициенты от младшего к старшему. */
static Status zeckendorf(const unsigned value, char *const text, const size_t capacity) {
    if (!text || capacity < 2 * sizeof(unsigned) * CHAR_BIT + 3) return ERR_ARG;
    if (!value) { strcpy(text, "01"); return OK; }
    unsigned fibonacci[2 * sizeof(unsigned) * CHAR_BIT];
    fibonacci[0] = 1; fibonacci[1] = 2;
    size_t n = 2;
    while (fibonacci[n - 1] <= UINT_MAX - fibonacci[n - 2]) {
        const unsigned next = fibonacci[n - 1] + fibonacci[n - 2];
        if (next > value) break;
        fibonacci[n++] = next;
    }
    while (n > 1 && fibonacci[n - 1] > value) --n;
    memset(text, '0', n);
    unsigned rest = value;
    for (size_t i = n; i > 0; --i) if (fibonacci[i - 1] <= rest) {
        text[i - 1] = '1'; rest -= fibonacci[i - 1];
    }
    text[n] = '1'; text[n + 1] = '\0'; return OK;
}

/* Пишет байты объекта в порядке адресов, внутри байта старший бит первый. */
static Status dump_memory(Text *const out, const void *const address, const size_t size) {
    if (!out || !address || CHAR_BIT != 8) return ERR_ARG;
    const unsigned char *bytes = address;
    for (size_t i = 0; i < size; ++i) {
        char bits[8];
        for (int bit = 0; bit < 8; ++bit) bits[bit] = (char)('0' + ((bytes[i] >> (7 - bit)) & 1));
        Status st = i ? append_text(out, " ", 1) : OK;
        if (st == OK) st = append_text(out, bits, 8);
        if (st != OK) return st;
    }
    return OK;
}

/* Разбирает один стандартный спецификатор, не вводя искусственного лимита его длины. */
static Status standard_spec(const char *const start, const char **const end, char length[3], char *const conversion) {
    const char *p = start + 1;
    while (*p && strchr("-+ #0", *p)) ++p;
    if (*p == '*') ++p;
    else while (isdigit((unsigned char)*p)) ++p;
    if (*p == '.') {
        ++p;
        if (*p == '*') ++p;
        else while (isdigit((unsigned char)*p)) ++p;
    }
    length[0] = length[1] = length[2] = '\0';
    if (*p && strchr("hljztL", *p)) {
        length[0] = *p++;
        if ((length[0] == 'h' || length[0] == 'l') && *p == length[0]) length[1] = *p++;
    }
    if (!*p || !strchr("diouxXfFeEgGaAcspn%", *p)) return ERR_ARG;
    *conversion = *p++; *end = p;
    if (strchr("diouxXn", *conversion) && length[0] == 'L') return ERR_ARG;
    if (strchr("fFeEgGaA", *conversion) && length[0] && strcmp(length,"l") && strcmp(length,"L")) return ERR_ARG;
    if (strchr("cs", *conversion) && length[0] && strcmp(length,"l")) return ERR_ARG;
    if (strchr("p%", *conversion) && length[0]) return ERR_ARG;
    if (*conversion == '%' && p - start != 2) return ERR_ARG;
    if (*conversion == 'n' && (size_t)(p-start) != 2 + strlen(length)) return ERR_ARG;
    return OK;
}

/* Пропускает аргументы стандартного формата после работы vsnprintf над копией списка. */
static Status advance_argument(va_list *const args, const char *const spec, const char *const length,
                               const char conversion, const size_t written) {
    for (const char *p = spec; *p; ++p) if (*p == '*') (void)va_arg(*args, int);
    if (conversion == 'n') {
        /* %n записывает общий счётчик, а не длину отдельного фрагмента. */
#define STORE_COUNT(T, LIMIT) do { T *ptr = va_arg(*args, T *); if (!ptr) return ERR_ARG; \
    if (written > (size_t)(LIMIT)) { return ERR_RANGE; } *ptr = (T)written; } while (0)
        if (!strcmp(length,"hh")) STORE_COUNT(signed char, SCHAR_MAX);
        else if (!strcmp(length,"h")) STORE_COUNT(short, SHRT_MAX);
        else if (!strcmp(length,"ll")) STORE_COUNT(long long, LLONG_MAX);
        else if (!strcmp(length,"l")) STORE_COUNT(long, LONG_MAX);
        else if (!strcmp(length,"j")) STORE_COUNT(intmax_t, INTMAX_MAX);
        else if (!strcmp(length,"z") || !strcmp(length,"t")) STORE_COUNT(ptrdiff_t, PTRDIFF_MAX);
        else STORE_COUNT(int, INT_MAX);
#undef STORE_COUNT
    } else if (strchr("di", conversion)) {
        if (!strcmp(length,"ll")) (void)va_arg(*args, long long);
        else if (!strcmp(length,"l")) (void)va_arg(*args, long);
        else if (!strcmp(length,"j")) (void)va_arg(*args, intmax_t);
        else if (!strcmp(length,"z") || !strcmp(length,"t")) (void)va_arg(*args, ptrdiff_t);
        else (void)va_arg(*args, int);
    } else if (strchr("ouxX", conversion)) {
        if (!strcmp(length,"ll")) (void)va_arg(*args, unsigned long long);
        else if (!strcmp(length,"l")) (void)va_arg(*args, unsigned long);
        else if (!strcmp(length,"j")) (void)va_arg(*args, uintmax_t);
        else if (!strcmp(length,"z") || !strcmp(length,"t")) (void)va_arg(*args, size_t);
        else if (!strcmp(length,"hh") || !strcmp(length,"h")) (void)va_arg(*args, int);
        else (void)va_arg(*args, unsigned);
    } else if (strchr("fFeEgGaA", conversion)) {
        if (length[0] == 'L') (void)va_arg(*args, long double);
        else (void)va_arg(*args, double);
    } else if (conversion == 'c') {
        if (length[0] == 'l') {
            /* В Windows wint_t уже int и продвигается при передаче через ... . */
#if WINT_MAX <= INT_MAX
            (void)va_arg(*args, int);
#else
            (void)va_arg(*args, wint_t);
#endif
        }
        else (void)va_arg(*args, int);
    } else if (conversion == 's') {
        if (length[0] == 'l') { if (!va_arg(*args, wchar_t *)) return ERR_ARG; }
        else if (!va_arg(*args, char *)) return ERR_ARG;
    } else if (conversion == 'p') (void)va_arg(*args, void *);
    return OK;
}

/* Обрабатывает дополнительный двухбуквенный формат и добавляет его результат. */
static Status custom_output(Text *const out, const char a, const char b, va_list *const args) {
    char text[2 * sizeof(unsigned) * CHAR_BIT + 3];
    Status st = OK;
    if (a == 'R' && b == 'o') {
        char *roman = NULL;
        st = roman_text(va_arg(*args,int), &roman);
        if (st == OK) st = append_text(out, roman, strlen(roman));
        free(roman); return st;
    }
    if (a == 'Z' && b == 'r') st = zeckendorf(va_arg(*args,unsigned), text, sizeof(text));
    else if (a == 'C' && (b == 'v' || b == 'V')) {
        const int value = va_arg(*args,int), base = va_arg(*args,int);
        st = format_base(value, base, b == 'V', text, sizeof(text));
    } else if ((a == 't' && b == 'o') || (a == 'T' && b == 'O')) {
        const char *word = va_arg(*args,char *);
        const int base = va_arg(*args,int); int value;
        st = parse_base(word, base, a == 'T', &value);
        if (st == OK) st = format_base(value, 10, false, text, sizeof(text));
    } else if (a == 'm') {
        if (b == 'i') { int v = va_arg(*args,int); return sizeof(v) == 4 ? dump_memory(out,&v,sizeof(v)) : ERR_RANGE; }
        if (b == 'u') { unsigned v = va_arg(*args,unsigned); return sizeof(v) == 4 ? dump_memory(out,&v,sizeof(v)) : ERR_RANGE; }
        if (b == 'd') { double v = va_arg(*args,double); return dump_memory(out,&v,sizeof(v)); }
        if (b == 'f') {
            const double promoted = va_arg(*args,double);
            if (isfinite(promoted) && fabs(promoted) > FLT_MAX) return ERR_RANGE;
            float v = (float)promoted; return dump_memory(out,&v,sizeof(v));
        }
        return ERR_ARG;
    } else return ERR_ARG;
    return st == OK ? append_text(out, text, strlen(text)) : st;
}

/* Строит вывод: стандартные преобразования выполняет vsnprintf, новые — наши функции. */
static Status format_output(Text *const out, const char *format, va_list *const args) {
    if (!out || !format || !args) return ERR_ARG;
    Status st = append_text(out,"",0);
    while (*format && st == OK) {
        if (*format != '%') { st = append_text(out,format++,1); continue; }
        const char a = format[1], b = a ? format[2] : '\0';
        if ((a=='R' && b=='o') || (a=='Z' && b=='r') || (a=='C' && (b=='v'||b=='V')) ||
            (a=='t' && b=='o') || (a=='T' && b=='O') || (a=='m' && b && strchr("iudf",b))) {
            st = custom_output(out,a,b,args); format += 3; continue;
        }
        const char *end; char length[3], conversion;
        st = standard_spec(format,&end,length,&conversion);
        if (st != OK) break;
        const size_t n = (size_t)(end-format);
        char *spec = malloc(n+1);
        if (!spec) { st = ERR_MEM; break; }
        memcpy(spec,format,n); spec[n]='\0';
        va_list copy;
        va_copy(copy,*args);
        st = advance_argument(args,spec,length,conversion,out->length);
        if (st == OK && conversion != 'n') {
            va_list measure;
            va_copy(measure,copy);
            const int needed = vsnprintf(NULL,0,spec,measure);
            va_end(measure);
            if (needed < 0) st = ERR_ARG;
            else if ((size_t)needed > (size_t)INT_MAX - out->length) st = ERR_RANGE;
            else {
                char *piece = malloc((size_t)needed+1);
                if (!piece) st = ERR_MEM;
                else {
                    if (vsnprintf(piece,(size_t)needed+1,spec,copy) != needed) st = ERR_ARG;
                    else st = append_text(out,piece,(size_t)needed);
                    free(piece);
                }
            }
        }
        va_end(copy); free(spec); format=end;
    }
    return st;
}

/* Аналог fprintf: число записанных байтов либо отрицательное значение при ошибке. */
static int overfprintf(FILE *const stream, const char *const format, ...) {
    if (!stream || !format) return -1;
    Text text = {NULL,0,0}; va_list args;
    va_start(args,format); Status st = format_output(&text,format,&args); va_end(args);
    if (st == OK && fwrite(text.data,1,text.length,stream) != text.length) st = ERR_IO;
    const int count = st == OK ? (int)text.length : -(int)st;
    free(text.data); return count;
}

/* Как у sprintf, достаточный размер назначения обязан обеспечить вызывающий код. */
static int oversprintf(char *const destination, const char *const format, ...) {
    if (!destination || !format) return -1;
    Text text = {NULL,0,0}; va_list args;
    va_start(args,format); Status st = format_output(&text,format,&args); va_end(args);
    if (st == OK) memcpy(destination,text.data,text.length+1);
    const int count = st == OK ? (int)text.length : -(int)st;
    free(text.data); return count;
}

/* Расшифровывает отрицательный результат наших форматных функций. */
static void report_error(const int code) {
    switch (code) {
        case ERR_ARG: fprintf(stderr, "Error: invalid format, NULL argument or invalid digit/case for the chosen base.\n"); break;
        case ERR_RANGE: fprintf(stderr, "Error: a number/output size exceeds the supported range or the dump requires an unsupported object size.\n"); break;
        case ERR_MEM: fprintf(stderr, "Error: not enough memory to format the output.\n"); break;
        case ERR_IO: fprintf(stderr, "Error: cannot write to the output stream.\n"); break;
        default: fprintf(stderr, "Error: output formatting failed.\n"); break;
    }
}

/* Демонстрирует все дополнительные форматы и совместное применение обычных. */
int main(void) {
    char text[512]; int count = -1;
    int n = oversprintf(text,"%Ro %Zr %Cv %CV %to %TO | %08d %.2f%n",
        5000, 10u, -255, 16, 255, 16, (char *)"ff", 16, (char *)"FF", 16, 7, 1.25, &count);
    if (n < 0) {
        report_error(-n);
        return 1;
    }
    if (n != count || puts(text) == EOF) { fprintf(stderr, "Error: output write or byte-count check failed.\n"); return 1; }
    if (overfprintf(stdout,"int: %mi\nuint: %mu\ndouble: %md\nfloat: %mf\n",-1,1u,1.0,1.0) < 0) { fprintf(stderr, "Error: cannot format or write memory dumps.\n"); return 1; }
    return 0;
}
