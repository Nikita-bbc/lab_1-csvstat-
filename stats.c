#include <stdio.h>
#include <stddef.h>

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

    /* статистики INT */
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

#define REAL_BUF_SIZE 512

char *copy_string(const char *s);

void format_real(char *buf, size_t size, double x);


static void print_quoted_name(FILE *out, const char *name)
{
    const char *p;
    fputc('"', out);
    for (p = name; *p != '\0'; p++) {
        if (*p == '"') {
            fputs("\"\"", out);
        } else {
            fputc(*p, out);
        }
    }
    fputc('"', out);
}

void print_schema(FILE *out, const Table *t)
{
    size_t i;
    fprintf(out, "rows %zu\n", t->rows);
    fprintf(out, "columns %zu\n", t->columns);
    for (i = 0; i < t->columns; i++) {
        fprintf(out, "%zu ", i);
        print_quoted_name(out, t->cols[i].name);
        fprintf(out, " %s\n", type_name(t->cols[i].type));
    }
}

static void print_int_block(FILE *out, const Column *c)
{
    char buf[REAL_BUF_SIZE];
    format_real(buf, sizeof(buf), int_mean(c));
    fprintf(out, "min %lld\n", c->int_min);
    fprintf(out, "max %lld\n", c->int_max);
    fprintf(out, "mean %s\n", buf);
    fprintf(out, "positive %lld\n", c->positive);
    fprintf(out, "negative %lld\n", c->negative);
    fprintf(out, "even %lld\n", c->even);
}

static void print_double_block(FILE *out, const Column *c)
{
    char buf[REAL_BUF_SIZE];
    format_real(buf, sizeof(buf), c->dbl_min);
    fprintf(out, "min %s\n", buf);
    format_real(buf, sizeof(buf), c->dbl_max);
    fprintf(out, "max %s\n", buf);
    format_real(buf, sizeof(buf), c->dbl_sum / (double)c->count);
    fprintf(out, "mean %s\n", buf);
    fprintf(out, "positive %lld\n", c->positive);
    fprintf(out, "negative %lld\n", c->negative);
    fprintf(out, "integer %lld\n", c->integer);
}

static void print_string_block(FILE *out, const Column *c)
{
    char buf[REAL_BUF_SIZE];
    format_real(buf, sizeof(buf), c->str_total / (double)c->count);
    fprintf(out, "min_length %zu\n", c->str_min);
    fprintf(out, "max_length %zu\n", c->str_max);
    fprintf(out, "mean_length %s\n", buf);
    fprintf(out, "empty %lld\n", c->empty);
}

void print_stats(FILE *out, const Table *t)
{
    size_t i;

    fprintf(out, "rows %zu\n", t->rows);
    fprintf(out, "columns %zu\n", t->columns);

    for (i = 0; i < t->columns; i++) {
        const Column *c = &t->cols[i];

        if (i > 0) {
            fputc('\n', out); /* пустая строка между блоками */
        }
        fprintf(out, "column %zu\n", i);
        fputs("name ", out);
        print_quoted_name(out, c->name);
        fputc('\n', out);
        fprintf(out, "type %s\n", type_name(c->type));

        if (c->type == TYPE_NULL) {
            fprintf(out, "rows %zu\n", t->rows);
            continue;
        }
        fprintf(out, "nulls %lld\n", c->nulls);
        if (c->type == TYPE_INT) {
            print_int_block(out, c);
        } else if (c->type == TYPE_DOUBLE) {
            print_double_block(out, c);
        } else {
            print_string_block(out, c);
        }
    }
}
