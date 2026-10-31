#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <limits.h>
#include <stdbool.h>
#include <math.h>

typedef enum { OK, ERR_ARG, ERR_RANGE, ERR_MEM, ERR_IO } Status;

/* Читает всю строку, не ограничивая длину имени фиксированным буфером. */
static Status read_line(FILE *const in, char **const result, bool *const exists) {
    if (!in || !result || !exists) return ERR_ARG;
    *result = NULL; *exists = false;
    size_t size = 128, used = 0;
    char *line = (char *)malloc(size);
    if (!line) return ERR_MEM;
    int c; Status st = OK;
    while ((c = fgetc(in)) != EOF) {
        *exists = true;
        if (c == '\n') break;
        if (c == 0) { st = ERR_ARG; break; }
        if (used == size - 1) {
            if (size > SIZE_MAX / 2) { st = ERR_RANGE; break; }
            char *next = (char *)realloc(line, size * 2);
            if (!next) { st = ERR_MEM; break; }
            line = next; size *= 2;
        }
        line[used++] = (char)c;
    }
    if (ferror(in)) st = ERR_IO;
    if (st != OK) { free(line); return st; }
    line[used] = '\0'; *result = line;
    return OK;
}

/* Разбивает одну строку ровно на count полей, используя её собственную память. */
static Status fields(char *line, char **const words, const size_t count) {
    if (!line || !words) return ERR_ARG;
    size_t n = 0;
    while (*line) {
        while (isspace((unsigned char)*line)) ++line;
        if (!*line) break;
        if (n == count) return ERR_ARG;
        words[n++] = line;
        while (*line && !isspace((unsigned char)*line)) ++line;
        if (*line) *line++ = '\0';
    }
    return n == count ? OK : ERR_ARG;
}

/* Читает неотрицательное целое с проверкой до умножения, без errno. */
static Status parse_unsigned(const char *s, unsigned long long *const result) {
    if (!s || !result) return ERR_ARG;
    if (*s == '+') ++s;
    if (!*s) return ERR_ARG;
    unsigned long long value = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return ERR_ARG;
        const unsigned digit = (unsigned)(*s - '0');
        if (value > (ULLONG_MAX - digit) / 10) return ERR_RANGE;
        value = value * 10 + digit;
    }
    *result = value; return OK;
}

/* Копирует имя в отдельный динамический блок. */
static Status copy_name(const char *const text, char **const result) {
    if (!text || !*text || !result) return ERR_ARG;
    *result = NULL;
    const size_t n = strlen(text);
    if (n == SIZE_MAX) return ERR_RANGE;
    char *copy = (char *)malloc(n + 1);
    if (!copy) return ERR_MEM;
    memcpy(copy, text, n + 1); *result = copy;
    return OK;
}

typedef struct { unsigned long long id; char *name, *surname; double salary; } Employee;

/* Проверяет непустое имя из латинских букв. */
static Status validate_name(const char *s) {
    if (!s || !*s) return ERR_ARG;
    for (; *s; ++s)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z'))) return ERR_ARG;
    return OK;
}

/* Проверяет десятичную зарплату, включая e/E, и конечность преобразования. */
static Status parse_salary(const char *const s, double *const result) {
    if (!s || !result || !*s) return ERR_ARG;
    const char *p = s;
    if (*p == '+') ++p;
    bool digit = false, dot = false, nonzero = false;
    for (; *p && *p != 'e' && *p != 'E'; ++p) {
        if (*p == '.' && !dot) { dot = true; continue; }
        if (*p < '0' || *p > '9') return ERR_ARG;
        digit = true; if (*p != '0') nonzero = true;
    }
    if (!digit) return ERR_ARG;
    if (*p) {
        ++p; if (*p == '+' || *p == '-') ++p;
        if (!*p) return ERR_ARG;
        for (; *p; ++p) if (*p < '0' || *p > '9') return ERR_ARG;
    }
    char *end;
    const double value = strtod(s, &end);
    if (*end || !isfinite(value) || (value == 0.0 && nonzero)) return ERR_RANGE;
    *result = value; return OK;
}

/* Освобождает строки сотрудников и сам массив, включая частично прочитанный. */
static void free_employees(Employee *const people, const size_t count) {
    for (size_t i = 0; i < count; ++i) { free(people[i].name); free(people[i].surname); }
    free(people);
}

/* Загружает и проверяет каждую строку, наращивая массив без потери старого адреса. */
static Status read_employees(FILE *const in, Employee **const result, size_t *const count) {
    if (!in || !result || !count) return ERR_ARG;
    *result = NULL; *count = 0;
    Employee *people = NULL;
    size_t used = 0, capacity = 0;
    Status st = OK;
    size_t line_number = 0;
    while (st == OK) {
        ++line_number;
        char *line = NULL; bool exists;
        st = read_line(in, &line, &exists);
        if (st != OK || !exists) { free(line); break; }
        char *word[4]; Employee person = {0, NULL, NULL, 0};
        st = fields(line, word, 4);
        if (st == OK) st = parse_unsigned(word[0], &person.id);
        if (st == OK) st = validate_name(word[1]);
        if (st == OK) st = validate_name(word[2]);
        if (st == OK) st = parse_salary(word[3], &person.salary);
        if (st == OK) st = copy_name(word[1], &person.name);
        if (st == OK) st = copy_name(word[2], &person.surname);
        if (st == OK && used == capacity) {
            if (capacity > SIZE_MAX / 2 / sizeof(*people)) st = ERR_RANGE;
            else {
                const size_t next_capacity = capacity ? capacity * 2 : 16;
                Employee *next = (Employee *)realloc(people, next_capacity * sizeof(*people));
                if (!next) st = ERR_MEM;
                else { people = next; capacity = next_capacity; }
            }
        }
        if (st == OK) people[used++] = person;
        else {
            fprintf(stderr, "Line %zu: expected nonnegative id, Latin name/surname and nonnegative finite salary.\n", line_number);
            free(person.name); free(person.surname);
        }
        free(line);
    }
    if (st != OK) { free_employees(people, used); return st; }
    *result = people; *count = used; return OK;
}

/* Сравнивает ключи. Для qsort eps=0: порядок обязан быть транзитивным.
   Приблизительное равенство с положительным eps это свойство нарушает. */
static int compare_employee(const Employee *const a, const Employee *const b, const double eps) {
    const long double difference = (long double)a->salary - b->salary;
    if (fabsl(difference) > eps) return difference < 0 ? -1 : 1;
    int order = strcmp(a->surname, b->surname);
    if (order) return order < 0 ? -1 : 1;
    order = strcmp(a->name, b->name);
    if (order) return order < 0 ? -1 : 1;
    return (a->id > b->id) - (a->id < b->id);
}

/* Адаптер стандартного qsort для возрастания, без глобального флага. */
static int ascending(const void *const a, const void *const b) {
    return compare_employee((const Employee *)a, (const Employee *)b, 0.0);
}

/* Адаптер стандартного qsort для убывания по всем ключам. */
static int descending(const void *const a, const void *const b) {
    return compare_employee((const Employee *)b, (const Employee *)a, 0.0);
}

/* Записывает массив; окончательные ошибки буфера проверяет fclose в main. */
static Status write_employees(FILE *const out, const Employee *const people, const size_t count) {
    if (!out || (!people && count)) return ERR_ARG;
    for (size_t i = 0; i < count; ++i)
        if (fprintf(out, "%llu %s %s %.17g\n", people[i].id, people[i].name,
                    people[i].surname, people[i].salary) < 0) return ERR_IO;
    return OK;
}

/* Печатает понятную причину ошибки; вычисления возвращают только Status. */
static void report_error(const Status st) {
    switch (st) {
        case ERR_ARG: fprintf(stderr, "Error: invalid employee record: expected id name surname salary; names must contain Latin letters only.\n"); break;
        case ERR_RANGE: fprintf(stderr, "Error: id, salary or record count exceeds the supported range.\n"); break;
        case ERR_MEM: fprintf(stderr, "Error: not enough memory.\n"); break;
        case ERR_IO: fprintf(stderr, "Error: cannot open/read/write/close a file or output stream.\n"); break;
        case OK: break;
        default: fprintf(stderr, "Error: unknown status.\n"); break;
    }
}

/* Сначала полностью читает вход, затем открывает выход: совпадение путей безопасно. */
int main(int argc, char **argv) {
    if (argc != 4 || (argv[2][0] != '-' && argv[2][0] != '/') ||
        (argv[2][1] != 'a' && argv[2][1] != 'd') || argv[2][2]) {
        fprintf(stderr, "Usage: %s input -a|-d output\n", argv[0]); return 1;
    }
    FILE *in = fopen(argv[1], "r");
    if (!in) { fprintf(stderr, "Error: cannot open input file: %s\n", argv[1]); return 1; }
    Employee *people = NULL; size_t count = 0;
    Status st = read_employees(in, &people, &count);
    if (fclose(in) == EOF) st = ERR_IO;
    if (st == OK && count > 1) qsort(people, count, sizeof(*people), argv[2][1] == 'a' ? ascending : descending);
    if (st == OK) {
        FILE *out = fopen(argv[3], "w");
        if (!out) st = ERR_IO;
        else { st = write_employees(out, people, count); if (fclose(out) == EOF) st = ERR_IO; }
    }
    free_employees(people, count);
    if (st != OK) report_error(st);
    return st != OK;
}
