#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <ctype.h>
#include <float.h>

/* Лимиты ограничивают время работы, достижение лимита не считается успехом. */
#define SERIES_LIMIT 1000000
#define GRID_LIMIT 134217728

typedef enum { OK = 0, ERR_ARG, ERR_CONVERGE, ERR_RANGE } Status;

/* Разбирает всю строку, включая e/E; отвергает пустоту, хвост и выход из диапазона. */
static Status parse_double(const char *const s, double *const res) {
    if (!s || !*s || !res || isspace((unsigned char)*s)) return ERR_ARG;
    char *end;
    const double value = strtod(s, &end);
    if (end == s || *end || !isfinite(value)) return ERR_ARG;
    /* Слишком маленькое ненулевое число не принимаем за точный ноль.
       Проверяем значащую часть отдельно от показателя степени. */
    const char *p = s;
    if (*p == '+' || *p == '-') ++p;
    bool hex = p[0] == '0' && (p[1] == 'x' || p[1] == 'X');
    if (hex) p += 2;
    bool nonzero = false;
    for (; *p && (hex ? (*p != 'p' && *p != 'P') : (*p != 'e' && *p != 'E')); ++p) {
        if (*p != '0' && *p != '.') nonzero = true;
    }
    if ((value == 0.0 && nonzero) || (value != 0.0 && fabs(value) < DBL_MIN)) return ERR_ARG;
    *res = value;
    return OK;
}

/* Ряд A: следующий член получается умножением на x/n; оцениваем весь хвост. */
static Status A_by_series(const double x, const double eps, double *const res) {
    if (!res || !isfinite(x) || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    long double sum = 1.0L, term = 1.0L, absolute_sum = 1.0L;
    for (int n = 1; n <= SERIES_LIMIT; ++n) {
        term *= (long double)x / n;
        sum += term;
        absolute_sum += fabsl(term);
        if (!isfinite(sum) || !isfinite(absolute_sum) || fabsl(sum) > DBL_MAX) return ERR_RANGE;
        const double ratio = fabs(x) / (n + 1.0);
        if (ratio < 1.0 && fabsl(term) * ratio / (1.0 - ratio) <= eps / 2.0) {
            /* Большие взаимно сокращающиеся члены могут потерять точность. */
            if (8.0L * LDBL_EPSILON * (n + 1.0L) * absolute_sum + DBL_EPSILON * fabsl(sum) > eps / 2.0L) return ERR_CONVERGE;
            *res = (double)sum;
            return OK;
        }
    }
    return ERR_CONVERGE;
}

/* Ряд B: после начала убывания модулей ошибка не больше следующего члена. */
static Status B_by_series(const double x, const double eps, double *const res) {
    if (!res || !isfinite(x) || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    long double sum = 1.0L, term = 1.0L, absolute_sum = 1.0L;
    for (int n = 1; n <= SERIES_LIMIT; ++n) {
        term *= -(long double)x * x / ((2.0 * n - 1.0) * (2.0 * n));
        sum += term;
        absolute_sum += fabsl(term);
        if (!isfinite(sum) || !isfinite(absolute_sum) || fabsl(sum) > DBL_MAX) return ERR_RANGE;
        const long double ratio = (long double)x * x / ((2.0 * n + 1.0) * (2.0 * n + 2.0));
        if (ratio < 1.0 && fabsl(term) * ratio <= eps / 2.0) {
            if (8.0L * LDBL_EPSILON * (n + 1.0L) * absolute_sum + DBL_EPSILON * fabsl(sum) > eps / 2.0L) return ERR_CONVERGE;
            *res = (double)sum;
            return OK;
        }
    }
    return ERR_CONVERGE;
}

/* Ряд C: |x|<1; убывающие отношения членов дают оценку геометрического хвоста. */
static Status C_by_series(const double x, const double eps, double *const res) {
    if (!res || !isfinite(x) || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    if (fabs(x) >= 1.0) return ERR_CONVERGE;
    double sum = 1.0, term = 1.0;
    for (int n = 1; n <= SERIES_LIMIT; ++n) {
        term *= 27.0 * n * n * n * x * x / ((3.0*n) * (3.0*n-1.0) * (3.0*n-2.0));
        sum += term;
        if (!isfinite(sum)) return ERR_RANGE;
        const double k = n + 1.0;
        const double ratio = 27.0*k*k*k*x*x / ((3.0*k)*(3.0*k-1.0)*(3.0*k-2.0));
        if (ratio < 1.0 && term * ratio / (1.0 - ratio) <= eps / 2.0) {
            /* У границы |x|=1 сумма велика: оставляем запас и на округление накопления. */
            if (8.0 * DBL_EPSILON * (n + 1.0) * sum > eps / 2.0) return ERR_CONVERGE;
            *res = sum;
            return OK;
        }
    }
    return ERR_CONVERGE;
}

/* Ряд D из PDF, начиная с n=1. Для сдачи используем только |x|<1. */
static Status D_by_series(const double x, const double eps, double *const res) {
    if (!res || !isfinite(x) || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    if (fabs(x) >= 1.0) return ERR_ARG;
    double sum = 0.0, term = 1.0;
    for (int n = 1; n <= SERIES_LIMIT; ++n) {
        term *= -x*x * (2.0*n-1.0) / (2.0*n);
        sum += term;
        const double next = -term * x*x * (2.0*n+1.0) / (2.0*n+2.0);
        /* По признаку Лейбница ошибка частичной суммы не больше
           модуля первого отброшенного члена. Поправок к сумме нет. */
        if (fabs(next) <= eps / 2.0) {
            *res = sum;
            return OK;
        }
    }
    return ERR_CONVERGE;
}

typedef double (*func_ptr)(double);

/* Подынтегральная функция A из PDF: ln(1+x)/x; предел в нуле равен 1. */
static double integrand_a(const double x) { return x == 0.0 ? 1.0 : log(1.0 + x) / x; }
/* Подынтегральная функция B. */
static double integrand_b(const double x) { return exp(-x*x / 2.0); }
/* Подынтегральная функция C из PDF: ln(1/(1-x)).
   При x=1 особенность: средние прямоугольники не касаются концов. */
static double integrand_c(const double x) { return log(1.0 / (1.0 - x)); }
/* Подынтегральная функция D; предел при x -> 0 равен 1. */
static double integrand_d(const double x) { return x == 0.0 ? 1.0 : pow(x, x); }

/* Средние прямоугольники на всём отрезке; сравниваем две сетки с запасом по eps. */
static Status integral_by_rectangles(func_ptr const f, const double a, const double b,
                                     const double eps, double *const res) {
    if (!f || !res || !isfinite(a) || !isfinite(b) || a > b ||
        !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    if (a == b) { *res = 0.0; return OK; }
    if (!isfinite(b - a)) return ERR_RANGE;
    double previous = 0.0;
    for (int n = 16; n <= GRID_LIMIT; n *= 2) {
        const double h = (b - a) / n;
        long double sum = 0.0L;
        for (int i = 0; i < n; ++i) {
            const double x = a + h * (i + 0.5);
            if (x == a || x == b) return ERR_CONVERGE;
            const double value = f(x);
            if (!isfinite(value)) return ERR_RANGE;
            sum += (long double)value * h;
        }
        const double current = (double)sum;
        if (!isfinite(current)) return ERR_RANGE;
        if (n > 16 && fabs(current - previous) <= eps / 2.0) { *res = current; return OK; }
        previous = current;
    }
    return ERR_CONVERGE;
}

/* Выводит ответ только при успехе; ошибку расчёта или вывода передаёт в main. */
static int print_result(const char *const label, const Status st, const double value) {
    if (st != OK) {
        fprintf(stderr, "%s: ошибка %d (1 — аргументы, 2 — сходимость/лимит, 3 — диапазон).\n", label, st);
        return 1;
    }
    return printf("%s: %.12f\n", label, value) < 0;
}

/* Принимает eps из argv и x из stdin, показывает все ряды и интегралы. */
int main(int argc, char *argv[]) {
#ifdef _WIN32
    /* Русские строки исходника записаны в UTF-8. */
    SetConsoleOutputCP(CP_UTF8);
#endif
    double eps;
    if (argc != 2 || parse_double(argv[1], &eps) != OK || eps < 1e-8) {
        fprintf(stderr, "Использование: %s <конечный eps >= 1e-8>\n", argv[0]);
        return 1;
    }
    char input[256];
    double x;
    printf("Введите значение x для вычисления рядов: ");
    if (!fgets(input, sizeof(input), stdin)) return 1;
    /* fgets читает всю строку: хвост после числа не должен молча игнорироваться. */
    size_t length = 0;
    while (input[length] && input[length] != '\n') ++length;
    if (!input[length] && !feof(stdin)) { fprintf(stderr, "Слишком длинный ввод.\n"); return 1; }
    input[length] = '\0';
    if (length && input[length-1] == '\r') input[--length] = '\0';
    if (parse_double(input, &x) != OK) { fprintf(stderr, "Ошибка ввода x.\n"); return 1; }
    int failed = 0;
    double result = 0.0;
    Status st;
    st = A_by_series(x, eps, &result); failed |= print_result("Ряд A", st, result);
    st = B_by_series(x, eps, &result); failed |= print_result("Ряд B", st, result);
    st = C_by_series(x, eps, &result); failed |= print_result("Ряд C", st, result);
    if (fabs(x) >= 1.0) {
        fprintf(stderr, "Ряд D: для этой версии требуется |x| < 1.\n");
        failed = 1;
    } else {
        st = D_by_series(x, eps, &result); failed |= print_result("Ряд D", st, result);
    }
    /* Средние прямоугольники не касаются концов, поэтому обрезать [0,1] не нужно. */
    st = integral_by_rectangles(integrand_a, 0.0, 1.0, eps, &result); failed |= print_result("Интеграл A", st, result);
    st = integral_by_rectangles(integrand_b, 0.0, 1.0, eps, &result); failed |= print_result("Интеграл B", st, result);
    st = integral_by_rectangles(integrand_c, 0.0, 1.0, eps, &result); failed |= print_result("Интеграл C", st, result);
    st = integral_by_rectangles(integrand_d, 0.0, 1.0, eps, &result); failed |= print_result("Интеграл D", st, result);
    return failed || ferror(stdout) || fflush(stdout) == EOF ? 1 : 0;
}
