#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <stdint.h>

typedef enum {
    OK = 0,
    ERR_ARG,
    ERR_FILE,
    ERR_MEM,
    ERR_OVERFLOW
} Status;

/* -d: переписывает все символы, кроме цифр; проверяет чтение и запись. */
static Status mode_d(FILE *const in, FILE *const out) {
    if (!in || !out) return ERR_ARG;
    int c;
    while ((c = fgetc(in)) != EOF) {
        if (!(c >= '0' && c <= '9') && fputc(c, out) == EOF) return ERR_FILE;
    }
    return ferror(in) || ferror(out) ? ERR_FILE : OK;
}

/* -i: число латинских букв для каждой реально прочитанной строки. */
static Status mode_i(FILE *const in, FILE *const out) {
    if (!in || !out) return ERR_ARG;
    int c;
    long count = 0;
    bool has_line = false;
    while ((c = fgetc(in)) != EOF) {
        if (c == '\n') {
            if (fprintf(out, "%ld\n", count) < 0) return ERR_FILE;
            count = 0;
            has_line = false;
        } else {
            has_line = true;
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                if (count == LONG_MAX) return ERR_OVERFLOW;
                ++count;
            }
        }
    }
    if (ferror(in)) return ERR_FILE;
    /* Последнюю строку дописываем только если она не закончилась переводом строки. */
    if (has_line && fprintf(out, "%ld\n", count) < 0) return ERR_FILE;
    return ferror(out) ? ERR_FILE : OK;
}

/* -s: считает всё, кроме латинских букв, цифр и пробела; табуляция учитывается. */
static Status mode_s(FILE *const in, FILE *const out) {
    if (!in || !out) return ERR_ARG;
    int c;
    long count = 0;
    bool has_line = false;
    while ((c = fgetc(in)) != EOF) {
        if (c == '\n') {
            if (fprintf(out, "%ld\n", count) < 0) return ERR_FILE;
            count = 0;
            has_line = false;
            continue;
        }
        has_line = true;
        bool is_lat = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        bool is_dig = c >= '0' && c <= '9';
        if (!is_lat && !is_dig && c != ' ') {
            if (count == LONG_MAX) return ERR_OVERFLOW;
            ++count;
        }
    }
    if (ferror(in)) return ERR_FILE;
    if (has_line && fprintf(out, "%ld\n", count) < 0) return ERR_FILE;
    return ferror(out) ? ERR_FILE : OK;
}

/* -a: цифры сохраняет, остальные байты записывает шестнадцатеричными кодами. */
static Status mode_a(FILE *const in, FILE *const out) {
    if (!in || !out) return ERR_ARG;
    int c;
    while ((c = fgetc(in)) != EOF) {
        if (c >= '0' && c <= '9') {
            if (fputc(c, out) == EOF) return ERR_FILE;
        } else if (fprintf(out, "%X", (unsigned int)c) < 0) return ERR_FILE;
    }
    return ferror(in) || ferror(out) ? ERR_FILE : OK;
}

/* Проверяет флаг и пути, запускает режим, записывает результат и закрывает ресурсы. */
int main(const int argc, char *argv[]) {
#ifdef _WIN32
    /* Русские строки исходника записаны в UTF-8. */
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Использование: %s <-d|-i|-s|-a> <вход> или <-nd|-ni|-ns|-na> <вход> <выход>\n", argv[0]);
        return 1;
    }

    const char *const flag = argv[1];
    const size_t flag_len = strlen(flag);
    if ((flag_len != 2 && flag_len != 3) || (flag[0] != '-' && flag[0] != '/')) {
        fprintf(stderr, "Некорректный флаг.\n");
        return 1;
    }
    const bool has_n = flag_len == 3;
    const char mode = flag[has_n ? 2 : 1];
    if ((has_n && flag[1] != 'n') || !strchr("disa", mode) || argc != (has_n ? 4 : 3)) {
        fprintf(stderr, "Некорректный флаг или количество аргументов.\n");
        return 1;
    }

    const char *const in_path = argv[2];
    const char *out_path = has_n ? argv[3] : NULL;
    char *allocated_path = NULL;
    if (!*in_path || (has_n && !*out_path)) {
        fprintf(stderr, "Пути не должны быть пустыми.\n");
        return 1;
    }
    if (!has_n) {
        const size_t length = strlen(in_path);
        size_t dir_len = 0;
        /* Ищем последний разделитель любого вида, в том числе в смешанном пути. */
        for (size_t i = 0; i < length; ++i) {
            if (in_path[i] == '/' || in_path[i] == '\\') dir_len = i + 1;
        }
        if (length > SIZE_MAX - 5) {
            fprintf(stderr, "Слишком длинный путь.\n");
            return 1;
        }
        allocated_path = malloc(length + 5); /* Четыре байта out_ и завершающий ноль. */
        if (!allocated_path) {
            fprintf(stderr, "Ошибка выделения памяти.\n");
            return 1;
        }
        memcpy(allocated_path, in_path, dir_len);
        strcpy(allocated_path + dir_len, "out_");
        strcpy(allocated_path + dir_len + 4, in_path + dir_len);
        out_path = allocated_path;
    }
    /* Явный выход используем прямо из argv: strdup и лишнее выделение не нужны. */
    if (strcmp(in_path, out_path) == 0) {
        fprintf(stderr, "Входной и выходной пути совпадают.\n");
        free(allocated_path);
        return 1;
    }

    FILE *in = fopen(in_path, "r");
    if (!in) {
        fprintf(stderr, "Не удалось открыть входной файл.\n");
        free(allocated_path);
        return 1;
    }
    FILE *temporary = tmpfile();
    Status st = temporary ? OK : ERR_FILE;
    if (st == OK) {
        switch (mode) {
            case 'd': st = mode_d(in, temporary); break;
            case 'i': st = mode_i(in, temporary); break;
            case 's': st = mode_s(in, temporary); break;
            case 'a': st = mode_a(in, temporary); break;
        }
    }
    if (fclose(in) == EOF) st = ERR_FILE;

    /* Открываем выход только после чтения всего входа. Это защищает и от разных
       записей пути к одному файлу. Ошибка окончательной записи всё ещё возможна. */
    if (st == OK && (fflush(temporary) == EOF || fseek(temporary, 0, SEEK_SET) != 0)) st = ERR_FILE;
    FILE *out = NULL;
    if (st == OK) {
        out = fopen(out_path, "w");
        if (!out) st = ERR_FILE;
    }
    if (st == OK) {
        int c;
        while ((c = fgetc(temporary)) != EOF) {
            if (fputc(c, out) == EOF) { st = ERR_FILE; break; }
        }
        if (ferror(temporary)) st = ERR_FILE;
    }
    if (out && fclose(out) == EOF) st = ERR_FILE;
    if (temporary && fclose(temporary) == EOF) st = ERR_FILE;

    if (st == OK) {
        if (printf("Обработка успешно завершена. Результат в: %s\n", out_path) < 0 ||
            fflush(stdout) == EOF) st = ERR_FILE;
    } else if (st == ERR_OVERFLOW) {
        fprintf(stderr, "Слишком много символов в одной строке.\n");
    } else {
        fprintf(stderr, "Ошибка чтения, записи или закрытия файла.\n");
    }
    free(allocated_path);
    return st == OK ? 0 : 1;
}
