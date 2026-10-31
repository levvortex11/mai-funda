#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

typedef enum { OK, ERR_ARG, ERR_RANGE, ERR_MEM, ERR_IO } Status;

/* Считает байты до нулевого символа; string.h по условию не используем. */
static Status mode_l(const char *const text, size_t *const length) {
    if (!text || !length) return ERR_ARG;
    size_t n = 0;
    while (text[n]) ++n;
    *length = n;
    return OK;
}

/* Создаёт перевёрнутую копию строки в динамической памяти. */
static Status mode_r(const char *const text, char **const result) {
    if (!result) return ERR_ARG;
    *result = NULL;
    size_t n;
    Status st = mode_l(text, &n);
    if (st != OK) return st;
    if (n == SIZE_MAX) return ERR_RANGE;
    char *copy = malloc(n + 1);
    if (!copy) return ERR_MEM;
    for (size_t i = 0; i < n; ++i) copy[i] = text[n - 1 - i];
    copy[n] = '\0';
    *result = copy;
    return OK;
}

/* Поднимает регистр латинских букв на нечётных индексах копии. */
static Status mode_u(const char *const text, char **const result) {
    if (!result) return ERR_ARG;
    *result = NULL;
    size_t n;
    Status st = mode_l(text, &n);
    if (st != OK) return st;
    if (n == SIZE_MAX) return ERR_RANGE;
    char *copy = malloc(n + 1);
    if (!copy) return ERR_MEM;
    for (size_t i = 0; i < n; ++i) {
        char c = text[i];
        if (i % 2 && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        copy[i] = c;
    }
    copy[n] = '\0';
    *result = copy;
    return OK;
}

/* Три прохода сохраняют порядок цифр, латинских букв и остальных байтов. */
static Status mode_n(const char *const text, char **const result) {
    if (!result) return ERR_ARG;
    *result = NULL;
    size_t n;
    Status st = mode_l(text, &n);
    if (st != OK) return st;
    if (n == SIZE_MAX) return ERR_RANGE;
    char *copy = malloc(n + 1);
    if (!copy) return ERR_MEM;
    size_t pos = 0;
    for (int group = 0; group < 3; ++group) {
        for (size_t i = 0; i < n; ++i) {
            const char c = text[i];
            int kind = c >= '0' && c <= '9' ? 0 :
                ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ? 1 : 2);
            if (kind == group) copy[pos++] = c;
        }
    }
    copy[pos] = '\0';
    *result = copy;
    return OK;
}

/* Перемешивает индексы и склеивает строки; исходные строки не меняет. */
static Status mode_c(const char *const *const words, const size_t count,
                     const unsigned seed, char **const result) {
    if (!words || !count || !result) return ERR_ARG;
    *result = NULL;
    if (count > SIZE_MAX / sizeof(size_t)) return ERR_RANGE;
    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t n;
        Status st = mode_l(words[i], &n);
        if (st != OK) return st;
        if (n > SIZE_MAX - 1 - total) return ERR_RANGE;
        total += n;
    }
    size_t *order = malloc(count * sizeof(*order));
    char *copy = malloc(total + 1);
    if (!order || !copy) { free(order); free(copy); return ERR_MEM; }
    for (size_t i = 0; i < count; ++i) order[i] = i;
    srand(seed);
    for (size_t i = count - 1; i > 0; --i) {
        const size_t j = (size_t)rand() % (i + 1);
        const size_t tmp = order[i]; order[i] = order[j]; order[j] = tmp;
    }
    size_t pos = 0;
    for (size_t i = 0; i < count; ++i)
        for (size_t j = 0; words[order[i]][j]; ++j) copy[pos++] = words[order[i]][j];
    copy[pos] = '\0';
    free(order);
    *result = copy;
    return OK;
}

/* Разбирает seed без errno и проверяет переполнение до операции. */
static Status parse_seed(const char *s, unsigned *const result) {
    if (!s || !result) return ERR_ARG;
    if (*s == '+') ++s;
    if (!*s) return ERR_ARG;
    unsigned value = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return ERR_ARG;
        const unsigned digit = (unsigned)(*s - '0');
        if (value > (UINT_MAX - digit) / 10) return ERR_RANGE;
        value = value * 10 + digit;
    }
    *result = value;
    return OK;
}

/* Печатает понятную причину ошибки; вычисления возвращают только Status. */
static void report_error(const Status st) {
    switch (st) {
        case ERR_ARG: fprintf(stderr, "Error: invalid flag, argument count, string or seed; seed must be an unsigned integer.\n"); break;
        case ERR_RANGE: fprintf(stderr, "Error: seed or combined string size exceeds the supported range.\n"); break;
        case ERR_MEM: fprintf(stderr, "Error: not enough memory.\n"); break;
        case ERR_IO: fprintf(stderr, "Error: cannot open/read/write/close a file or output stream.\n"); break;
        case OK: break;
        default: fprintf(stderr, "Error: unknown status.\n"); break;
    }
}

/* Проверяет CLI, печатает результат и освобождает созданную строку. */
int main(int argc, char **argv) {
    if (argc < 3 || argv[1][0] != '-' || !argv[1][1] || argv[1][2]) {
        fprintf(stderr, "Usage: %s -l|-r|-u|-n text OR -c text seed [texts...]\n", argv[0]);
        return 1;
    }
    const char mode = argv[1][1];
    if ((mode != 'c' && argc != 3) || (mode == 'c' && argc < 4)) { report_error(ERR_ARG); return 1; }
    Status st = ERR_ARG;
    char *result = NULL;
    size_t length = 0;
    switch (mode) {
        case 'l': st = mode_l(argv[2], &length); break;
        case 'r': st = mode_r(argv[2], &result); break;
        case 'u': st = mode_u(argv[2], &result); break;
        case 'n': st = mode_n(argv[2], &result); break;
        case 'c': {
            unsigned seed;
            st = parse_seed(argv[3], &seed);
            if (st != OK) break;
            const size_t count = (size_t)argc - 3;
            if (count > SIZE_MAX / sizeof(char *)) { st = ERR_RANGE; break; }
            const char **words = malloc(count * sizeof(*words));
            if (!words) { st = ERR_MEM; break; }
            words[0] = argv[2];
            for (size_t i = 1; i < count; ++i) words[i] = argv[i + 3];
            st = mode_c(words, count, seed, &result);
            free(words);
            break;
        }
    }
    if (st == OK) {
        if (mode == 'l') { if (printf("%zu\n", length) < 0) st = ERR_IO; }
        else if (puts(result) == EOF) st = ERR_IO;
    }
    free(result);
    if (st != OK) report_error(st);
    return st != OK;
}
