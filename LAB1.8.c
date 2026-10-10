#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

typedef enum {
    OK = 0,
    ERR_ARG,
    ERR_FILE,
    ERR_RANGE,
    ERR_MEM
} Status;


/* Читает целое слово в растущий буфер. Пустая строка означает конец файла.
   Владельцем буфера остается mode_file: он освобождает его и при ошибке. */
static Status read_word(FILE *in, char **word, size_t *capacity) {
    size_t length = 0;
    int c;
    do { c = fgetc(in); } while (c != EOF && isspace((unsigned char)c));
    while (c != EOF && !isspace((unsigned char)c)) {
        if (c == '\0') return ERR_ARG;
        /* Оставляем место для завершающего нуля и проверяем удвоение размера. */
        if (length + 1 >= *capacity) {
            if (*capacity > SIZE_MAX / 2) return ERR_MEM;
            size_t new_capacity = *capacity ? *capacity * 2 : 128;
            char *grown = realloc(*word, new_capacity);
            if (!grown) return ERR_MEM;
            *word = grown;
            *capacity = new_capacity;
        }
        (*word)[length++] = (char)c;
        c = fgetc(in);
    }
    if (*word) (*word)[length] = '\0';
    return ferror(in) ? ERR_FILE : OK;
}

/* Возвращает значение цифры или -1 для недопустимого символа. */
static int char_val(const char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}


/* Определяет основание по цифрам модуля числа; 0 означает неверную запись. */
static int get_min_base(const char *word) {
    if (!word || !*word) return 0;
    int max_val = 0;
    for (size_t i = 0; word[i]; ++i) {
        int v = char_val(word[i]);
        if (v < 0) return 0;
        if (v > max_val) max_val = v;
    }
    return (max_val + 1 < 2) ? 2 : (max_val + 1);
}


/* Пропускает ведущие нули в проверенном модуле числа, сохраняя один ноль. */
static const char* skip_leading_zeros(const char *word) {
    size_t i = 0;
    while (word[i] == '0' && word[i+1] != '\0') {
        i++;
    }

    /* Возвращаем адрес внутри word, новую память не выделяем. */
    return word + i;
}

/* Проверяет числа, определяет основания и выводит результаты в переданный поток. */
static Status mode_file(FILE *in, FILE *out) {
    if (!in || !out) return ERR_ARG;
    char *word = NULL;
    size_t capacity = 0;
    Status st = OK;

    while ((st = read_word(in, &word, &capacity)) == OK && word && *word) {
        int sign_offset = 0;
        int is_negative = 0;

        if (word[0] == '-') { is_negative = 1; sign_offset = 1; }
        else if (word[0] == '+') { sign_offset = 1; }

        /* Ищем минимальное основание по модулю числа */
        int base = get_min_base(word + sign_offset);
        if (base == 0) { st = ERR_ARG; break; }

        /* errno сбрасываем перед вызовом: старое значение не относится к этому числу. */
        char *endptr;
        errno = 0;
        long long dec_val = strtoll(word, &endptr, base);
        if (errno == ERANGE) { st = ERR_RANGE; break; }
        if (endptr == word || *endptr != '\0') { st = ERR_ARG; break; }

        /* Пропускаем нули для вывода */
        const char *clean_word = skip_leading_zeros(word + sign_offset);

        const char *sign = is_negative && clean_word[0] != '0' ? "-" : "";
        if (fprintf(out, "%s%s %d %lld\n", sign, clean_word, base, dec_val) < 0) { st = ERR_FILE; break; }
    }
    free(word);
    return ferror(in) || ferror(out) ? ERR_FILE : st;
}

/* Сначала готовит временный результат, затем открывает выход; закрывает все потоки. */
int main(int argc, char *argv[]) {
#ifdef _WIN32
    /* Русские строки исходника записаны в UTF-8. */
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (argc != 3) {
        printf("Использование: %s <входной_файл> <выходной_файл>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], argv[2]) == 0) {
        fprintf(stderr, "Входной и выходной пути совпадают.\n");
        return 1;
    }
    FILE *in = fopen(argv[1], "r");
    if (!in) {
        printf("Ошибка: не удалось открыть входной файл.\n");
        return 1;
    }

    FILE *temporary = tmpfile();
    Status st = temporary ? mode_file(in, temporary) : ERR_FILE;
    if (fclose(in) == EOF) st = ERR_FILE;

    /* Даже разные пути могут обозначать один файл. Выход открываем только после
       полного чтения входа и проверки всех чисел, чтобы не потерять исходные данные. */
    if (st == OK && (fflush(temporary) == EOF || fseek(temporary, 0, SEEK_SET) != 0)) st = ERR_FILE;
    FILE *out = NULL;
    if (st == OK) {
        out = fopen(argv[2], "w");
        if (!out) st = ERR_FILE;
    }
    if (st == OK) {
        int c;
        while ((c = fgetc(temporary)) != EOF) {
            if (fputc(c, out) == EOF) { st = ERR_FILE; break; }
        }
        if (ferror(temporary)) st = ERR_FILE;
    }
    /* Ошибка буферизованной записи может проявиться только при закрытии. */
    if (out && fclose(out) == EOF) st = ERR_FILE;
    if (temporary && fclose(temporary) == EOF) st = ERR_FILE;

    if (st == ERR_ARG) fprintf(stderr, "Ошибка: некорректная запись числа.\n");
    else if (st == ERR_RANGE) fprintf(stderr, "Ошибка: число не помещается в long long.\n");
    else if (st == ERR_MEM) fprintf(stderr, "Ошибка выделения памяти для слова.\n");
    else if (st == ERR_FILE) fprintf(stderr, "Ошибка открытия, чтения, записи или закрытия файла.\n");
    else if (printf("Конвертация завершена успешно.\n") < 0 || fflush(stdout) == EOF) st = ERR_FILE;

    return (st == OK) ? 0 : 1;
}
