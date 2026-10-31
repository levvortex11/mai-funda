#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

typedef enum { OK, ERR_ARG, ERR_RANGE, ERR_MEM, ERR_IO } Status;

/* Проверяет запись неотрицательного числа и возвращает длину. */
static Status validate_number(const char *const word, const int base, size_t *const length) {
    if (!word || !*word || !length || base < 2 || base > 36) return ERR_ARG;
    size_t n = 0;
    while (word[n]) {
        const char c = word[n];
        const int digit = c >= '0' && c <= '9' ? c - '0' :
            (c >= 'A' && c <= 'Z' ? c - 'A' + 10 :
             (c >= 'a' && c <= 'z' ? c - 'a' + 10 : -1));
        if (digit < 0 || digit >= base) return ERR_ARG;
        ++n;
    }
    *length = n;
    return OK;
}

/* Складывает справа налево по разрядам; сумма не ограничена размером int. */
static Status add_columns(const char *const a, const char *const b,
                          const int base, char **const result) {
    if (!result) return ERR_ARG;
    *result = NULL;
    size_t na, nb;
    Status st = validate_number(a, base, &na);
    if (st == OK) st = validate_number(b, base, &nb);
    if (st != OK) return st;
    const size_t n = na > nb ? na : nb;
    if (n > SIZE_MAX - 2) return ERR_RANGE;
    char *sum = malloc(n + 2);
    if (!sum) return ERR_MEM;
    size_t pos = n + 1;
    sum[pos] = '\0';
    int carry = 0;
    while (na || nb || carry) {
        int digit = carry;
        if (na) {
            const char c = a[--na];
            digit += c <= '9' ? c - '0' : (c <= 'Z' ? c - 'A' + 10 : c - 'a' + 10);
        }
        if (nb) {
            const char c = b[--nb];
            digit += c <= '9' ? c - '0' : (c <= 'Z' ? c - 'A' + 10 : c - 'a' + 10);
        }
        carry = digit / base;
        digit %= base;
        sum[--pos] = (char)(digit < 10 ? '0' + digit : 'A' + digit - 10);
    }
    while (sum[pos] == '0' && sum[pos + 1]) ++pos;
    memmove(sum, sum + pos, n + 2 - pos);
    *result = sum;
    return OK;
}

/* Последовательно складывает variadic-строки; результат принадлежит вызывающему коду. */
static Status sum_numbers(const int base, const int count, char **const result, ...) {
    if (!result || count <= 0 || base < 2 || base > 36) return ERR_ARG;
    *result = NULL;
    char *sum = malloc(2);
    if (!sum) return ERR_MEM;
    sum[0] = '0'; sum[1] = '\0';
    va_list args;
    va_start(args, result);
    Status st = OK;
    for (int i = 0; i < count && st == OK; ++i) {
        const char *word = va_arg(args, const char *);
        char *next = NULL;
        st = add_columns(sum, word, base, &next);
        if (st == OK) { free(sum); sum = next; }
    }
    va_end(args);
    if (st != OK) { free(sum); return st; }
    *result = sum;
    return OK;
}

/* Печатает понятную причину ошибки; вычисления возвращают только Status. */
static void report_error(const Status st) {
    switch (st) {
        case ERR_ARG: fprintf(stderr, "Error: base must be in [2,36], count positive, strings nonempty and all digits smaller than the base; signs are forbidden.\n"); break;
        case ERR_RANGE: fprintf(stderr, "Error: the result string is too large to allocate safely.\n"); break;
        case ERR_MEM: fprintf(stderr, "Error: not enough memory.\n"); break;
        case ERR_IO: fprintf(stderr, "Error: cannot open/read/write/close a file or output stream.\n"); break;
        case OK: break;
        default: fprintf(stderr, "Error: unknown status.\n"); break;
    }
}

/* Показывает сумму чисел, которые уже не помещаются в unsigned long long. */
int main(void) {
    const char *a = "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZz", *b = "1", *c = "0000";
    char *sum = NULL;
    Status st = sum_numbers(36, 3, &sum, a, b, c);
    if (st == OK && puts(sum) == EOF) st = ERR_IO;
    free(sum);
    sum = NULL;
    if (st == OK) st = sum_numbers(16, 2, &sum, (const char *)"FFFF", (const char *)"1");
    if (st == OK && puts(sum) == EOF) st = ERR_IO;
    free(sum);
    if (st != OK) report_error(st);
    return st != OK;
}
