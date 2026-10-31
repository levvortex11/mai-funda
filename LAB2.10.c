#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <float.h>
#include <stdint.h>

typedef enum { OK, ERR_ARG, ERR_RANGE, ERR_MEM, ERR_IO } Status;

/* Подставляет x=y+a в схему Горнера. Коэффициенты входа и выхода: от f0 до fn. */
static Status shift_polynomial(const double eps, const double a, double **const result,
                               const int degree, ...) {
    if (!result) return ERR_ARG;
    *result = NULL;
    if (!isfinite(eps) || eps <= 0 || !isfinite(a) || degree < 0) return ERR_ARG;
    const size_t count = (size_t)degree + 1;
    if (count > SIZE_MAX / sizeof(long double) || count > SIZE_MAX / sizeof(double)) return ERR_RANGE;
    long double *f = malloc(count * sizeof(*f));
    long double *g = calloc(count, sizeof(*g));
    double *answer = malloc(count * sizeof(*answer));
    if (!f || !g || !answer) { free(f); free(g); free(answer); return ERR_MEM; }
    va_list args;
    va_start(args, degree);
    Status st = OK;
    for (size_t i = 0; i < count; ++i) {
        f[i] = va_arg(args, double);
        if (!isfinite(f[i])) st = ERR_ARG;
    }
    va_end(args);
    /* Умножаем текущий многочлен на (y+a) и прибавляем следующий коэффициент. */
    for (size_t i = count; i > 0 && st == OK; --i) {
        const size_t used = count - i;
        for (size_t j = used; j > 0; --j) {
            g[j] = g[j - 1] + (long double)a * g[j];
            if (!isfinite(g[j])) st = ERR_RANGE;
        }
        g[0] = (long double)a * g[0] + f[i - 1];
        if (!isfinite(g[0])) st = ERR_RANGE;
    }
    for (size_t i = 0; i < count && st == OK; ++i) {
        if (fabsl(g[i]) > DBL_MAX) st = ERR_RANGE;
        else {
            answer[i] = (double)g[i];
            /* Не обнуляем малые коэффициенты: при больших x они могут быть важны. */
            if (fabsl(g[i] - (long double)answer[i]) > eps) st = ERR_RANGE;
        }
    }
    free(f); free(g);
    if (st != OK) { free(answer); return st; }
    *result = answer;
    return OK;
}

/* Печатает понятную причину ошибки; вычисления возвращают только Status. */
static void report_error(const Status st) {
    switch (st) {
        case ERR_ARG: fprintf(stderr, "Error: eps must be positive and finite, degree nonnegative, and a/all coefficients finite.\n"); break;
        case ERR_RANGE: fprintf(stderr, "Error: a coefficient overflows or loses more than eps when converted to double.\n"); break;
        case ERR_MEM: fprintf(stderr, "Error: not enough memory.\n"); break;
        case ERR_IO: fprintf(stderr, "Error: cannot open/read/write/close a file or output stream.\n"); break;
        case OK: break;
        default: fprintf(stderr, "Error: unknown status.\n"); break;
    }
}

/* Для 1+2x+3x^2 и a=2 получаем 17+14(x-2)+3(x-2)^2. */
int main(void) {
    double *g = NULL;
    Status st = shift_polynomial(0.001, 2.0, &g, 2, 0.05, 0.04, 0.0000000000000000000000000000000000000000000009);
    if (st == OK) for (int i = 0; i <= 2; ++i)
        if (printf("g%d = %.17g\n", i, g[i]) < 0) st = ERR_IO;
    free(g);
    if (st != OK) report_error(st);
    return st != OK;
}
