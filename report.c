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
    long long count; /* сколько значений НЕ NULL */

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

/* Среднее арифметическое значений INT-столбца */
double int_mean(const Column *c);

int analyze_file(const char *path, const Options *opt, Table *t);


/* Размер буфера, которого хватит для любого вещественного числа с 6 знаками */
#define REAL_BUF_SIZE 512

/* Копирует строку в динамическую память */
char *copy_string(const char *s);

/* Печатает число с ровно 6 знаками после точки */
void format_real(char *buf, size_t size, double x);


#define REPORT_COLS 8

static const char *headers[REPORT_COLS] = {"#", "Name", "Type", "Nulls", "Min", "Max", "Mean", "Details"};
/* 1 - выравнивание вправо (числа), 0 - влево (текст) */
static const int right_aligned[REPORT_COLS] = {1, 0, 0, 1, 1, 1, 1, 0};

/* Заполняет одну строку таблицы отчёта (массив из REPORT_COLS строк) */
static int fill_row(char **row, size_t index, const Column *c, size_t table_rows)
{
    char buf[REAL_BUF_SIZE + 16];
    char buf2[REAL_BUF_SIZE];
    size_t k;

    snprintf(buf, sizeof(buf), "%zu", index);
    row[0] = copy_string(buf);
    row[1] = copy_string(c->name);
    row[2] = copy_string(type_name(c->type));

    if (c->type == TYPE_NULL) {
        snprintf(buf, sizeof(buf), "%zu", table_rows);
        row[3] = copy_string(buf);
        row[4] = copy_string("-");
        row[5] = copy_string("-");
        row[6] = copy_string("-");
        row[7] = copy_string("all values are NULL");
    } else {
        snprintf(buf, sizeof(buf), "%lld", c->nulls);
        row[3] = copy_string(buf);

        if (c->type == TYPE_INT) {
            snprintf(buf, sizeof(buf), "%lld", c->int_min);
            row[4] = copy_string(buf);
            snprintf(buf, sizeof(buf), "%lld", c->int_max);
            row[5] = copy_string(buf);
            format_real(buf, sizeof(buf), int_mean(c));
            row[6] = copy_string(buf);
            snprintf(buf, sizeof(buf), "positive=%lld negative=%lld even=%lld",
                     c->positive, c->negative, c->even);
            row[7] = copy_string(buf);
        } else if (c->type == TYPE_DOUBLE) {
            format_real(buf, sizeof(buf), c->dbl_min);
            row[4] = copy_string(buf);
            format_real(buf, sizeof(buf), c->dbl_max);
            row[5] = copy_string(buf);
            format_real(buf, sizeof(buf), c->dbl_sum / (double)c->count);
            row[6] = copy_string(buf);
            snprintf(buf, sizeof(buf), "positive=%lld negative=%lld integer=%lld",
                     c->positive, c->negative, c->integer);
            row[7] = copy_string(buf);
        } else {
            /* для строк Min/Max/Mean - это длины в байтах */
            snprintf(buf, sizeof(buf), "len %zu", c->str_min);
            row[4] = copy_string(buf);
            snprintf(buf, sizeof(buf), "len %zu", c->str_max);
            row[5] = copy_string(buf);
            format_real(buf2, sizeof(buf2), c->str_total / (double)c->count);
            snprintf(buf, sizeof(buf), "len %s", buf2);
            row[6] = copy_string(buf);
            snprintf(buf, sizeof(buf), "empty=%lld", c->empty);
            row[7] = copy_string(buf);
        }
    }

    for (k = 0; k < REPORT_COLS; k++) {
        if (row[k] == NULL) {
            return -1;
        }
    }
    return 0;
}

static void print_separator(FILE *out, const size_t *widths)
{
    size_t k;
    size_t i;
    fputc('+', out);
    for (k = 0; k < REPORT_COLS; k++) {
        for (i = 0; i < widths[k] + 2; i++) {
            fputc('-', out);
        }
        fputc('+', out);
    }
    fputc('\n', out);
}

static void print_row(FILE *out, char **row, const size_t *widths)
{
    size_t k;
    fputc('|', out);
    for (k = 0; k < REPORT_COLS; k++) {
        fputc(' ', out);
        if (right_aligned[k]) {
            fprintf(out, "%*s", (int)widths[k], row[k]);
        } else {
            fprintf(out, "%-*s", (int)widths[k], row[k]);
        }
        fputs(" |", out);
    }
    fputc('\n', out);
}

int write_report(const char *path, const Table *t)
{
    size_t total_rows = t->columns + 1; /* +1 строка заголовков */
    size_t widths[REPORT_COLS] = {0};
    char **cells;
    FILE *out;
    size_t r;
    size_t k;
    int result = 1;
    int write_failed = 0;

    cells = calloc(total_rows * REPORT_COLS, sizeof(char *));
    if (cells == NULL) {
        fprintf(stderr, "error: out of memory\n");
        return 1;
    }

    /* Готовим все ячейки заранее, чтобы узнать ширину столбцов */
    for (k = 0; k < REPORT_COLS; k++) {
        cells[k] = copy_string(headers[k]);
        if (cells[k] == NULL) {
            fprintf(stderr, "error: out of memory\n");
            goto cleanup;
        }
    }
    for (r = 0; r < t->columns; r++) {
        if (fill_row(&cells[(r + 1) * REPORT_COLS], r, &t->cols[r], t->rows) != 0) {
            fprintf(stderr, "error: out of memory\n");
            goto cleanup;
        }
    }
    for (r = 0; r < total_rows; r++) {
        for (k = 0; k < REPORT_COLS; k++) {
            size_t len = strlen(cells[r * REPORT_COLS + k]);
            if (len > widths[k]) {
                widths[k] = len;
            }
        }
    }

    out = fopen(path, "wb");
    if (out == NULL) {
        perror("error: cannot open output file");
        goto cleanup;
    }

    fprintf(out, "Rows:    %zu\n", t->rows);
    fprintf(out, "Columns: %zu\n\n", t->columns);
    print_separator(out, widths);
    print_row(out, &cells[0], widths);
    print_separator(out, widths);
    for (r = 1; r < total_rows; r++) {
        print_row(out, &cells[r * REPORT_COLS], widths);
    }
    print_separator(out, widths);

    if (ferror(out)) {
        write_failed = 1;
    }
    if (fclose(out) != 0) {
        write_failed = 1;
    }
    if (write_failed) {
        fprintf(stderr, "error: cannot write report\n");
        remove(path); /* неполный файл не должен остаться */
        goto cleanup;
    }
    result = 0;

cleanup:
    for (r = 0; r < total_rows * REPORT_COLS; r++) {
        free(cells[r]);
    }
    free(cells);
    return result;
}
