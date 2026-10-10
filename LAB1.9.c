#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <limits.h>
#include <ctype.h>

typedef enum {
    OK = 0,
    ERR_ARG,
    ERR_MEM,
    ERR_RANGE
} Status;

#define FIXED_SIZE 20

/* Разбирает целый int целиком, проверяя знак, цифры и диапазон. */
static Status parse_int(const char *const s, int *const res) {
    if (!s || !*s || !res || isspace((unsigned char)*s)) return ERR_ARG;
    bool negative = *s == '-';
    const char *p = s;
    if (*p == '+' || *p == '-') ++p;
    if (!*p) return ERR_ARG;
    int value = 0;
    const int limit = negative ? INT_MIN : -INT_MAX;
    /* Накапливаем отрицательное число, чтобы поддержать INT_MIN.
       Проверка выполняется до умножения и вычитания цифры. */
    for (; *p; ++p) {
        if (*p < '0' || *p > '9') return ERR_ARG;
        int digit = *p - '0';
        if (value < (limit + digit) / 10) return ERR_RANGE;
        value = value * 10 - digit;
    }
    *res = negative ? value : -value;
    return OK;
}

/* Генерирует число на всём заданном диапазоне int, включая его границы. */
static Status random_in_range(const int min_val, const int max_val, int *const res) {
    if (!res || min_val > max_val) return ERR_ARG;
    /* Вычитание в long long не переполняет 32-битный int. */
    const unsigned long long width = (long long)max_val - min_val + 1;
    const unsigned long long base = (unsigned long long)RAND_MAX + 1;
    unsigned long long span = base;
    unsigned long long value = (unsigned)rand();
    /* Один rand даёт мало значений. Добавляем его результаты как цифры
       числа с основанием RAND_MAX+1, пока не охватим весь диапазон. */
    while (span < width) {
        value = value * base + (unsigned)rand();
        span *= base;
    }
    *res = (int)((long long)min_val + (long long)(value % width));
    return OK;
}

/* Поиск и обмен мин/макс за один проход */
static Status swap_extremes(int *const arr, const int size) {
    if (!arr || size <= 0) return ERR_ARG;

    int min_idx = 0, max_idx = 0;
    for (int i = 1; i < size; ++i) {
        if (arr[i] < arr[min_idx]) min_idx = i;
        if (arr[i] > arr[max_idx]) max_idx = i;
    }

    int temp = arr[min_idx];
    arr[min_idx] = arr[max_idx];
    arr[max_idx] = temp;

    return OK;
}

/* Формирование массива C на основе A и B */
static Status make_array_C(const int *const A, const int size_A, const int *const B, const int size_B, int **const C) {
    if (!A || !B || !C || size_A <= 0 || size_B <= 0) return ERR_ARG;

    *C = NULL;
    if (size_A > 10000 || size_B > 10000) return ERR_RANGE;
    /* По PDF элементы A и B лежат в [-1000,1000]: разность и сумма помещаются в int. */
    for (int i = 0; i < size_A; ++i) if (A[i] < -1000 || A[i] > 1000) return ERR_RANGE;
    for (int i = 0; i < size_B; ++i) if (B[i] < -1000 || B[i] > 1000) return ERR_RANGE;
    *C = malloc((size_t)size_A * sizeof(int));
    if (!*C) return ERR_MEM;

    for (int i = 0; i < size_A; ++i) {
        int closest = B[0];
        int min_diff = abs(A[i] - B[0]);

        for (int j = 1; j < size_B; ++j) {
            int diff = abs(A[i] - B[j]);
            if (diff < min_diff) {
                min_diff = diff;
                closest = B[j];
            }
        }
        (*C)[i] = A[i] + closest;
    }

    return OK;
}

/* Принимает границы, показывает обмен экстремумов и создаёт массивы A, B, C. */
int main(int argc, char *argv[]) {
#ifdef _WIN32
    /* Русские строки исходника записаны в UTF-8. */
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc != 3) {
        printf("Использование: %s <a> <b> (диапазон для массива)\n", argv[0]);
        return 1;
    }

    int a, b;
    if (parse_int(argv[1], &a) != OK || parse_int(argv[2], &b) != OK) {
        printf("Ошибка: аргументы должны быть целыми числами.\n");
        return 1;
    }
    if (a > b) {
        int temp = a; a = b; b = temp; // Меняем местами, если перепутаны
    }

    srand((unsigned int)time(NULL));


    int arr[FIXED_SIZE];
    for (int i = 0; i < FIXED_SIZE; ++i) {
        if (random_in_range(a, b, &arr[i]) != OK) {
            fprintf(stderr, "Ошибка границ случайного диапазона.\n");
            return 1;
        }
    }

    printf("Исходный фиксированный массив:\n");
    for (int i = 0; i < FIXED_SIZE; ++i) printf("%d ", arr[i]);
    printf("\n");

    if (swap_extremes(arr, FIXED_SIZE) != OK) return 1;

    printf("После обмена min и max:\n");
    for (int i = 0; i < FIXED_SIZE; ++i) printf("%d ", arr[i]);
    printf("\n\n");


    int size_A, size_B;
    if (random_in_range(10, 10000, &size_A) != OK || random_in_range(10, 10000, &size_B) != OK) return 1;

    int *A = (int *)malloc(size_A * sizeof(int));
    int *B = (int *)malloc(size_B * sizeof(int));
    int *C = NULL;

    if (!A || !B) {
        printf("Ошибка выделения памяти для массивов A и B.\n");
        free(A); free(B);
        return 1;
    }

    Status st = OK;
    for (int i = 0; i < size_A && st == OK; ++i) st = random_in_range(-1000, 1000, &A[i]);
    for (int i = 0; i < size_B && st == OK; ++i) st = random_in_range(-1000, 1000, &B[i]);
    if (st != OK) { free(A); free(B); return 1; }

    if (make_array_C(A, size_A, B, size_B, &C) != OK) {
        printf("Ошибка формирования массива C.\n");
        free(A); free(B); free(C);
        return 1;
    }

    printf("Сгенерированы массивы:\nРазмер A = %d, Размер B = %d\n", size_A, size_B);
    printf("Массив C успешно сформирован. Первые 5 элементов C:\n");
    for (int i = 0; i < 5 && i < size_A; ++i) {
        printf("%d ", C[i]);
    }
    printf("\n");

    free(A);
    free(B);
    free(C);

    return ferror(stdout) || fflush(stdout) == EOF ? 1 : 0;
}