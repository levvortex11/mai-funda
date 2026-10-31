#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <limits.h>
#include <stdbool.h>
#include <math.h>

typedef enum { OK, ERR_ARG, ERR_RANGE, ERR_MEM, ERR_IO } Status;

/* Читает всю строку, не ограничивая длину имени фиксированным буфером. */
static Status read_line(FILE *const in, char **const result, bool *const exists) {
    if (!in || !result || !exists) return ERR_ARG;
    *result = NULL; *exists = false;
    size_t size = 128, used = 0;
    char *line = malloc(size);
    if (!line) return ERR_MEM;
    int c; Status st = OK;
    while ((c = fgetc(in)) != EOF) {
        *exists = true;
        if (c == '\n') break;
        if (c == 0) { st = ERR_ARG; break; }
        if (used == size - 1) {
            if (size > SIZE_MAX / 2) { st = ERR_RANGE; break; }
            char *next = realloc(line, size * 2);
            if (!next) { st = ERR_MEM; break; }
            line = next; size *= 2;
        }
        line[used++] = (char)c;
    }
    if (ferror(in)) st = ERR_IO;
    if (st != OK) { free(line); return st; }
    line[used] = '\0'; *result = line;
    return OK;
}

/* Разбивает одну строку ровно на count полей, используя её собственную память. */
static Status fields(char *line, char **const words, const size_t count) {
    if (!line || !words) return ERR_ARG;
    size_t n = 0;
    while (*line) {
        while (isspace((unsigned char)*line)) ++line;
        if (!*line) break;
        if (n == count) return ERR_ARG;
        words[n++] = line;
        while (*line && !isspace((unsigned char)*line)) ++line;
        if (*line) *line++ = '\0';
    }
    return n == count ? OK : ERR_ARG;
}

/* Читает неотрицательное целое с проверкой до умножения, без errno. */
static Status parse_unsigned(const char *s, unsigned long long *const result) {
    if (!s || !result) return ERR_ARG;
    if (*s == '+') ++s;
    if (!*s) return ERR_ARG;
    unsigned long long value = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return ERR_ARG;
        const unsigned digit = (unsigned)(*s - '0');
        if (value > (ULLONG_MAX - digit) / 10) return ERR_RANGE;
        value = value * 10 + digit;
    }
    *result = value; return OK;
}

/* Копирует имя в отдельный динамический блок. */
static Status copy_name(const char *const text, char **const result) {
    if (!text || !*text || !result) return ERR_ARG;
    *result = NULL;
    const size_t n = strlen(text);
    if (n == SIZE_MAX) return ERR_RANGE;
    char *copy = malloc(n + 1);
    if (!copy) return ERR_MEM;
    memcpy(copy, text, n + 1); *result = copy;
    return OK;
}

typedef struct { char *name; size_t size; unsigned long long importance; } FileInfo;
typedef struct {
    FileInfo *files;
    size_t count, total_size;
    unsigned long long total_importance;
} Backup;

/* Освобождает входные имена. В результате выбора имена только заимствуются. */
static void free_files(FileInfo *const files, const size_t count) {
    for (size_t i = 0; i < count; ++i) free(files[i].name);
    free(files);
}

/* Читает n, capacity и ровно n строк; не принимает повторяющиеся имена и лишние поля строки. */
static Status read_files(FILE *const in, FileInfo **const result, size_t *const count, size_t *const capacity) {
    if (!in || !result || !count || !capacity) return ERR_ARG;
    *result = NULL; *count = 0;
    char *line = NULL, *word[3]; bool exists;
    Status st = read_line(in, &line, &exists);
    unsigned long long n = 0, cap = 0;
    if (st == OK) st = exists ? fields(line, word, 2) : ERR_ARG;
    if (st == OK) st = parse_unsigned(word[0], &n);
    if (st == OK) st = parse_unsigned(word[1], &cap);
    free(line);
    if (st != OK) { fprintf(stderr, "Line 1: expected positive file count and nonnegative capacity.\n"); return st; }
    if (!n || n > SIZE_MAX / sizeof(FileInfo) || cap > SIZE_MAX) {
        fprintf(stderr, "Line 1: file count must be positive and sizes must fit size_t.\n"); return ERR_RANGE;
    }
    FileInfo *files = calloc((size_t)n, sizeof(*files));
    if (!files) return ERR_MEM;
    for (size_t i = 0; i < (size_t)n && st == OK; ++i) {
        line = NULL;
        st = read_line(in, &line, &exists);
        if (st == OK) st = exists ? fields(line, word, 3) : ERR_ARG;
        unsigned long long size = 0;
        if (st == OK) st = parse_unsigned(word[1], &size);
        if (st == OK && (!size || size > SIZE_MAX)) {
            fprintf(stderr, "Line %zu: file size must be a positive integer (zero is forbidden by the PDF).\n", i + 2);
            st = ERR_RANGE;
        }
        if (st == OK) st = parse_unsigned(word[2], &files[i].importance);
        if (st == OK) st = copy_name(word[0], &files[i].name);
        if (st == OK) {
            files[i].size = (size_t)size;
            for (size_t j = 0; j < i; ++j)
                if (strcmp(files[i].name, files[j].name) == 0) {
                    fprintf(stderr, "Line %zu: duplicate filename '%s'.\n", i + 2, files[i].name);
                    st = ERR_ARG;
                }
        }
        if (st != OK) fprintf(stderr, "Line %zu: expected filename positive_size nonnegative_importance.\n", i + 2);
        free(line);
    }
    if (st != OK) { free_files(files, (size_t)n); return st; }
    *result = files; *count = (size_t)n; *capacity = (size_t)cap; return OK;
}

/* Таблица рюкзака: строка i учитывает первые i файлов, столбец c — вместимость.
   Результат владеет массивом описаний, но не копирует имена входных файлов. */
static Status choose_files(const FileInfo *const files, const size_t count,
                           const size_t capacity, Backup *const result) {
    if (!result) return ERR_ARG;
    *result = (Backup){NULL, 0, 0, 0};
    if (!files || !count) return ERR_ARG;
    for (size_t i = 0; i < count; ++i) {
        if (!files[i].name || !*files[i].name || !files[i].size) return ERR_ARG;
        for (const char *p = files[i].name; *p; ++p) if (isspace((unsigned char)*p)) return ERR_ARG;
        for (size_t j = 0; j < i; ++j) if (!strcmp(files[i].name, files[j].name)) return ERR_ARG;
    }
    /* Нет смысла строить столбцы для места сверх суммы подходящих файлов. */
    size_t limit = 0;
    for (size_t i = 0; i < count; ++i) if (files[i].size <= capacity) {
        if (files[i].size > capacity - limit) limit = capacity;
        else limit += files[i].size;
    }
    if (!limit) return OK;
    if (count == SIZE_MAX || limit == SIZE_MAX) return ERR_RANGE;
    const size_t columns = limit + 1;
    if (count + 1 > SIZE_MAX / columns / sizeof(unsigned long long) ||
        count > SIZE_MAX / sizeof(FileInfo)) return ERR_RANGE;
    unsigned long long *table = calloc((count + 1) * columns, sizeof(*table));
    FileInfo *selected = malloc(count * sizeof(*selected));
    if (!table || !selected) { free(table); free(selected); return ERR_MEM; }
    Status st = OK;
    for (size_t i = 1; i <= count && st == OK; ++i) {
        const FileInfo f = files[i - 1];
        for (size_t c = 0; c <= limit; ++c) {
            unsigned long long best = table[(i - 1) * columns + c];
            if (f.size <= c) {
                const unsigned long long before = table[(i - 1) * columns + c - f.size];
                if (before > ULLONG_MAX - f.importance) { st = ERR_RANGE; break; }
                const unsigned long long take = before + f.importance;
                if (take > best) best = take;
            }
            table[i * columns + c] = best;
        }
    }
    size_t used = 0, space = limit, total = 0;
    if (st == OK) {
        for (size_t i = count; i > 0; --i) {
            if (table[i * columns + space] != table[(i - 1) * columns + space]) {
                selected[used++] = files[i - 1];
                space -= files[i - 1].size; total += files[i - 1].size;
            }
        }
        /* Восстановление шло с конца: разворачиваем выбранные файлы в исходный порядок. */
        for (size_t i = 0; i < used / 2; ++i) {
            FileInfo tmp = selected[i]; selected[i] = selected[used - 1 - i]; selected[used - 1 - i] = tmp;
        }
        result->total_importance = table[count * columns + limit];
        result->total_size = total; result->count = used;
    }
    free(table);
    if (st != OK || !used) free(selected);
    else result->files = selected;
    return st;
}

/* Печатает понятную причину ошибки; вычисления возвращают только Status. */
static void report_error(const Status st) {
    switch (st) {
        case ERR_ARG: fprintf(stderr, "Error: invalid input: unique nonempty filenames, positive count/sizes and nonnegative capacity/importance are required.\n"); break;
        case ERR_RANGE: fprintf(stderr, "Error: size is zero/too large, importance sum overflows, or the knapsack table is too large.\n"); break;
        case ERR_MEM: fprintf(stderr, "Error: not enough memory.\n"); break;
        case ERR_IO: fprintf(stderr, "Error: cannot open/read/write/close a file or output stream.\n"); break;
        case OK: break;
        default: fprintf(stderr, "Error: unknown status.\n"); break;
    }
}

/* Выводит результат строго в формате PDF и освобождает оба массива. */
int main(void) {
    FileInfo *files = NULL; size_t count = 0, capacity = 0;
    Backup result = {NULL, 0, 0, 0};
    Status st = read_files(stdin, &files, &count, &capacity);
    if (st == OK) st = choose_files(files, count, capacity, &result);
    if (st == OK) {
        if (printf("TOTAL_SIZE %zu\nTOTAL_IMPORTANCE %llu\nFILES %zu\n",
                   result.total_size, result.total_importance, result.count) < 0) st = ERR_IO;
        for (size_t i = 0; i < result.count; ++i) if (puts(result.files[i].name) == EOF) st = ERR_IO;
    }
    free(result.files); free_files(files, count);
    if (st != OK) report_error(st);
    return st != OK;
}
