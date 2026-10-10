#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include <limits.h>

typedef enum {
    OK = 0,
    ERR_ARG,
    ERR_CONVERGE
} Status;

/* Читает положительный epsilon, включая 3e-4 и 3E-4. */
static Status parse_double(const char *const s, double *const res) {
    if (!s || !*s || !res) return ERR_ARG;
    double val = 0.0, frac = 0.0, div = 1.0;
    bool dot_seen = false, digit_seen = false;
    int i = 0;
    if (s[i] == '+') ++i;
    for (; s[i] && s[i] != 'e' && s[i] != 'E'; ++i) {
        if (s[i] >= '0' && s[i] <= '9') {
            digit_seen = true;
            if (!dot_seen) val = val * 10.0 + (s[i] - '0');
            else {
                frac = frac * 10.0 + (s[i] - '0');
                div *= 10.0;
            }
        } else if (s[i] == '.' && !dot_seen) dot_seen = true;
        else return ERR_ARG;
    }
    if (!digit_seen || !isfinite(val) || !isfinite(frac) || !isfinite(div)) return ERR_ARG;
    val += frac / div;
    if (s[i] == 'e' || s[i] == 'E') {
        ++i;
        int sign = 1;
        if (s[i] == '-') { sign = -1; ++i; }
        else if (s[i] == '+') ++i;
        if (s[i] < '0' || s[i] > '9') return ERR_ARG;
        /* double вместо int: длинный показатель не вызывает знакового переполнения. */
        double exponent = 0.0;
        for (; s[i]; ++i) {
            if (s[i] < '0' || s[i] > '9') return ERR_ARG;
            exponent = exponent * 10.0 + (s[i] - '0');
        }
        if (!isfinite(exponent)) return ERR_ARG;
        val *= pow(10.0, sign * exponent);
    }
    /* Более строгую точность этот простой набор методов не обещает. */
    if (!isfinite(val) || val < 1e-8) return ERR_ARG;
    *res = val;
    return OK;
}

/* e как предел (1+1/n)^n; ошибка меньше 2/n. */
static Status E_by_limit(const double eps, double *const res) {
    if (!res || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    double n = 1.0, cur;
    do {
        /* log1p сохраняет малую добавку 1/n; формула предела та же. */
        cur = exp(n * log1p(1.0 / n));
        if (2.0 / n <= eps / 4.0) break;
        n *= 2.0;
    } while (true);
    *res = cur;
    return OK;
}

/* e как сумма 1/n!; хвост после члена n не превосходит term/n. */
static Status E_by_series(const double eps, double *const res) {
    if (!res || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    double sum = 1.0, term = 1.0;
    for (int n = 1; ; ++n) {
        term /= n;
        sum += term;
        if (term / n <= eps / 4.0) break;
    }
    *res = sum;
    return OK;
}

/* Ньютон для ln(x)=1; на [2,3] ошибка x не больше 3*|ln(x)-1|. */
static Status E_by_equation(const double eps, double *const res) {
    if (!res || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    double x = 2.0;
    while (3.0 * fabs(log(x) - 1.0) > eps / 4.0) {
        double next_x = x - (log(x) - 1.0) * x;
        if (!isfinite(next_x) || fabs(next_x - x) < eps / 32.0) return ERR_CONVERGE;
        x = next_x;
    }
    *res = x;
    return OK;
}

/* pi как предел произведения Валлиса; ошибка после n множителей меньше 1/n. */
static Status P_by_limit(const double eps, double *const res) {
    if (!res || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    /* Много множителей около 1: long double уменьшает накопленное округление. */
    long double cur = 2.0L;
    for (long long n = 1; ; ++n) {
        cur *= 1.0L + 1.0L / (4.0L * n * n - 1.0L);
        if (1.0L / n <= eps / 2.0L) break;
    }
    *res = (double)cur;
    return OK;
}

/* Ряд Лейбница: ошибка pi не больше 4/(2*n+1), с учётом множителя 4. */
static Status P_by_series(const double eps, double *const res) {
    if (!res || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    long double sum = 0.0L;
    int sign = 1;
    for (long long n = 1; ; ++n) {
        sum += sign / (2.0L * n - 1.0L);
        sign = -sign;
        if (4.0L / (2.0L * n + 1.0L) <= eps / 2.0L) break;
    }
    *res = (double)(4.0L * sum);
    return OK;
}

/* Корень sin(x)=0 на [2,4] делением отрезка пополам. */
static Status P_by_equation(const double eps, double *const res) {
    if (!res || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    double l = 2.0, r = 4.0;
    while (r - l > eps / 2.0) {
        double mid = (l + r) / 2.0;
        if (sin(mid) > 0.0) l = mid;
        else r = mid;
    }
    *res = (l + r) / 2.0;
    return OK;
}

/* ln(2) как предел n*(2^(1/n)-1); ошибка меньше 1/n. */
static Status LN2_by_limit(const double eps, double *const res) {
    if (!res || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    double n = 1.0, cur;
    do {
        /* expm1 вычисляет exp(y)-1 без вычитания почти равных чисел. */
        cur = n * expm1(log(2.0) / n);
        if (1.0 / n <= eps / 4.0) break;
        n *= 2.0;
    } while (true);
    *res = cur;
    return OK;
}

/* Знакочередующийся ряд ln(2); хвост не больше 1/(n+1). */
static Status LN2_by_series(const double eps, double *const res) {
    if (!res || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    long double sum = 0.0L;
    int sign = 1;
    for (long long n = 1; ; ++n) {
        sum += (long double)sign / n;
        sign = -sign;
        if (1.0L / (n + 1) <= eps / 2.0L) break;
    }
    *res = (double)sum;
    return OK;
}

/* Ньютон для exp(x)=2; на [0,1] ошибка не больше |exp(x)-2|. */
static Status LN2_by_equation(const double eps, double *const res) {
    if (!res || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    double x = 1.0;
    while (fabs(exp(x) - 2.0) > eps / 4.0) {
        double next_x = x - (exp(x) - 2.0) / exp(x);
        if (!isfinite(next_x) || fabs(next_x - x) < eps / 32.0) return ERR_CONVERGE;
        x = next_x;
    }
    *res = x;
    return OK;
}

/* sqrt(2) как предел x_next=x-x*x/2+1, начало -0.5. */
static Status SQRT2_by_limit(const double eps, double *const res) {
    if (!res || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    double x = -0.5;
    do {
        double next_x = x - x * x / 2.0 + 1.0;
        if (!isfinite(next_x)) return ERR_CONVERGE;
        x = next_x;
        /* После первого шага x>0: |x-sqrt(2)| < |x*x-2|. */
    } while (fabs(x * x - 2.0) > eps / 4.0);
    *res = x;
    return OK;
}

/* Произведение 2^(1/4)*2^(1/8)*... с проверкой всего хвоста показателей. */
static Status SQRT2_by_product(const double eps, double *const res) {
    if (!res || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    double prod = 1.0, exp_val = 0.25;
    /* Сумма ещё не использованных показателей равна 2*exp_val. */
    while (prod * expm1(2.0 * exp_val * log(2.0)) > eps / 4.0) {
        prod *= pow(2.0, exp_val);
        exp_val /= 2.0;
    }
    *res = prod;
    return OK;
}

/* Решает x*x=2 методом Ньютона, формула Герона. */
static Status SQRT2_by_equation(const double eps, double *const res) {
    if (!res || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    double x = 1.0;
    while (fabs(x * x - 2.0) > eps / 4.0) {
        double next_x = 0.5 * (x + 2.0 / x);
        if (!isfinite(next_x) || fabs(next_x - x) < eps / 32.0) return ERR_CONVERGE;
        x = next_x;
    }
    *res = x;
    return OK;
}

/* gamma как предел H_n-ln(n)-1/(2n); ошибка меньше 1/(12*n*n). */
static Status G_by_limit(const double eps, double *const res) {
    if (!res || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    double sum = 0.0;
    for (int n = 1; ; ++n) {
        sum += 1.0 / n;
        if (1.0 / (12.0 * n * n) <= eps / 4.0) {
            *res = sum - log((double)n) - 1.0 / (2.0 * n);
            return OK;
        }
    }
}

/* gamma = 1/2 + сумма [1/(2n)+1/(2(n+1))-ln(1+1/n)]. */
static Status G_by_series(const double eps, double *const res) {
    if (!res || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    double sum = 0.5;
    for (int n = 1; ; ++n) {
        sum += 0.5 / n + 0.5 / (n + 1.0) - log1p(1.0 / n);
        /* Оценка всей оставшейся суммы, а не одного слагаемого. */
        if (1.0 / (12.0 * (n + 1.0) * (n + 1.0)) <= eps / 4.0) break;
    }
    *res = sum;
    return OK;
}

/* Решает x+I=0, где I=интеграл exp(-t)*ln(t) от 0 до бесконечности. */
static Status G_by_equation(const double eps, double *const res) {
    if (!res || !isfinite(eps) || eps < 1e-8) return ERR_ARG;
    /* При T>=20 хвост < exp(-T)*(T+1); делаем его меньше eps/16. */
    double end_t = 20.0;
    while (exp(-end_t) * (end_t + 1.0) > eps / 16.0) end_t += 1.0;
    double right = sqrt(sqrt(end_t));
    double prev = 0.0, integral = 0.0;
    int n = 16;
    do {
        prev = integral;
        double h = right / n, sum = 0.0;
        /* t=u^4: функция 16*u^3*exp(-u^4)*ln(u) имеет предел 0 при u=0.
           Симпсон: веса 1,4,2,...,4,1. Нулевой узел не вычисляем, его вклад 0. */
        for (int i = 1; i <= n; ++i) {
            double u = h * i;
            double value = 16.0 * u * u * u * exp(-u * u * u * u) * log(u);
            int weight = (i == n) ? 1 : ((i % 2) ? 4 : 2);
            sum += weight * value;
        }
        integral = h * sum / 3.0;
        if (!isfinite(integral)) return ERR_CONVERGE;
        if (n > 16 && fabs(integral - prev) <= eps / 16.0) break;
        /* Это граница типа счётчика, а не произвольный лимит точности. */
        if (n > INT_MAX / 2) return ERR_CONVERGE;
        n *= 2;
    } while (true);

    /* Разность сеток — практическая проверка с запасом, не строгая оценка
       для произвольных интегралов. Точность этой формулы проверяем по эталону. */
    double l = 0.0, r = 1.0;
    if (integral > 0.0 || integral < -1.0) return ERR_CONVERGE;
    while (r - l > eps / 2.0) {
        double mid = (l + r) / 2.0;
        if (mid + integral < 0.0) l = mid;
        else r = mid;
    }
    *res = (l + r) / 2.0;
    return OK;
}

/* Проверяет ввод и статусы трёх методов, выводит пять строк результатов. */
int main(int argc, char *argv[]) {
#ifdef _WIN32
    /* Русские строки исходника записаны в UTF-8. */
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc != 2) {
        printf("Использование: %s <эпсилон, например 3e-4>\n", argv[0]);
        return 1;
    }
    double eps;
    if (parse_double(argv[1], &eps) != OK) {
        printf("Ошибка: нужен конечный эпсилон не меньше 1e-8.\n");
        return 1;
    }
    double v1, v2, v3;

    /* e */
    if (E_by_limit(eps, &v1) != OK || E_by_series(eps, &v2) != OK || E_by_equation(eps, &v3) != OK) {
        printf("Ошибка вычисления e.\n"); return 1;
    }
    printf("e:       предел = %.12f, ряд = %.12f, уравнение = %.12f\n", v1, v2, v3);
    /* pi */
    if (P_by_limit(eps, &v1) != OK || P_by_series(eps, &v2) != OK || P_by_equation(eps, &v3) != OK) {
        printf("Ошибка вычисления pi.\n"); return 1;
    }
    printf("pi:      предел = %.12f, ряд = %.12f, уравнение = %.12f\n", v1, v2, v3);
    /* ln 2 */
    if (LN2_by_limit(eps, &v1) != OK || LN2_by_series(eps, &v2) != OK || LN2_by_equation(eps, &v3) != OK) {
        printf("Ошибка вычисления ln 2.\n"); return 1;
    }
    printf("ln 2:    предел = %.12f, ряд = %.12f, уравнение = %.12f\n", v1, v2, v3);
    /* sqrt(2) */
    if (SQRT2_by_limit(eps, &v1) != OK || SQRT2_by_product(eps, &v2) != OK || SQRT2_by_equation(eps, &v3) != OK) {
        printf("Ошибка вычисления sqrt(2).\n"); return 1;
    }
    printf("sqrt(2): предел = %.12f, произведение = %.12f, уравнение = %.12f\n", v1, v2, v3);
    /* gamma */
    if (G_by_limit(eps, &v1) != OK || G_by_series(eps, &v2) != OK || G_by_equation(eps, &v3) != OK) {
        printf("Ошибка вычисления gamma.\n"); return 1;
    }
    printf("gamma:   предел = %.12f, ряд = %.12f, уравнение = %.12f\n", v1, v2, v3);
    return 0;
}
