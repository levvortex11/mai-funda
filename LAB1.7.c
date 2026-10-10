#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

typedef enum {
    OK = 0,
    ERR_ARG,
    ERR_FILE,
    ERR_MEM
} Status;

/* Вспомогательная: перевод символа в строку с кодом в заданной СС (4 или 8) */
static void char_to_base_str(const char c, const int base, char *out_buf) {
    int val = (unsigned char)c;
    char temp[10];
    int idx = 0;

    if (val == 0) {
        out_buf[0] = '0';
        out_buf[1] = '\0';
        return;
    }

    while (val > 0) {
        temp[idx++] = (val % base) + '0';
        val /= base;
    }

    for (int i = 0; i < idx; ++i) {
        out_buf[i] = temp[idx - 1 - i];
    }
    out_buf[idx] = '\0';
}

/* Читает и сразу выводит одно целое слово, не сохраняя его в массиве.
   Номер 1 означает копирование; для -a действуют правила 2, 5 и 10. */
static Status write_word(FILE *in, FILE *out, int number, bool *found, bool *first) {
    int c;
    *found = false;
    do { c = fgetc(in); } while (c != EOF && isspace((unsigned char)c));
    if (c == EOF) return ferror(in) ? ERR_FILE : OK;
    *found = true;
    if (!*first && fputc(' ', out) == EOF) return ERR_FILE;
    *first = false;
    do {
        if (number % 2 == 0) c = tolower((unsigned char)c);
        if (number % 5 == 0) {
            char buf[10];
            char_to_base_str((char)c, number == 10 ? 4 : 8, buf);
            if (fputs(buf, out) == EOF) return ERR_FILE;
        } else if (fputc(c, out) == EOF) return ERR_FILE;
        c = fgetc(in);
    } while (c != EOF && !isspace((unsigned char)c));
    return ferror(in) || ferror(out) ? ERR_FILE : OK;
}

/* -r: поочередно копирует целые слова из двух файлов до конца обоих. */
static Status mode_r(FILE *f1, FILE *f2, FILE *out) {
    if (!f1 || !f2 || !out) return ERR_ARG;
    bool has_w1 = true, has_w2 = true, first = true;
    while (has_w1 || has_w2) {
        Status st = OK;
        if (has_w1) st = write_word(f1, out, 1, &has_w1, &first);
        if (st != OK) return st;
        if (has_w2) st = write_word(f2, out, 1, &has_w2, &first);
        if (st != OK) return st;
    }
    return fputc('\n', out) == EOF || ferror(out) ? ERR_FILE : OK;
}

/* -a: обрабатывает слова по одному; правила повторяются каждые 10 слов. */
static Status mode_a(FILE *in, FILE *out) {
    if (!in || !out) return ERR_ARG;
    bool found = true, first = true;
    int number = 1;
    while (found) {
        Status st = write_word(in, out, number, &found, &first);
        if (st != OK) return st;
        number = number == 10 ? 1 : number + 1;
    }
    return fputc('\n', out) == EOF || ferror(out) ? ERR_FILE : OK;
}

/* Проверяет команду, готовит временный результат и закрывает все открытые потоки. */
int main(int argc, char *argv[]) {
#ifdef _WIN32
    /* Русские строки исходника записаны в UTF-8. */
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc < 4) {
        printf("Ошибка аргументов.\n");
        return 1;
    }

    const char *flag = argv[1];
    if ((flag[0] != '-' && flag[0] != '/') || !flag[1] || flag[2]) {
        printf("Некорректный флаг.\n");
        return 1;
    }

    const bool merge = flag[1] == 'r';
    if ((!merge && flag[1] != 'a') || argc != (merge ? 5 : 4)) {
        fprintf(stderr, "Использование: %s -r <file1> <file2> <out> или -a <in> <out>\n", argv[0]);
        return 1;
    }

    const char *out_path = argv[argc - 1];
    if (strcmp(argv[2], out_path) == 0 || (merge && strcmp(argv[3], out_path) == 0)) {
        fprintf(stderr, "in Выходной путь совпадает с входным.\n");
        return 1;
    }

    FILE *f1 = fopen(argv[2], "r");
    FILE *f2 = NULL;
    if (merge) f2 = fopen(argv[3], "r");
    Status st = !f1 || (merge && !f2) ? ERR_FILE : OK;
    FILE *temporary = NULL;
    if (st == OK) {
        temporary = tmpfile();
        if (!temporary) st = ERR_FILE;
    }
    if (st == OK) st = merge ? mode_r(f1, f2, temporary) : mode_a(f1, temporary);
    if (f1 && fclose(f1) == EOF) st = ERR_FILE;
    if (f2 && fclose(f2) == EOF) st = ERR_FILE;

    /* Выход открываем после полного чтения входов. Разные записи одного пути
       тоже безопасны для чтения: весь результат уже во временном файле. */
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
    /* fclose тоже проверяется: ошибка записи может обнаружиться при сбросе буфера. */
    if (out && fclose(out) == EOF) st = ERR_FILE;
    if (temporary && fclose(temporary) == EOF) st = ERR_FILE;

    if (st != OK) fprintf(stderr, "Ошибка открытия, чтения, записи или закрытия файла.\n");
    else if (printf("Обработка успешно завершена.\n") < 0 || fflush(stdout) == EOF) st = ERR_FILE;
    return (st == OK) ? 0 : 1;
}
