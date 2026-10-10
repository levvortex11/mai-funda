#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <stdbool.h>
#include <limits.h>

typedef enum {
    OK = 0,
    ERR_ARG,
    ERR_RANGE,
    ERR_PRECISION
} Status;

/* Все вершины должны лежать с одной стороны каждого ребра.
   Вершины передаются в порядке обхода; eps — допуск для координат и cross. */
static Status is_convex(const int v_count, const double eps, bool *const res, ...) {
    if (v_count < 3 || !res || !isfinite(eps) || eps <= 0.0) return ERR_ARG;
    /* Два автоматических массива занимают не более 16 КБ при double=8 байт. */
    if (v_count > 1000) return ERR_RANGE;
    
    va_list args;
    va_start(args, res);
    
    double x[v_count], y[v_count];
    for (int i = 0; i < v_count; ++i) {
        x[i] = va_arg(args, double);
        y[i] = va_arg(args, double);
        if (!isfinite(x[i]) || !isfinite(y[i])) {
            va_end(args);
            return ERR_ARG;
        }
    }
    va_end(args);

    *res = false;
    /* Повторённая вершина не образует корректную границу многоугольника. */
    for (int i = 0; i < v_count; ++i) {
        for (int j = i + 1; j < v_count; ++j) {
            if (fabs(x[i] - x[j]) <= eps && fabs(y[i] - y[j]) <= eps) return OK;
        }
    }
    int sign = 0;
    for (int i = 0; i < v_count; ++i) {
        const int next = (i + 1) % v_count;
        const double dx = x[next] - x[i];
        const double dy = y[next] - y[i];
        for (int j = 0; j < v_count; ++j) {
            if (j == i || j == next) continue;
            const double cross = dx * (y[j] - y[i]) - dy * (x[j] - x[i]);
            if (!isfinite(cross)) return ERR_RANGE;
            if (fabs(cross) <= eps) continue;
            const int current_sign = cross > eps ? 1 : -1;
            if (sign == 0) sign = current_sign;
            else if (sign != current_sign) return OK;
        }
    }
    *res = sign != 0; /* Все точки на одной прямой — не многоугольник. */
    return OK;
}

/* Схема Горнера */
static Status poly_value(const double point, const int n, double *const res, ...) {
    if (n < 0 || !res || !isfinite(point)) return ERR_ARG;
    
    va_list args;
    va_start(args, res);
    
    double current = 0.0;
    for (int i = 0; ; ++i) {
        const double coefficient = va_arg(args, double);
        if (!isfinite(coefficient)) { va_end(args); return ERR_ARG; }
        current = current * point + coefficient;
        if (!isfinite(current)) { va_end(args); return ERR_RANGE; }
        if (i == n) break; /* Не увеличиваем i после последнего коэффициента. */
    }
    va_end(args);
    
    *res = current;
    return OK;
}

/* Среднее геометрическое: exp((log(a1)+...+log(an))/n), ноль обработан отдельно. */
static Status geom_mean(double *const res, const int count, ...) {
    if (!res || count <= 0) return ERR_ARG;

    va_list args;
    va_start(args, count);

    double mean_log = 0.0;
    bool has_zero = false;
    for (int i = 0; i < count; ++i) {
        const double val = va_arg(args, double);
        if (!isfinite(val) || val < 0.0) {
            va_end(args);
            return ERR_ARG;
        }
        if (val == 0.0) has_zero = true;
        /* Произведение могло бы переполниться. Даже после нуля проверяем остальные аргументы. */
        else mean_log += log(val) / count;
    }
    va_end(args);

    const double result = has_zero ? 0.0 : exp(mean_log);
    if (!isfinite(result) || (!has_zero && result == 0.0)) return ERR_RANGE;
    *res = result;
    return OK;
}

/* Быстрая рекурсивная степень, включая отрицательный показатель и INT_MIN. */
static Status fast_pow(const double base, const int exp, double *const res) {
    if (!res || !isfinite(base) || (base == 0.0 && exp < 0)) return ERR_ARG;
    if (exp == 0) {
        *res = 1.0; /* В этой функции 0^0 принято равным 1. */
        return OK;
    }
    double half;
    /* Делим показатель на 2, не меняя его знак: -INT_MIN не представим в int. */
    const Status st = fast_pow(base, exp / 2, &half);
    if (st != OK) return st;
    double result = half * half;
    if (exp % 2 != 0) result *= exp > 0 ? base : 1.0 / base;
    if (!isfinite(result) || (base != 0.0 && result == 0.0)) return ERR_RANGE;
    *res = result;
    return OK;
}

/* Поиск корня методом дихотомии */
typedef double (*eq_func)(double);

/* Ищет корень непрерывной функции; eps задаёт точность по x, а не по f(x). */
static Status find_root(double left, double right, const double eps, eq_func const f, double *const res) {
    if (!res || !f || !isfinite(left) || !isfinite(right) ||
        !isfinite(eps) || eps <= 0.0 || left > right) return ERR_ARG;
    double fl = f(left);
    const double fr = f(right);
    if (!isfinite(fl) || !isfinite(fr)) return ERR_RANGE;
    /* Малое f(x) не гарантирует близость к корню: здесь проверяем именно ноль. */
    if (fl == 0.0) { *res = left; return OK; }
    if (fr == 0.0) { *res = right; return OK; }
    if ((fl < 0.0) == (fr < 0.0)) return ERR_ARG;
    if (!isfinite(right - left)) return ERR_RANGE;
    while (true) {
        const double mid = left + (right - left) / 2.0;
        if (right - left <= eps) { *res = mid; return OK; }
        /* Если середина округлилась до конца, дальнейшее сужение невозможно. */
        if (mid == left || mid == right) return ERR_PRECISION;
        const double fm = f(mid);
        if (!isfinite(fm)) return ERR_RANGE;
        if (fm == 0.0) { *res = mid; return OK; }
        /* Произведение малых значений могло бы округлиться до нуля. */
        if ((fl < 0.0) != (fm < 0.0)) right = mid;
        else { left = mid; fl = fm; }
    }
}


/* Перевод символа в числовое значение цифры (0-9, A-Z, a-z) */
static int char_val(const char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    return -1;
}

/* Разбирает неотрицательное целое в основании 2..36, проверяя переполнение. */
static Status parse_uint_base(const char *str, const int base, unsigned long long *const val) {
    if (!str || !*str || !val || base < 2 || base > 36) return ERR_ARG;

    unsigned long long res = 0;
    for (; *str; ++str) {
        const int v = char_val(*str);
        if (v < 0 || v >= base) return ERR_ARG;
        /* Проверка до умножения и сложения, пока значение ещё не испорчено. */
        if (res > (ULLONG_MAX - (unsigned)v) / (unsigned)base) return ERR_RANGE;
        res = res * (unsigned long long)base + (unsigned long long)v;
    }
    *val = res;
    return OK;
}

/* Перебирает разбиения квадрата на две части; правая часть должна быть ненулевой. */
static Status is_kaprekar(const unsigned long long n, const int base, bool *const res) {
    if (base < 2 || base > 36 || !res) return ERR_ARG;
    if (n == 0) { *res = false; return OK; }

    if (n == 1) { *res = true; return OK; }
    if (n > ULLONG_MAX / n) return ERR_RANGE;
    const unsigned long long sq = n * n;
    *res = false;
    /* base, base^2, ... задают все места разреза, а не только одно. */
    for (unsigned long long divisor = (unsigned)base; divisor <= sq; ) {
        const unsigned long long right = sq % divisor;
        const unsigned long long left = sq / divisor;
        if (right != 0 && left <= n && right == n - left) { *res = true; return OK; }
        if (divisor > sq / (unsigned)base) break;
        divisor *= (unsigned)base; /* Следующий делитель не превышает sq. */
    }
    return OK;
}

/* Заполняет count элементов found; неверные строки и переполнения возвращает как ошибку. */
static Status find_kaprekar_numbers(const int base, const int count, bool *const found, ...) {
    if (base < 2 || base > 36 || count <= 0 || !found) return ERR_ARG;

    va_list args;
    va_start(args, found);

    for (int i = 0; i < count; ++i) {
        const char *const word = va_arg(args, const char *);
        unsigned long long val;
        Status st = parse_uint_base(word, base, &val);
        if (st == OK) st = is_kaprekar(val, base, &found[i]);
        if (st != OK) {
            va_end(args);
            return st;
        }
    }

    va_end(args);
    return OK;
}

/* Первое уравнение для дихотомии: x^2 - 2 = 0. */
static double test_eq(const double x) { return x * x - 2.0; }

/* Второе уравнение: x - 1 = 0, в том числе для корня на границе. */
static double linear_eq(const double x) { return x - 1.0; }

/* Демонстрирует шесть алгоритмов и обрабатывает их ошибки. */
int main(void) {
#ifdef _WIN32
    /* Русские строки исходника записаны в UTF-8. */
    SetConsoleOutputCP(CP_UTF8);
#endif
    bool is_conv;
    Status st = is_convex(4, 1e-9, &is_conv, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0);
    if (st != OK) { fprintf(stderr, "Ошибка выпуклости: %d\n", st); return 1; }
    printf("Квадрат выпуклый: %s\n", is_conv ? "Да" : "Нет");

    double value;
    st = poly_value(2.0, 2, &value, 2.0, 3.0, 1.0);
    if (st != OK) { fprintf(stderr, "Ошибка многочлена: %d\n", st); return 1; }
    printf("2x^2 + 3x + 1 при x=2: %f\n", value);

    st = geom_mean(&value, 3, 6.0, 5.0, 7.0);
    if (st != OK) { fprintf(stderr, "Ошибка среднего: %d\n", st); return 1; }
    printf("Среднее геометрическое 6, 5, 7: %f\n", value);

    st = fast_pow(2.0, 10, &value);
    if (st != OK) { fprintf(stderr, "Ошибка степени: %d\n", st); return 1; }
    printf("2^10 = %f\n", value);
    st = fast_pow(2.0, -3, &value);
    if (st != OK) { fprintf(stderr, "Ошибка степени: %d\n", st); return 1; }
    printf("2^(-3) = %f\n", value);

    st = find_root(0.0, 3.0, 1e-6, test_eq, &value);
    if (st != OK) { fprintf(stderr, "Ошибка дихотомии: %d\n", st); return 1; }
    printf("x^2-2, [0,3], eps=1e-6: %.9f\n", value);
    st = find_root(-2.0, -1.0, 1e-8, test_eq, &value);
    if (st != OK) { fprintf(stderr, "Ошибка дихотомии: %d\n", st); return 1; }
    printf("x^2-2, [-2,-1], eps=1e-8: %.9f\n", value);
    st = find_root(1.0, 4.0, 1e-5, linear_eq, &value);
    if (st != OK) { fprintf(stderr, "Ошибка дихотомии: %d\n", st); return 1; }
    printf("x-1, [1,4], eps=1e-5: %.9f\n", value);

    const char *const nums10[] = {"1", "9", "45", "55", "99", "100", "297", "4879"};
    bool found10[8];
    st = find_kaprekar_numbers(10, 8, found10, nums10[0], nums10[1], nums10[2], nums10[3],
                               nums10[4], nums10[5], nums10[6], nums10[7]);
    if (st != OK) { fprintf(stderr, "Ошибка Капрекара: %d\n", st); return 1; }
    printf("\nЧисла Капрекара в СС 10:\n");
    for (int i = 0; i < 8; ++i) printf("  %s -> %s\n", nums10[i], found10[i] ? "да" : "нет");

    const char *const nums9[] = {"8", "10", "17"};
    bool found9[3];
    st = find_kaprekar_numbers(9, 3, found9, nums9[0], nums9[1], nums9[2]);
    if (st != OK) { fprintf(stderr, "Ошибка Капрекара: %d\n", st); return 1; }
    printf("\nЧисла Капрекара в СС 9:\n");
    for (int i = 0; i < 3; ++i) printf("  %s -> %s\n", nums9[i], found9[i] ? "да" : "нет");
    return ferror(stdout) || fflush(stdout) == EOF ? 1 : 0;
}
