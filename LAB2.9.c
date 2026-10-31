#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <math.h>
#include <limits.h>

typedef enum { OK, ERR_ARG, ERR_RANGE, ERR_IO } Status;

/* Наибольший общий делитель: остатки уменьшаются до нуля. */
static Status gcd(unsigned long long a, unsigned long long b, unsigned long long *const result) {
    if (!result) return ERR_ARG;
    while (b) {
        const unsigned long long rest = a % b;
        a = b; b = rest;
    }
    *result = a; return OK;
}

/* Восстанавливает рациональное приближение цепной дробью.
   Например, 0.3333333333333333 распознаётся как 1/3 с заданным eps.
   Исходное выражение по double восстановить однозначно невозможно. */
static Status rational_fraction(const double value, const double eps,
                                 unsigned long long *const denominator) {
    if (!denominator || !isfinite(value) || !isfinite(eps) || eps <= 0 || eps >= 1 ||
        value <= 0 || value >= 1) return ERR_ARG;
    /* Два предыдущих числителя p и знаменателя q очередного приближения. */
    unsigned long long p0 = 0, p1 = 1, q0 = 1, q1 = 0;
    long double part = value;
    while (true) {
        const long double whole = floorl(part);
        if (!isfinite(whole) || whole < 0 || whole >= (long double)ULLONG_MAX) return ERR_RANGE;
        const unsigned long long a = (unsigned long long)whole;
        if ((p1 && a > (ULLONG_MAX - p0) / p1) ||
            (q1 && a > (ULLONG_MAX - q0) / q1)) return ERR_RANGE;
        const unsigned long long p = a * p1 + p0, q = a * q1 + q0;
        if (q && p && p < q) {
            const long double approximation = (long double)p / q;
            if (fabsl(approximation - value) <= (long double)eps * value) {
                unsigned long long divisor;
                gcd(p, q, &divisor);
                *denominator = q / divisor;
                return OK;
            }
        }
        const long double rest = part - whole;
        if (rest <= 0) return ERR_RANGE;
        part = 1.0L / rest;
        p0 = p1; p1 = p; q0 = q1; q1 = q;
    }
}

/* У знаменателя удаляем все множители, входящие в основание. */
static Status finite_fraction(const double value, const int base, const double eps,
                               bool *const result) {
    if (!result || base < 2) return ERR_ARG;
    unsigned long long denominator;
    Status st = rational_fraction(value, eps, &denominator);
    if (st != OK) return st;
    unsigned long long divisor;
    gcd(denominator, (unsigned)base, &divisor);
    while (divisor > 1) {
        denominator /= divisor;
        gcd(denominator, (unsigned)base, &divisor);
    }
    *result = denominator == 1;
    return OK;
}

/* Каждый дополнительный аргумент — double; eps задаёт точность восстановления. */
static Status check_fractions(const int base, const double eps, const int count,
                               bool *const results, ...) {
    if (base < 2 || count <= 0 || !results || !isfinite(eps) || eps <= 0 || eps >= 1) return ERR_ARG;
    va_list args;
    va_start(args, results);
    Status st = OK;
    for (int i = 0; i < count && st == OK; ++i)
        st = finite_fraction(va_arg(args, double), base, eps, &results[i]);
    va_end(args);
    return st;
}

/* В основании 3 дробь 1/3 конечна: её запись равна 0.1. */
int main(void) {
    const double values[] = {0.5, 0.1, 1.0 / 3.0};
    bool result[3];
    Status st = check_fractions(3, 1e-12, 3, result, values[0], values[1], values[2]);
    if (st == OK) for (int i = 0; i < 3; ++i)
        if (printf("%.17g in base 3: %s\n", values[i], result[i] ? "finite" : "infinite") < 0) st = ERR_IO;
    if (st == ERR_ARG) fprintf(stderr, "Error: base must be >= 2, eps in (0,1), fractions finite and in (0,1).\n");
    else if (st == ERR_RANGE) fprintf(stderr, "Error: cannot recover a rational fraction within eps and unsigned long long range.\n");
    else if (st == ERR_IO) fprintf(stderr, "Error: cannot write the result.\n");
    return st != OK;
}
