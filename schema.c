#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef enum {
    TYPE_NULL = 0,
    TYPE_INT = 1,
    TYPE_DOUBLE = 2,
    TYPE_STRING = 3
} ColumnType;

typedef struct {
    int scientific;
    int multiline;
    char delimiter;
} Options;


typedef struct {
    char *name;
    ColumnType type;

    long long nulls;
    long long count; 

    long long int_min;
    long long int_max;
    
    unsigned long long pos_low;
    unsigned long long pos_high;
    unsigned long long neg_low;
    unsigned long long neg_high;
    long long positive;
    long long negative;
    long long even;

    double dbl_min;
    double dbl_max;
    double dbl_sum;
    long long integer;

    size_t str_min;
    size_t str_max;
    double str_total;
    long long empty;
} Column;

typedef struct {
    size_t rows;
    size_t columns;
    Column *cols;
} Table;

void table_init(Table *t);
void table_free(Table *t);

const char *type_name(ColumnType type);

double int_mean(const Column *c);


int analyze_file(const char *path, const Options *opt, Table *t);

#define CSV_OK 1
#define CSV_END 0
#define CSV_ERROR -1

typedef struct {
    char *text;  
    size_t length;
    int quoted;    
} Field;

typedef struct {
    Field *items;
    size_t count;
    size_t capacity;
} Record;

typedef struct {
    FILE *file;
    char delimiter;
    int multiline;
} CsvReader;

void record_init(Record *rec);
void record_free(Record *rec);

void csv_reader_init(CsvReader *reader, FILE *file, char delimiter, int multiline);

int csv_read_record(CsvReader *reader, Record *rec);

int csv_field_is_null(const Field *field);


#define REAL_BUF_SIZE 512

char *copy_string(const char *s);

void format_real(char *buf, size_t size, double x);


void table_init(Table *t)
{
    t->rows = 0;
    t->columns = 0;
    t->cols = NULL;
}

void table_free(Table *t)
{
    size_t i;
    for (i = 0; i < t->columns; i++) {
        free(t->cols[i].name);
    }
    free(t->cols);
    table_init(t);
}

const char *type_name(ColumnType type)
{
    switch (type) {
    case TYPE_INT:
        return "INT";
    case TYPE_DOUBLE:
        return "DOUBLE";
    case TYPE_STRING:
        return "STRING";
    default:
        return "NULL";
    }
}


/* Идёт вперёд по цифрам и возвращает указатель на первый нецифровой символ */
static const char *skip_digits(const char *s)
{
    while (*s >= '0' && *s <= '9') {
        s++;
    }
    return s;
}

/* Подходит ли строка под int */
static int like_int(const char *s)
{
    const char *end;
    if (*s == '+' || *s == '-') {
        s++;
    }
    end = skip_digits(s);
    return end != s && *end == '\0';
}

static int like_double(const char *s, int scientific)
{
    const char *p;
    const char *q;
    int has_dot = 0;
    int has_exp = 0;

    if (*s == '+' || *s == '-') {
        s++;
    }
    p = skip_digits(s);
    if (p == s) {
        return 0;
    }
    if (*p == '.') {
        p++;
        q = skip_digits(p);
        if (q == p) {
            return 0;
        }
        p = q;
        has_dot = 1;
    }
    if (scientific && (*p == 'e' || *p == 'E')) {
        p++;
        if (*p == '+' || *p == '-') {
            p++;
        }
        q = skip_digits(p);
        if (q == p) {
            return 0;
        }
        p = q;
        has_exp = 1;
    }
    if (*p != '\0') {
        return 0;
    }
    return has_dot || has_exp;
}

static ColumnType detect_value_type(const Field *f, int scientific)
{
    if (csv_field_is_null(f)) {
        return TYPE_NULL;
    }
    if (like_int(f->text)) {
        errno = 0;
        strtoll(f->text, NULL, 10);
        if (errno == ERANGE) {
            return TYPE_STRING; /* не влезает в 64 бита */
        }
        return TYPE_INT;
    }
    if (like_double(f->text, scientific)) {
        double v = strtod(f->text, NULL);
        if (v - v != 0.0) {
            return TYPE_STRING; /* получилась бесконечность (inf - inf не равно 0) */
        }
        return TYPE_DOUBLE;
    }
    return TYPE_STRING;
}


/* Прибавляет x к числу из двух частей (high, low) */
static void add_to_sum(unsigned long long *low, unsigned long long *high, unsigned long long x)
{
    *low += x;
    if (*low < x) {
        (*high)++; /* low переполнилось - запоминаем перенос */
    }
}

/* Переводит high и low в обычное вещественное число */
static double sum_to_double(unsigned long long low, unsigned long long high)
{
    return (double)high * 18446744073709551616.0 + (double)low;
}

double int_mean(const Column *c)
{
    unsigned long long low;
    unsigned long long high;
    double mean;

    if (c->pos_high > c->neg_high || (c->pos_high == c->neg_high && c->pos_low >= c->neg_low)) {
        /* сумма неотрицательная: pos - neg */
        low = c->pos_low - c->neg_low;
        high = c->pos_high - c->neg_high;
        if (c->pos_low < c->neg_low) {
            high--; /* занимаем из старшей части */
        }
        mean = sum_to_double(low, high) / (double)c->count;
    } else {
        /* сумма отрицательная -> neg - pos, потом меняем знак */
        low = c->neg_low - c->pos_low;
        high = c->neg_high - c->pos_high;
        if (c->neg_low < c->pos_low) {
            high--;
        }
        mean = -(sum_to_double(low, high) / (double)c->count);
    }
    return mean;
}

static void add_int(Column *c, long long v)
{
    if (c->count == 0) {
        c->int_min = v;
        c->int_max = v;
    } else {
        if (v < c->int_min) {
            c->int_min = v;
        }
        if (v > c->int_max) {
            c->int_max = v;
        }
    }
    if (v >= 0) {
        add_to_sum(&c->pos_low, &c->pos_high, (unsigned long long)v);
    } else {
        /* модуль отрицательного числа */
        add_to_sum(&c->neg_low, &c->neg_high, 0ULL - (unsigned long long)v);
    }
    if (v > 0) {
        c->positive++;
    } else if (v < 0) {
        c->negative++;
    }
    if (v % 2 == 0) {
        c->even++;
    }
    c->count++;
}

/* Целое ли число */
static int is_whole_number(double v)
{
    if (v >= 9223372036854775808.0 || v <= -9223372036854775808.0) {
        return 1;
    }
    return v == (double)(long long)v;
}

static void add_double(Column *c, double v)
{
    if (c->count == 0) {
        c->dbl_min = v;
        c->dbl_max = v;
    } else {
        if (v < c->dbl_min) {
            c->dbl_min = v;
        }
        if (v > c->dbl_max) {
            c->dbl_max = v;
        }
    }
    c->dbl_sum += v;
    if (v > 0) {
        c->positive++;
    } else if (v < 0) {
        c->negative++;
    }
    if (is_whole_number(v)) {
        c->integer++;
    }
    c->count++;
}

static void add_string(Column *c, const Field *f)
{
    if (c->count == 0) {
        c->str_min = f->length;
        c->str_max = f->length;
    } else {
        if (f->length < c->str_min) {
            c->str_min = f->length;
        }
        if (f->length > c->str_max) {
            c->str_max = f->length;
        }
    }
    c->str_total += (double)f->length;
    if (f->length == 0) {
        c->empty++;
    }
    c->count++;
}


/* Создаёт столбцы по заголовку и проверяет имена */
static int create_columns(const Record *header, Table *t)
{
    size_t i;
    size_t j;

    for (i = 0; i < header->count; i++) {
        if (header->items[i].length == 0) {
            fprintf(stderr, "error: empty column name\n");
            return -1;
        }
        for (j = 0; j < i; j++) {
            if (strcmp(header->items[i].text, header->items[j].text) == 0) {
                fprintf(stderr, "error: duplicate column name\n");
                return -1;
            }
        }
    }

    t->cols = calloc(header->count, sizeof(Column));
    if (t->cols == NULL) {
        fprintf(stderr, "error: out of memory\n");
        return -1;
    }
    t->columns = header->count;
    for (i = 0; i < header->count; i++) {
        t->cols[i].name = copy_string(header->items[i].text);
        if (t->cols[i].name == NULL) {
            fprintf(stderr, "error: out of memory\n");
            return -1;
        }
    }
    return 0;
}

/* считаем строки и определяем типы столбцов */
static int detect_types(CsvReader *reader, Record *rec, Table *t, const Options *opt)
{
    int status;
    size_t i;

    while ((status = csv_read_record(reader, rec)) == CSV_OK) {
        if (rec->count != t->columns) {
            fprintf(stderr, "error: wrong number of fields in a row\n");
            return -1;
        }
        for (i = 0; i < t->columns; i++) {
            ColumnType vt = detect_value_type(&rec->items[i], opt->scientific);
            if (vt > t->cols[i].type) {
                t->cols[i].type = vt;
            }
        }
        t->rows++;
    }
    if (status == CSV_ERROR) {
        fprintf(stderr, "error: invalid CSV (bad format or out of memory)\n");
        return -1;
    }
    return 0;
}

/* считаем статистики */
static int compute_stats(CsvReader *reader, Record *rec, Table *t)
{
    int status;
    size_t i;

    while ((status = csv_read_record(reader, rec)) == CSV_OK) {
        for (i = 0; i < t->columns; i++) {
            const Field *f = &rec->items[i];
            Column *col = &t->cols[i];

            if (csv_field_is_null(f)) {
                col->nulls++;
            } else if (col->type == TYPE_INT) {
                add_int(col, strtoll(f->text, NULL, 10));
            } else if (col->type == TYPE_DOUBLE) {
                add_double(col, strtod(f->text, NULL));
            } else if (col->type == TYPE_STRING) {
                add_string(col, f);
            }
        }
    }
    if (status == CSV_ERROR) {
        fprintf(stderr, "error: invalid CSV (bad format or out of memory)\n");
        return -1;
    }
    return 0;
}

static int analyze_stream(FILE *file, const Options *opt, Table *t, Record *rec)
{
    CsvReader reader;
    int status;

    /* заголовок и типы */
    csv_reader_init(&reader, file, opt->delimiter, opt->multiline);
    status = csv_read_record(&reader, rec);
    if (status == CSV_END) {
        fprintf(stderr, "error: empty file\n");
        return -1;
    }
    if (status == CSV_ERROR) {
        fprintf(stderr, "error: invalid header\n");
        return -1;
    }
    if (create_columns(rec, t) != 0) {
        return -1;
    }
    if (detect_types(&reader, rec, t, opt) != 0) {
        return -1;
    }

    /*считаем статистики */
    rewind(file);
    csv_reader_init(&reader, file, opt->delimiter, opt->multiline);
    if (csv_read_record(&reader, rec) != CSV_OK) {
        fprintf(stderr, "error: cannot read header again\n");
        return -1;
    }
    if (compute_stats(&reader, rec, t) != 0) {
        return -1;
    }
    return 0;
}

int analyze_file(const char *path, const Options *opt, Table *t)
{
    FILE *file;
    Record rec;
    int status;

    table_init(t);

    file = fopen(path, "rb");
    if (file == NULL) {
        perror("error: cannot open file");
        return 1;
    }

    record_init(&rec);
    status = analyze_stream(file, opt, t, &rec);

    record_free(&rec);
    fclose(file);

    if (status != 0) {
        table_free(t);
        return 1;
    }
    return 0;
}
