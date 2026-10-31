/* MinGW: стандартные форматы C, включая настоящий long double. */
#if defined(__MINGW32__) && !defined(__USE_MINGW_ANSI_STDIO)
#define __USE_MINGW_ANSI_STDIO 1
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>
#include <wchar.h>
#include <math.h>
#include <float.h>

typedef enum { OK, ERR_ARG, ERR_RANGE, ERR_MEM, ERR_IO } Status;

/* Читает int в указанном основании, накапливая отрицательное значение для INT_MIN. */
static Status parse_base(const char *s, int base, const bool upper, int *const result) {
    if (!s || !*s || !result) return ERR_ARG;
    if (base < 2 || base > 36) base = 10;
    const bool negative = *s == '-';
    if (*s == '+' || *s == '-') ++s;
    if (!*s) return ERR_ARG;
    const int limit = negative ? INT_MIN : -INT_MAX;
    int value = 0;
    for (; *s; ++s) {
        int digit = *s >= '0' && *s <= '9' ? *s - '0' : -1;
        if (upper && *s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
        if (!upper && *s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
        if (digit < 0 || digit >= base) return ERR_ARG;
        if (value < (limit + digit) / base) return ERR_RANGE;
        value = value * base - digit;
    }
    *result = negative ? value : -value;
    return OK;
}

/* Обычная запись до 3999; скобки умножают группу на 1000: (V) = 5000.
   Внутри каждой группы I, X, C, M не повторяются больше трёх раз подряд. */
static Status roman_text(const int value, char **const result) {
    if (!result) return ERR_ARG;
    *result = NULL;
    unsigned magnitude = value < 0 ? 0u - (unsigned)value : (unsigned)value;
    char *thousands = NULL;
    if (magnitude >= 4000) {
        Status st = roman_text((int)(magnitude / 1000u), &thousands);
        if (st != OK) return st;
        magnitude %= 1000u;
    }
    const size_t high_length = thousands ? strlen(thousands) : 0;
    if (high_length > SIZE_MAX - 32) { free(thousands); return ERR_RANGE; }
    char *text = malloc(high_length + 32);
    if (!text) { free(thousands); return ERR_MEM; }
    size_t pos = 0;
    if (value < 0) text[pos++] = '-';
    if (thousands) {
        text[pos++] = '(';
        memcpy(text + pos, thousands, high_length); pos += high_length;
        text[pos++] = ')';
    }
    free(thousands);
    const unsigned values[] = {1000,900,500,400,100,90,50,40,10,9,5,4,1};
    const char *const symbols[] = {"M","CM","D","CD","C","XC","L","XL","X","IX","V","IV","I"};
    if (!value) text[pos++] = 'N';
    for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
        while (magnitude >= values[i]) {
            for (size_t j = 0; symbols[i][j]; ++j) text[pos++] = symbols[i][j];
            magnitude -= values[i];
        }
    text[pos] = '\0'; *result = text; return OK;
}

/* Значение римской цифры, ноль для остальных символов. */
static int roman_digit(const int c) {
    switch (c) {
        case 'I': return 1; case 'V': return 5; case 'X': return 10;
        case 'L': return 50; case 'C': return 100; case 'D': return 500; case 'M': return 1000;
        default: return 0;
    }
}

/* Читает римское целое, включая скобочные группы тысяч, N и знак минус. */
static Status parse_roman(const char *text, int *const result) {
    if (!text || !*text || !result) return ERR_ARG;
    const char *original = text;
    const bool negative = *text == '-';
    if (negative) ++text;
    if (!strcmp(text,"N") && !negative) { *result = 0; return OK; }
    if (!*text) return ERR_ARG;
    long long sum = 0, multiplier = 1;
    const long long limit = negative ? -(long long)INT_MIN : INT_MAX;
    for (size_t i = 0; text[i]; ++i) {
        if (text[i] == '(') {
            if (multiplier > limit / 1000) return ERR_RANGE;
            multiplier *= 1000; continue;
        }
        if (text[i] == ')') {
            if (multiplier == 1) return ERR_ARG;
            multiplier /= 1000; continue;
        }
        const int current = roman_digit((unsigned char)text[i]);
        const int next = roman_digit((unsigned char)text[i+1]);
        if (!current) return ERR_ARG;
        sum += (current < next ? -current : current) * multiplier;
        if (sum > limit || sum < -limit) return ERR_RANGE;
    }
    if (multiplier != 1 || sum < 0 || sum > limit) return ERR_RANGE;
    const int value = (int)(negative ? -sum : sum);
    char *canonical = NULL;
    Status st = roman_text(value,&canonical);
    if (st == OK && strcmp(original,canonical)) st = ERR_ARG;
    free(canonical);
    if (st == OK) *result = value;
    return st;
}

/* Убирает завершающую единицу и складывает отмеченные числа Фибоначчи. */
static Status parse_zeckendorf(const char *const text, unsigned *const result) {
    if (!text || !result) return ERR_ARG;
    const size_t n = strlen(text);
    if (!strcmp(text,"01")) { *result = 0; return OK; }
    if (n < 2 || text[n-1] != '1' || text[n-2] != '1') return ERR_ARG;
    unsigned a = 1, b = 2, sum = 0;
    for (size_t i = 0; i < n-1; ++i) {
        if (text[i] != '0' && text[i] != '1') return ERR_ARG;
        if (text[i] == '1') {
            if (i && text[i-1] == '1') return ERR_ARG;
            if (sum > UINT_MAX - a) return ERR_RANGE;
            sum += a;
        }
        if (i + 1 < n-1) {
            /* Последнее нужное число может помещаться, даже если следующее уже нет. */
            const unsigned next = a <= UINT_MAX-b ? a+b : 0;
            a=b; b=next;
            if (!a) return ERR_RANGE;
        }
    }
    *result = sum; return OK;
}

/* Добавляет один символ к динамической лексеме. */
static Status push_char(char **const text, size_t *const length, size_t *const capacity, const int c) {
    if (*length + 1 >= *capacity) {
        if (*capacity > SIZE_MAX/2) return ERR_RANGE;
        const size_t next_size = *capacity ? *capacity*2 : 32;
        char *next = realloc(*text,next_size);
        if (!next) return ERR_MEM;
        *text=next; *capacity=next_size;
    }
    (*text)[(*length)++] = (char)c; (*text)[*length]='\0'; return OK;
}

/* Читает символы пользовательского числа, оставляя первый разделитель в потоке. */
static Status custom_word(FILE *const in, const char kind, const int base, const bool upper,
                          char **const result, size_t *const consumed, bool *const at_eof) {
    *result=NULL; *at_eof=false;
    int c;
    while ((c=fgetc(in)) != EOF && isspace((unsigned char)c)) {
        if (*consumed == INT_MAX) return ERR_RANGE;
        ++*consumed;
    }
    if (c == EOF) { *at_eof=true; return ferror(in) ? ERR_IO : OK; }
    size_t n=0, capacity=0; Status st=OK;
    while (c != EOF) {
        bool valid=false;
        if (kind=='R') valid=roman_digit(c)!=0 || c=='(' || c==')' || (n==0 && (c=='-' || c=='N'));
        else if (kind=='Z') valid=c=='0' || c=='1';
        else {
            int digit=c>='0' && c<='9' ? c-'0' : -1;
            if (upper && c>='A' && c<='Z') digit=c-'A'+10;
            if (!upper && c>='a' && c<='z') digit=c-'a'+10;
            valid=(digit>=0 && digit<base) || (n==0 && (c=='+' || c=='-'));
        }
        if (!valid) { if (ungetc(c,in)==EOF) st=ERR_IO; break; }
        if (*consumed == INT_MAX) { st=ERR_RANGE; break; }
        ++*consumed;
        st=push_char(result,&n,&capacity,c);
        if (st!=OK) break;
        /* Две единицы подряд завершают ненулевую запись Фибоначчи. */
        if (kind=='Z' && n>=2 && (*result)[n-1]=='1' && (*result)[n-2]=='1') break;
        c=fgetc(in);
    }
    if (ferror(in)) st=ERR_IO;
    if (st!=OK) { free(*result); *result=NULL; }
    return st;
}

/* Разбирает стандартный scanf-формат, включая ширину, подавление и набор %[...]. */
static Status scan_spec(const char *const start, const char **const end, char length[3],
                        char *const conversion, bool *const suppress) {
    const char *p=start+1;
    *suppress=*p=='*'; if (*suppress) ++p;
    unsigned width=0; bool has_width=false;
    while (isdigit((unsigned char)*p)) {
        has_width=true;
        if (width > ((unsigned)INT_MAX-(unsigned)(*p-'0'))/10) return ERR_RANGE;
        width=width*10+(unsigned)(*p++-'0');
    }
    if (has_width && !width) return ERR_ARG;
    length[0]=length[1]=length[2]='\0';
    if (*p && strchr("hljztL",*p)) {
        length[0]=*p++;
        if ((length[0]=='h'||length[0]=='l') && *p==length[0]) length[1]=*p++;
    }
    if (!*p || !strchr("diouxXfFeEgGaAcspn[%",*p)) return ERR_ARG;
    *conversion=*p++;
    if (*conversion=='[') {
        if (*p=='^') ++p;
        if (*p==']') ++p;
        while (*p && *p!=']') ++p;
        if (*p!=']') return ERR_ARG;
        ++p;
    }
    if (strchr("diouxXn",*conversion) && length[0]=='L') return ERR_ARG;
    if (strchr("fFeEgGaA",*conversion) && length[0] && strcmp(length,"l") && strcmp(length,"L")) return ERR_ARG;
    if (strchr("cs[",*conversion) && length[0] && strcmp(length,"l")) return ERR_ARG;
    if (strchr("p%",*conversion) && length[0]) return ERR_ARG;
    if (*conversion=='%' && p-start!=2) return ERR_ARG;
    if (*conversion=='n' && (has_width || *suppress)) return ERR_ARG;
    *end=p; return OK;
}

/* Сохраняет счётчик %n подходящего типа; присваивание не увеличивает результат scanf. */
static Status scan_count(va_list *const args, const char *const length, const size_t count) {
#define COUNT(T, MAXIMUM) do { T *p=va_arg(*args,T *); if (!p) return ERR_ARG; \
    if (count>(size_t)(MAXIMUM)) { return ERR_RANGE; } *p=(T)count; } while(0)
    if (!strcmp(length,"hh")) COUNT(signed char,SCHAR_MAX);
    else if (!strcmp(length,"h")) COUNT(short,SHRT_MAX);
    else if (!strcmp(length,"ll")) COUNT(long long,LLONG_MAX);
    else if (!strcmp(length,"l")) COUNT(long,LONG_MAX);
    else if (!strcmp(length,"j")) COUNT(intmax_t,INTMAX_MAX);
    else if (!strcmp(length,"z") || !strcmp(length,"t")) COUNT(ptrdiff_t,PTRDIFF_MAX);
    else COUNT(int,INT_MAX);
#undef COUNT
    return OK;
}

/* Делегирует одну стандартную конверсию fscanf; добавленный %n считает прочитанные байты. */
static Status scan_standard(FILE *const in, const char *const spec, const char *const length,
                            const char conversion, const bool suppress, va_list *const args,
                            int *const assigned, int *const consumed) {
    *consumed=-1;
    if (suppress || conversion=='%') { *assigned=fscanf(in,spec,consumed); return OK; }
#define READ(T) do { T *p=va_arg(*args,T *); if (!p) return ERR_ARG; *assigned=fscanf(in,spec,p,consumed); } while(0)
    if (strchr("di",conversion)) {
        if (!strcmp(length,"hh")) READ(signed char);
        else if (!strcmp(length,"h")) READ(short);
        else if (!strcmp(length,"ll")) READ(long long);
        else if (!strcmp(length,"l")) READ(long);
        else if (!strcmp(length,"j")) READ(intmax_t);
        else if (!strcmp(length,"z") || !strcmp(length,"t")) READ(ptrdiff_t);
        else READ(int);
    } else if (strchr("ouxX",conversion)) {
        if (!strcmp(length,"hh")) READ(unsigned char);
        else if (!strcmp(length,"h")) READ(unsigned short);
        else if (!strcmp(length,"ll")) READ(unsigned long long);
        else if (!strcmp(length,"l")) READ(unsigned long);
        else if (!strcmp(length,"j")) READ(uintmax_t);
        else if (!strcmp(length,"z") || !strcmp(length,"t")) READ(size_t);
        else READ(unsigned);
    } else if (strchr("fFeEgGaA",conversion)) {
        if (!strcmp(length,"L")) READ(long double);
        else if (!strcmp(length,"l")) READ(double);
        else READ(float);
    } else if (strchr("cs[",conversion)) {
        if (!strcmp(length,"l")) READ(wchar_t);
        else READ(char);
    } else if (conversion=='p') READ(void *);
    else return ERR_ARG;
#undef READ
    return OK;
}

/* Общий разбор потока: literals, пробелы, стандартные и дополнительные форматы. */
static int scan_input(FILE *const in, const char *format, va_list *const args) {
    if (!in || !format || !args) return EOF;
    int assigned=0; size_t consumed=0;
    while (*format) {
        if (isspace((unsigned char)*format)) {
            while (isspace((unsigned char)*format)) ++format;
            int c;
            while ((c=fgetc(in))!=EOF && isspace((unsigned char)c)) {
                if (consumed==INT_MAX) return EOF;
                ++consumed;
            }
            if (c!=EOF && ungetc(c,in)==EOF) return EOF;
            if (ferror(in)) return EOF;
            continue;
        }
        if (*format!='%') {
            const int c=fgetc(in);
            if (c==EOF) return assigned ? assigned : EOF;
            if (c!=(unsigned char)*format) { if (ungetc(c,in)==EOF) return EOF; return assigned; }
            if (consumed==INT_MAX) return EOF;
            ++consumed; ++format; continue;
        }
        const char a=format[1], b=a ? format[2] : '\0';
        if ((a=='R'&&b=='o') || (a=='Z'&&b=='r') || (a=='C'&&(b=='v'||b=='V'))) {
            int *integer=NULL; unsigned *natural=NULL; int base=10;
            if (a=='Z') natural=va_arg(*args,unsigned *);
            else integer=va_arg(*args,int *);
            if ((a=='Z' && !natural) || (a!='Z' && !integer)) return EOF;
            if (a=='C') { base=va_arg(*args,int); if (base<2||base>36) base=10; }
            char *word=NULL; bool at_eof;
            Status st=custom_word(in,a,base,b=='V',&word,&consumed,&at_eof);
            if (st!=OK) { free(word); return EOF; }
            if (!word) return at_eof && !assigned ? EOF : assigned;
            if (a=='R') st=parse_roman(word,integer);
            else if (a=='Z') st=parse_zeckendorf(word,natural);
            else st=parse_base(word,base,b=='V',integer);
            free(word);
            if (st!=OK) return assigned;
            ++assigned; format+=3; continue;
        }
        char length[3], conversion; const char *end; bool suppress;
        Status st=scan_spec(format,&end,length,&conversion,&suppress);
        if (st!=OK) return EOF;
        if (conversion=='n') {
            if (scan_count(args,length,consumed)!=OK) return EOF;
            format=end; continue;
        }
        const size_t n=(size_t)(end-format);
        if (n>SIZE_MAX-3) return EOF;
        char *spec=malloc(n+3);
        if (!spec) return EOF;
        memcpy(spec,format,n); memcpy(spec+n,"%n",3);
        int current=0, used=-1;
        st=scan_standard(in,spec,length,conversion,suppress,args,&current,&used);
        free(spec);
        if (st!=OK || ferror(in)) return EOF;
        if (used<0) return current==EOF && !assigned ? EOF : assigned;
        if ((size_t)used>(size_t)INT_MAX-consumed) return EOF;
        consumed+=(size_t)used;
        if (!suppress && conversion!='%') ++assigned;
        format=end;
    }
    return assigned;
}

/* Аналог fscanf: число присваиваний; EOF при ошибке чтения до первого присваивания. */
static int overfscanf(FILE *const stream, const char *const format, ...) {
    if (!stream || !format) return EOF;
    va_list args;
    va_start(args,format); const int result=scan_input(stream,format,&args); va_end(args);
    return result;
}

/* Строку помещаем во временный поток, чтобы оба интерфейса имели одинаковый разбор. */
static int oversscanf(const char *const text, const char *const format, ...) {
    if (!text || !format) return EOF;
    FILE *in=tmpfile();
    if (!in) return EOF;
    const size_t n=strlen(text);
    int result=EOF;
    if (fwrite(text,1,n,in)==n && fflush(in)!=EOF && fseek(in,0,SEEK_SET)==0) {
        va_list args;
        va_start(args,format); result=scan_input(in,format,&args); va_end(args);
    }
    if (fclose(in)==EOF) result=EOF;
    return result;
}

/* Показывает обе функции, пользовательские форматы, обычные числа и %n. */
int main(void) {
    int roman=0, lower=0, upper=0, count=0; unsigned zeck=0; double real=0;
    const int n=oversscanf("XIV 0010011 ff FF 1.25", "%Ro %Zr %Cv %CV %lf%n",
        &roman,&zeck,&lower,16,&upper,16,&real,&count);
    if (n!=5 || printf("%d %u %d %d %.2f; read %d bytes\n",roman,zeck,lower,upper,real,count)<0) { fprintf(stderr, "Error: expected five parsed values and writable output. Check Roman/Fibonacci notation, base and matching pointer types.\n"); return 1; }
    FILE *file=tmpfile();
    if (!file) { fprintf(stderr, "Error: cannot create a temporary file.\n"); return 1; }
    bool failed=fputs("IX 42",file)==EOF || fflush(file)==EOF || fseek(file,0,SEEK_SET)!=0;
    int number=0;
    if (!failed) failed=overfscanf(file,"%Ro %d",&roman,&number)!=2;
    if (fclose(file)==EOF) failed=true;
    if (!failed && printf("file: %d %d\n",roman,number)<0) failed=true;
    if (failed) fprintf(stderr, "Error: temporary file read/write/close or Roman input conversion failed.\n");
    return failed;
}
