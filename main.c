#include <stddef.h>
#include <stdio.h>
#include <string.h>


typedef enum {
    type_null = 0,
    type_int = 1,
    type_double = 2,
    type_string = 3
} ColumnType;

typedef struct {
    int scientific;
    int multiline;
    char delimiter;
} Options; /* разбор csv */

/* Один столбец: имя, тип и все статистики */
typedef struct {
    char *name;
    ColumnType type;

    long long nulls;
    long long count; /* сколько значений not NULL */

    /* статистики INT */
    long long int_min;
    long long int_max;
    /*
    high (сколько раз low переполнялось).
     */
    unsigned long long pos_low;
    unsigned long long pos_high;
    unsigned long long neg_low;
    unsigned long long neg_high;
    long long positive;
    long long negative;
    long long even;

    /* статистики DOUBLE */
    double dbl_min;
    double dbl_max;
    double dbl_sum;
    long long integer;

    /* статистики STRING */
    size_t str_min;
    size_t str_max;
    double str_total;
    long long empty;
} Column;

/* Вся таблица: число строк и столбцы */
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

/*
 Читает CSV-файл два раза: первый проход определяет типы,
 второй считает статистики. Возвращает 0 при успехе, 1 при ошибке
 (сообщение об ошибке уже выведено в stderr).
 */
int analyze_file(const char *path, const Options *opt, Table *t);


/* Команда schema */
void print_schema(FILE *out, const Table *t);

/* Команда stats */
void print_stats(FILE *out, const Table *t);


/* Записывает таблицу со статистиками в текстовый файл */
int write_report(const char *path, const Table *t);


#define EXIT_OK 0
#define EXIT_RUNTIME_ERROR 1
#define EXIT_USAGE_ERROR 2

static void print_help(FILE *out)
{
    fputs("Usage:\n"
          "  csvstat --help\n"
          "  csvstat schema <csv-file> [options]\n"
          "  csvstat stats  <csv-file> [options]\n"
          "  csvstat report <csv-file> <output-file> [options]\n"
          "Options:\n"
          "  --scientific       allow exponent notation (1e10, -2.5E-3)\n"
          "  --multiline        allow newlines inside quoted fields\n"
          "  --delimiter <char> use another one-character ASCII delimiter\n",
          out);
}

/* Подходит ли строка как разделитель */
static int delimiter_is_valid(const char *s)
{
    char c;
    if (strlen(s) != 1) {
        return 0;
    }
    c = s[0];
    if (c == '"' || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        return 0;
    }
    if ((unsigned char)c >= 128) {
        return 0;
    }
    return 1;
}

/* Разбирает параметры начиная с argv[start] */
static int parse_options(int argc, char **argv, int start, Options *opt)
{
    int seen_scientific = 0;
    int seen_multiline = 0;
    int seen_delimiter = 0;
    int i;

    opt->scientific = 0;
    opt->multiline = 0;
    opt->delimiter = ',';

    for (i = start; i < argc; i++) {
        if (strcmp(argv[i], "--scientific") == 0) {
            if (seen_scientific) {
                return -1;
            }
            seen_scientific = 1;
            opt->scientific = 1;
        } else if (strcmp(argv[i], "--multiline") == 0) {
            if (seen_multiline) {
                return -1;
            }
            seen_multiline = 1;
            opt->multiline = 1;
        } else if (strcmp(argv[i], "--delimiter") == 0) {
            if (seen_delimiter || i + 1 >= argc) {
                return -1;
            }
            i++;
            if (!delimiter_is_valid(argv[i])) {
                return -1;
            }
            seen_delimiter = 1;
            opt->delimiter = argv[i][0];
        } else {
            return -1; /* неизвестный параметр или лишний аргумент */
        }
    }
    return 0;
}

static int usage_error(void)
{
    fprintf(stderr, "error: invalid command line (try csvstat --help)\n");
    return EXIT_USAGE_ERROR;
}

int main(int argc, char **argv)
{
    Options opt;
    Table table;
    const char *command;

    if (argc < 2) {
        return usage_error();
    }
    command = argv[1];

    if (strcmp(command, "--help") == 0) {
        if (argc != 2) {
            return usage_error();
        }
        print_help(stdout);
        return EXIT_OK;
    }

    if (strcmp(command, "schema") == 0 || strcmp(command, "stats") == 0) {
        if (argc < 3 || parse_options(argc, argv, 3, &opt) != 0) {
            return usage_error();
        }
        if (analyze_file(argv[2], &opt, &table) != 0) {
            return EXIT_RUNTIME_ERROR;
        }
        if (strcmp(command, "schema") == 0) {
            print_schema(stdout, &table);
        } else {
            print_stats(stdout, &table);
        }
        table_free(&table);
        return EXIT_OK;
    }

    if (strcmp(command, "report") == 0) {
        int status;
        if (argc < 4 || parse_options(argc, argv, 4, &opt) != 0) {
            return usage_error();
        }
        if (analyze_file(argv[2], &opt, &table) != 0) {
            return EXIT_RUNTIME_ERROR;
        }
        status = write_report(argv[3], &table);
        table_free(&table);
        return status == 0 ? EXIT_OK : EXIT_RUNTIME_ERROR;
    }

    return usage_error();
}
