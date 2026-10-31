#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <math.h>
#include <float.h>
#include <stdint.h>

typedef enum { OK, ERR_ARG, ERR_RANGE, ERR_MEM, ERR_IO } Status;
typedef struct { const double *coordinates; size_t dimension; } Vector;
/* Одинаковая сигнатура всех норм: различающиеся параметры передаются через context. */
typedef Status (*Norm)(const Vector *, const void *, double, double *);
typedef struct { const double *values; size_t dimension; } Matrix;
typedef struct { Vector *vectors; size_t count; double maximum; } Longest;

/* Проверяет размерность, координаты и численный допуск. */
static Status validate_vector(const Vector *const vector, const double eps, double *const result) {
    if (!vector || !vector->coordinates || !vector->dimension || !result || !isfinite(eps) || eps <= 0) return ERR_ARG;
    for (size_t i = 0; i < vector->dimension; ++i) if (!isfinite(vector->coordinates[i])) return ERR_ARG;
    return OK;
}

/* Максимум модулей координат; context для этой нормы не нужен. */
static Status norm_inf(const Vector *const vector, const void *const context, const double eps, double *const result) {
    (void)context;
    Status st = validate_vector(vector, eps, result);
    if (st != OK) return st;
    double maximum = 0;
    for (size_t i = 0; i < vector->dimension; ++i) maximum = fmax(maximum, fabs(vector->coordinates[i]));
    *result = maximum; return OK;
}

/* p-норма; масштабирование не даёт промежуточным степеням больших координат переполниться. */
static Status norm_p(const Vector *const vector, const void *const context, const double eps, double *const result) {
    Status st = validate_vector(vector, eps, result);
    if (st != OK || !context) return ERR_ARG;
    const double p = *(const double *)context;
    if (!isfinite(p) || p < 1.0) return ERR_ARG;
    double scale;
    st = norm_inf(vector, NULL, eps, &scale);
    if (st != OK) return st;
    if (scale == 0.0) { *result = 0.0; return OK; }
    long double sum = 0;
    for (size_t i = 0; i < vector->dimension; ++i)
        sum += powl(fabsl((long double)vector->coordinates[i] / scale), p);
    const long double answer = scale * powl(sum, 1.0L / p);
    if (!isfinite(answer) || answer > DBL_MAX) return ERR_RANGE;
    *result = (double)answer; return OK;
}

/* Проверяет симметричную положительно определённую матрицу разложением Холецкого.
   Одновременно считает sqrt(x^T A x) как длину L^T x, без вычитания больших слагаемых. */
static Status norm_matrix(const Vector *const vector, const void *const context, const double eps, double *const result) {
    Status st = validate_vector(vector, eps, result);
    if (st != OK || !context) return ERR_ARG;
    const Matrix *a = context;
    const size_t n = vector->dimension;
    if (!a->values || a->dimension != n) return ERR_ARG;
    if (n > SIZE_MAX / n / sizeof(long double)) return ERR_RANGE;
    for (size_t i = 0; i < n; ++i) for (size_t j = 0; j < n; ++j) {
        if (!isfinite(a->values[i*n+j]) || fabsl((long double)a->values[i*n+j] - a->values[j*n+i]) > eps) return ERR_ARG;
    }
    long double *l = calloc(n*n, sizeof(*l));
    if (!l) return ERR_MEM;
    for (size_t i = 0; i < n && st == OK; ++i) for (size_t j = 0; j <= i; ++j) {
        long double value = ((long double)a->values[i*n+j] + a->values[j*n+i]) / 2;
        for (size_t k = 0; k < j; ++k) value -= l[i*n+k] * l[j*n+k];
        if (!isfinite(value)) { st = ERR_RANGE; break; }
        if (i == j) {
            if (value <= eps) { st = ERR_ARG; break; }
            l[i*n+j] = sqrtl(value);
        } else l[i*n+j] = value / l[j*n+j];
    }
    long double length = 0;
    if (st == OK) for (size_t j = 0; j < n; ++j) {
        long double coordinate = 0;
        for (size_t i = j; i < n; ++i) coordinate += l[i*n+j] * vector->coordinates[i];
        length = hypotl(length, coordinate);
    }
    free(l);
    if (st != OK) return st;
    if (!isfinite(length) || length > DBL_MAX) return ERR_RANGE;
    *result = (double)length; return OK;
}

/* Освобождает результаты; координаты векторов по-прежнему принадлежат вызывающему коду. */
static void free_longest(Longest *const results, const size_t count) {
    for (size_t i = 0; i < count; ++i) free(results[i].vectors);
    free(results);
}

/* В ... сначала идут Vector по значению, затем пары Norm и const void*.
   Сначала находим настоящий максимум, затем выбираем все векторы в пределах eps. */
static Status longest_vectors(const size_t dimension, const double eps, const int vector_count,
                              const int norm_count, Longest **const result, ...) {
    if (!result) return ERR_ARG;
    *result = NULL;
    if (!dimension || vector_count <= 0 || norm_count <= 0 || !isfinite(eps) || eps <= 0) return ERR_ARG;
    if ((size_t)vector_count > SIZE_MAX / sizeof(Vector) || (size_t)vector_count > SIZE_MAX / sizeof(double) ||
        (size_t)norm_count > SIZE_MAX / sizeof(Longest)) return ERR_RANGE;
    Vector *vectors = malloc((size_t)vector_count * sizeof(*vectors));
    double *values = malloc((size_t)vector_count * sizeof(*values));
    Longest *found = calloc((size_t)norm_count, sizeof(*found));
    if (!vectors || !values || !found) { free(vectors); free(values); free(found); return ERR_MEM; }
    Status st = OK;
    va_list args;
    va_start(args, result);
    for (int i = 0; i < vector_count; ++i) {
        vectors[i] = va_arg(args, Vector);
        if (vectors[i].dimension != dimension || validate_vector(&vectors[i], eps, &values[i]) != OK) st = ERR_ARG;
    }
    for (int k = 0; k < norm_count && st == OK; ++k) {
        Norm norm = va_arg(args, Norm);
        const void *context = va_arg(args, const void *);
        if (!norm) { st = ERR_ARG; break; }
        double maximum = 0;
        for (int i = 0; i < vector_count && st == OK; ++i) {
            st = norm(&vectors[i], context, eps, &values[i]);
            if (st == OK && (!isfinite(values[i]) || values[i] < -eps)) st = ERR_RANGE;
            if (st == OK) maximum = fmax(maximum, values[i]);
        }
        if (st != OK) break;
        found[k].vectors = malloc((size_t)vector_count * sizeof(Vector));
        if (!found[k].vectors) { st = ERR_MEM; break; }
        found[k].maximum = maximum;
        for (int i = 0; i < vector_count; ++i)
            if (fabs(values[i] - maximum) <= eps) found[k].vectors[found[k].count++] = vectors[i];
    }
    va_end(args);
    free(vectors); free(values);
    if (st != OK) { free_longest(found, (size_t)norm_count); return st; }
    *result = found; return OK;
}

/* Печатает понятную причину ошибки; вычисления возвращают только Status. */
static void report_error(const Status st) {
    switch (st) {
        case ERR_ARG: fprintf(stderr, "Error: invalid vectors, dimension, eps, p (< 1), or matrix (must be symmetric positive definite).\n"); break;
        case ERR_RANGE: fprintf(stderr, "Error: a norm exceeds double range or array size is too large.\n"); break;
        case ERR_MEM: fprintf(stderr, "Error: not enough memory.\n"); break;
        case ERR_IO: fprintf(stderr, "Error: cannot open/read/write/close a file or output stream.\n"); break;
        case OK: break;
        default: fprintf(stderr, "Error: unknown status.\n"); break;
    }
}

/* Демонстрирует разные максимумы и все равные по норме векторы. */
int main(void) {
    const double x[] = {3, 4}, y[] = {5, 0}, z[] = {-3, -4};
    const Vector a = {x, 2}, b = {y, 2}, c = {z, 2};
    const double p = 2, matrix[] = {2, 0, 0, 1};
    const Matrix m = {matrix, 2};
    Longest *found = NULL;
    Status st = longest_vectors(2, 1e-9, 3, 3, &found, a, b, c,
        (Norm)norm_inf, (const void *)NULL, (Norm)norm_p, (const void *)&p,
        (Norm)norm_matrix, (const void *)&m);
    if (st == OK) {
        for (int k = 0; k < 3; ++k) {
            if (printf("Norm %d, maximum %.12g, vectors %zu\n", k + 1, found[k].maximum, found[k].count) < 0) st = ERR_IO;
            for (size_t i = 0; i < found[k].count; ++i)
                if (printf("  %.12g %.12g\n", found[k].vectors[i].coordinates[0], found[k].vectors[i].coordinates[1]) < 0) st = ERR_IO;
        }
        free_longest(found, 3);
    }
    if (st != OK) report_error(st);
    return st != OK;
}
