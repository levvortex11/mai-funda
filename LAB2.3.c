#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#endif

typedef enum { OK, ERR_ARG, ERR_RANGE, ERR_MEM, ERR_IO } Status;
typedef struct { size_t line, column; } Match;
typedef struct { const char *path; Match *matches; size_t count; } FileMatches;

/* Текст UTF-8: начальный BOM пропускается, CRLF считается одним переводом строки. */
static Status normalize_text(unsigned char *const data, const size_t length, size_t *const result) {
    if ((!data && length) || !result) return ERR_ARG;
    size_t read = 0, written = 0;
    if (length >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) read = 3;
    while (read < length) {
        if (data[read] == '\r' && read + 1 < length && data[read + 1] == '\n') ++read;
        data[written++] = data[read++];
    }
    *result = written; return OK;
}

/* Загружает весь текст; поиск может пересекать любое число строк. */
static Status read_bytes(FILE *const in, unsigned char **const result, size_t *const length) {
    if (!in || !result || !length) return ERR_ARG;
    *result = NULL; *length = 0;
    size_t capacity = 256, n = 0;
    unsigned char *data = malloc(capacity);
    if (!data) return ERR_MEM;
    Status st = OK; int c;
    while ((c = fgetc(in)) != EOF) {
        if (n == capacity) {
            if (capacity > SIZE_MAX / 2) { st = ERR_RANGE; break; }
            unsigned char *next = realloc(data, capacity * 2);
            if (!next) { st = ERR_MEM; break; }
            data = next; capacity *= 2;
        }
        data[n++] = (unsigned char)c;
    }
    if (ferror(in)) st = ERR_IO;
    if (st != OK) { free(data); return st; }
    normalize_text(data, n, length);
    *result = data; return OK;
}

/* Проверяет каждый стартовый байт, в том числе перекрывающиеся вхождения. */
static Status search_bytes(const unsigned char *const data, const size_t length,
                           const char *const pattern, Match **const result, size_t *const count) {
    if ((!data && length) || !pattern || !*pattern || !result || !count) return ERR_ARG;
    *result = NULL; *count = 0;
    const size_t pattern_length = strlen(pattern);
    Match *matches = NULL;
    size_t used = 0, capacity = 0, line = 1, column = 1;
    for (size_t i = 0; i < length; ++i) {
        size_t j = 0;
        if (pattern_length <= length - i)
            while (j < pattern_length && data[i + j] == (unsigned char)pattern[j]) ++j;
        if (j == pattern_length) {
            if (used == capacity) {
                if (capacity > SIZE_MAX / 2 / sizeof(*matches)) { free(matches); return ERR_RANGE; }
                const size_t next_capacity = capacity ? capacity * 2 : 16;
                Match *next = realloc(matches, next_capacity * sizeof(*matches));
                if (!next) { free(matches); return ERR_MEM; }
                matches = next; capacity = next_capacity;
            }
            matches[used++] = (Match){line, column};
        }
        if (data[i] == '\n') { if (line == SIZE_MAX) { free(matches); return ERR_RANGE; } ++line; column = 1; }
        else if ((data[i] & 0xC0) != 0x80) { if (column == SIZE_MAX) { free(matches); return ERR_RANGE; } ++column; }
    }
    *result = matches; *count = used; return OK;
}

/* Освобождает массивы вхождений. Пути принадлежат вызывающему коду. */
static void free_results(FileMatches *const results, const size_t count) {
    for (size_t i = 0; i < count; ++i) free(results[i].matches);
    free(results);
}

/* Ищет подстроку в variadic-списке файлов, возвращая данные без печати. */
static Status search_files(const char *const pattern, const int count, FileMatches **const result, ...) {
    if (!pattern || !*pattern || count <= 0 || !result) return ERR_ARG;
    *result = NULL;
    if ((size_t)count > SIZE_MAX / sizeof(FileMatches)) return ERR_RANGE;
    FileMatches *found = calloc((size_t)count, sizeof(*found));
    if (!found) return ERR_MEM;
    const size_t pattern_size = strlen(pattern);
    char *needle = malloc(pattern_size + 1);
    if (!needle) { free(found); return ERR_MEM; }
    memcpy(needle, pattern, pattern_size + 1);
    size_t normalized_size;
    normalize_text((unsigned char *)needle, pattern_size, &normalized_size);
    needle[normalized_size] = '\0';
    if (!*needle) { free(needle); free(found); return ERR_ARG; }
    va_list args;
    va_start(args, result);
    Status st = OK;
    for (int i = 0; i < count && st == OK; ++i) {
        found[i].path = va_arg(args, const char *);
        if (!found[i].path || !*found[i].path) { st = ERR_ARG; break; }
        FILE *in = fopen(found[i].path, "rb");
        if (!in) { fprintf(stderr, "Error: cannot open input file: %s\n", found[i].path); st = ERR_IO; break; }
        unsigned char *data = NULL; size_t length = 0;
        st = read_bytes(in, &data, &length);
        if (fclose(in) == EOF) st = ERR_IO;
        if (st == OK) st = search_bytes(data, length, needle, &found[i].matches, &found[i].count);
        free(data);
    }
    va_end(args);
    free(needle);
    if (st != OK) { free_results(found, (size_t)count); return st; }
    *result = found; return OK;
}

/* Печатает путь и все пары строка:столбец, индексация начинается с единицы. */
static Status print_matches(const FileMatches *const results, const size_t count) {
    if (!results && count) return ERR_ARG;
    for (size_t i = 0; i < count; ++i) {
        if (printf("%s: %zu matches\n", results[i].path, results[i].count) < 0) return ERR_IO;
        for (size_t j = 0; j < results[i].count; ++j)
            if (printf("  %zu:%zu\n", results[i].matches[j].line, results[i].matches[j].column) < 0) return ERR_IO;
    }
    return OK;
}

/* Печатает понятную причину ошибки; вычисления возвращают только Status. */
static void report_error(const Status st) {
    switch (st) {
        case ERR_ARG: fprintf(stderr, "Error: empty pattern, invalid escape sequence or missing file argument.\n"); break;
        case ERR_RANGE: fprintf(stderr, "Error: file size, match count or coordinates exceed the supported range.\n"); break;
        case ERR_MEM: fprintf(stderr, "Error: not enough memory.\n"); break;
        case ERR_IO: fprintf(stderr, "Error: cannot open/read/write/close a file or output stream.\n"); break;
        case OK: break;
        default: fprintf(stderr, "Error: unknown status.\n"); break;
    }
}

/* В Windows обычный argv может быть в системной кодировке, а файл — в UTF-8.
   Корректный UTF-8 оставляем; иначе переводим системную строку в UTF-8. */
static Status argument_pattern(const char *const argument, char **const result) {
    if (!argument || !result) return ERR_ARG;
    *result = NULL;
#ifdef _WIN32
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argument, -1, NULL, 0)) {
        const int count = MultiByteToWideChar(CP_ACP, 0, argument, -1, NULL, 0);
        if (!count || (size_t)count > SIZE_MAX / sizeof(wchar_t)) return ERR_ARG;
        wchar_t *wide = malloc((size_t)count * sizeof(*wide));
        if (!wide) return ERR_MEM;
        if (!MultiByteToWideChar(CP_ACP, 0, argument, -1, wide, count)) { free(wide); return ERR_ARG; }
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
        char *text = size > 0 ? malloc((size_t)size) : NULL;
        if (!text) { free(wide); return size > 0 ? ERR_MEM : ERR_ARG; }
        const int converted = WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, size, NULL, NULL);
        free(wide);
        if (!converted) { free(text); return ERR_ARG; }
        *result = text; return OK;
    }
#endif
    const size_t n = strlen(argument);
    char *text = malloc(n + 1);
    if (!text) return ERR_MEM;
    memcpy(text, argument, n + 1); *result = text; return OK;
}

/* Только режим -e: переводит \n, \r, \t и \\ в настоящие символы.
   Без -e обратные слеши остаются обычными символами поиска. */
static Status decode_escapes(char *const text) {
    if (!text) return ERR_ARG;
    size_t read = 0, written = 0;
    while (text[read]) {
        char c = text[read++];
        if (c == '\\') {
            c = text[read++];
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c != '\\') return ERR_ARG;
        }
        text[written++] = c;
    }
    text[written] = '\0'; return OK;
}

/* Печатает все найденные координаты; строки поиска и результаты освобождаются здесь. */
int main(int argc, char **argv) {
    Status st = OK;
    if (argc == 1) {
        FileMatches *result = NULL;
        st = search_files("aba", 2, &result, (const char *)"lab2/examples/search1.txt", (const char *)"lab2/examples/search2.txt");
        if (st == OK) { st = print_matches(result, 2); free_results(result, 2); }
    } else {
        const int pattern_index = strcmp(argv[1], "-e") == 0 ? 2 : 1;
        if (argc <= pattern_index + 1) st = ERR_ARG;
        char *pattern = NULL;
        if (st == OK) st = argument_pattern(argv[pattern_index], &pattern);
        if (st == OK && pattern_index == 2) st = decode_escapes(pattern);
        for (int i = pattern_index + 1; i < argc && st == OK; ++i) {
            FileMatches *result = NULL;
            st = search_files(pattern, 1, &result, (const char *)argv[i]);
            if (st == OK) { st = print_matches(result, 1); free_results(result, 1); }
        }
        free(pattern);
    }
    if (st != OK) { report_error(st); fprintf(stderr, "Usage: %s [-e] pattern file [files...]\n", argv[0]); }
    return st != OK;
}
