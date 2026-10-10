#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>

typedef enum { OK = 0, ERR_ARG, ERR_FORMAT, ERR_RANGE, ERR_INPUT } Status;

/* Читает целую лексему; слишком длинную отвергает, а не делит на разные числа. */
static Status read_word(char *const word, const size_t capacity) {
    if (!word || capacity < 2) return ERR_ARG;
    int c;
    do { c = getchar(); } while (c != EOF && isspace((unsigned char)c));
    if (c == EOF) return ERR_INPUT;
    size_t length = 0;
    do {
        if (c == '\0') return ERR_FORMAT;
        if (length == capacity - 1) return ERR_RANGE;
        word[length++] = (char)c;
        c = getchar();
    } while (c != EOF && !isspace((unsigned char)c));
    word[length] = '\0';
    return ferror(stdin) ? ERR_INPUT : OK;
}

/* Значение цифры; по условию буквы в задании 10 должны быть прописными. */
static int char_val(const char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

/* Проверяет цифры выбранной СС и преобразует всё число в long long. */
static Status parse_integer(const char *const str, const int base, long long *const res) {
    if (!str || !res || base < 2 || base > 36) return ERR_ARG;
    size_t i = str[0] == '-' || str[0] == '+' ? 1 : 0;
    if (!str[i]) return ERR_FORMAT;
    for (; str[i]; ++i) {
        const int digit = char_val(str[i]);
        if (digit < 0 || digit >= base) return ERR_FORMAT;
    }
    char *end;
    errno = 0;
    const long long value = strtoll(str, &end, base);
    if (errno == ERANGE) return ERR_RANGE;
    if (end == str || *end) return ERR_FORMAT;
    *res = value;
    return OK;
}

/* Переводит число в основание 2..36; вызывающий код передаёт буфер из 100 байтов. */
static Status format_integer(long long num, const int base, char *const buf) {
    if (!buf || base < 2 || base > 36) return ERR_ARG;
    if (num == 0) { strcpy(buf, "0"); return OK; }
    const bool negative = num < 0;
    /* Храним модуль со знаком минус: -LLONG_MIN не помещается, а -LLONG_MAX помещается. */
    if (num > 0) num = -num;
    char temp[100];
    int length = 0;
    while (num != 0) {
        const int digit = (int)(-(num % base)); /* Отрицательный остаток по модулю меньше base. */
        temp[length++] = (char)(digit < 10 ? '0' + digit : 'A' + digit - 10);
        num /= base;
    }
    int pos = 0;
    if (negative) buf[pos++] = '-';
    while (length > 0) buf[pos++] = temp[--length];
    buf[pos] = '\0';
    return OK;
}

/* Принимает основание и числа до Stop, проверяет сумму и печатает результат. */
int main(void) {
#ifdef _WIN32
    /* Русские строки исходника записаны в UTF-8. */
    SetConsoleOutputCP(CP_UTF8);
#endif
    char word[256];
    long long base_value;
    printf("Введите основание системы счисления [2..36]: ");
    if (read_word(word, sizeof(word)) != OK || parse_integer(word, 10, &base_value) != OK ||
        base_value < 2 || base_value > 36) {
        fprintf(stderr, "Ошибка: некорректное основание.\n");
        return 1;
    }
    const int base = (int)base_value;
    printf("Вводите числа в системе счисления %d. Для завершения введите 'Stop'.\n", base);
    long long sum = 0, maximum = 0, negative_max_magnitude = 0;
    bool first = true;
    while (true) {
        if (read_word(word, sizeof(word)) != OK) {
            fprintf(stderr, "Ошибка чтения, слишком длинная лексема или ввод закончился без Stop.\n");
            return 1;
        }
        if (strcmp(word, "Stop") == 0) break;
        long long current;
        const Status st = parse_integer(word, base, &current);
        if (st != OK) { fprintf(stderr, "Некорректное число или переполнение: ошибка %d.\n", st); return 1; }
        /* Проверяем сложение до операции: signed overflow не определён языком C. */
        if ((current > 0 && sum > LLONG_MAX - current) ||
            (current < 0 && sum < LLONG_MIN - current)) {
            fprintf(stderr, "Сумма не помещается в long long.\n");
            return 1;
        }
        sum += current;
        /* Отрицательный модуль позволяет сравнить даже LLONG_MIN без llabs. */
        const long long negative_magnitude = current > 0 ? -current : current;
        if (first || negative_magnitude < negative_max_magnitude) {
            maximum = current;
            negative_max_magnitude = negative_magnitude;
            first = false;
        }
    }
    if (first) {
        printf("Ни одного корректного числа не было введено.\n");
        return ferror(stdout) || fflush(stdout) == EOF ? 1 : 0;
    }
    const int bases[] = {9, 18, 27, 36};
    char buf[100];
    printf("\nМаксимальное по модулю число: %lld\n", maximum);
    if (format_integer(maximum, base, buf) != OK) return 1;
    printf("Исходная СС %d: %s\n", base, buf);
    for (int i = 0; i < 4; ++i) {
        if (format_integer(maximum, bases[i], buf) != OK) return 1;
        printf("  В системе счисления %2d: %s\n", bases[i], buf);
    }
    printf("\nСумма введенных чисел: %lld\n", sum);
    if (format_integer(sum, base, buf) != OK) return 1;
    printf("Исходная СС %d: %s\n", base, buf);
    for (int i = 0; i < 4; ++i) {
        if (format_integer(sum, bases[i], buf) != OK) return 1;
        printf("  В системе счисления %2d: %s\n", bases[i], buf);
    }
    return ferror(stdout) || fflush(stdout) == EOF ? 1 : 0;
}
