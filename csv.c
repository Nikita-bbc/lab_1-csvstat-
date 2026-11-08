#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* Результаты чтения записи */
#define CSV_OK 1
#define CSV_END 0
#define CSV_ERROR -1

/* уже разобранное значение + было ли оно в кавычках */
typedef struct {
    char *text;    /* значение без кавычек, всегда заканчивается '\0' */
    size_t length; 
    int quoted;    /* 1, если поле было в двойных кавычках */
} Field;

/* строка таблицы = массив полей */
typedef struct {
    Field *items;
    size_t count;
    size_t capacity;
} Record;

/* помнит файл и настройки */
typedef struct {
    FILE *file;
    char delimiter;
    int multiline;
} CsvReader;

void record_init(Record *rec);
void record_free(Record *rec);

void csv_reader_init(CsvReader *reader, FILE *file, char delimiter, int multiline);

/* Читает следующую запись. Возвращает CSV_OK, CSV_END или CSV_ERROR. */
int csv_read_record(CsvReader *reader, Record *rec);

/* 1, если значение поля равно NULL */
int csv_field_is_null(const Field *field);


typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} StrBuf;

static int buf_init(StrBuf *b)
{
    b->capacity = 16;
    b->length = 0;
    b->data = malloc(b->capacity);
    if (b->data == NULL) {
        return 0;
    }
    b->data[0] = '\0';
    return 1;
}

static int buf_add(StrBuf *b, char c)
{
    if (b->length + 1 >= b->capacity) {
        size_t new_capacity = b->capacity * 2;
        char *new_data = realloc(b->data, new_capacity);
        if (new_data == NULL) {
            return 0;
        }
        b->data = new_data;
        b->capacity = new_capacity;
    }
    b->data[b->length] = c;
    b->length++;
    b->data[b->length] = '\0';
    return 1;
}


void record_init(Record *rec)
{
    rec->items = NULL;
    rec->count = 0;
    rec->capacity = 0;
}

/* Освобождает тексты полей, но оставляет массив для повторного использования */
static void record_clear(Record *rec)
{
    size_t i;
    for (i = 0; i < rec->count; i++) {
        free(rec->items[i].text);
    }
    rec->count = 0;
}

void record_free(Record *rec)
{
    record_clear(rec);
    free(rec->items);
    rec->items = NULL;
    rec->capacity = 0;
}

/* Добавляет поле в запись. Запись забирает text себе. При ошибке text не освобождается. */
static int record_add(Record *rec, char *text, size_t length, int quoted)
{
    if (rec->count == rec->capacity) {
        size_t new_capacity = (rec->capacity == 0) ? 8 : rec->capacity * 2;
        Field *new_items = realloc(rec->items, new_capacity * sizeof(Field));
        if (new_items == NULL) {
            return 0;
        }
        rec->items = new_items;
        rec->capacity = new_capacity;
    }
    rec->items[rec->count].text = text;
    rec->items[rec->count].length = length;
    rec->items[rec->count].quoted = quoted;
    rec->count++;
    return 1;
}

/* Запись из одного пустого поля */
static int record_add_empty_field(Record *rec)
{
    char *text = malloc(1);
    if (text == NULL) {
        return 0;
    }
    text[0] = '\0';
    if (!record_add(rec, text, 0, 0)) {
        free(text);
        return 0;
    }
    return 1;
}


void csv_reader_init(CsvReader *reader, FILE *file, char delimiter, int multiline)
{
    reader->file = file;
    reader->delimiter = delimiter;
    reader->multiline = multiline;
}

int csv_field_is_null(const Field *field)
{
    return !field->quoted && field->length == 0;
}

/* Пропускает пробелы и табуляции, возвращает первый другой символ */
static int skip_blanks(FILE *f)
{
    int c;
    do {
        c = fgetc(f);
    } while (c == ' ' || c == '\t');
    return c;
}

/*
Читает одно поле в буфер b.
quoted = 1, если поле было в кавычках.
end = 1, если поле было последним в записи..
 */
static int read_field(CsvReader *r, StrBuf *b, int *quoted, int *end)
{
    int c = skip_blanks(r->file);
    *quoted = 0;

    if (c == '"') {
        *quoted = 1;
        for (;;) {
            c = fgetc(r->file);
            if (c == EOF) {
                return -1; /* кавычка не закрыта */
            }
            if (c == '\n' && !r->multiline) {
                return -1; /* перевод строки внутри кавычек запрещён */
            }
            if (c == '"') {
                int next = fgetc(r->file);
                if (next == '"') {
                    /* две кавычки подряд = одна кавычка в значении */
                    if (!buf_add(b, '"')) {
                        return -1;
                    }
                    continue;
                }
                /* это закрывающая кавычка; возвращаем символ, который прочитали лишним */
                if (next != EOF) {
                    ungetc(next, r->file);
                }
                break;
            }
            if (!buf_add(b, (char)c)) {
                return -1;
            }
        }
        /* после закрывающей кавычки можно только пробелы/табы, потом разделитель или конец */
        c = skip_blanks(r->file);
        if (c == (unsigned char)r->delimiter) {
            *end = 0;
            return 0;
        }
        if (c == '\n' || c == EOF) {
            *end = 1;
            return 0;
        }
        return -1;
    }

    /* обычное поле */
    while (c != EOF && c != '\n' && c != (unsigned char)r->delimiter) {
        if (c == '"') {
            return -1; /* кавычка внутри обычного поля - ошибка */
        }
        if (!buf_add(b, (char)c)) {
            return -1;
        }
        c = fgetc(r->file);
    }
    /* убираем пробелы и табы в конце */
    while (b->length > 0 && (b->data[b->length - 1] == ' ' || b->data[b->length - 1] == '\t')) {
        b->length--;
    }
    b->data[b->length] = '\0';

    *end = (c != (unsigned char)r->delimiter);
    return 0;
}

int csv_read_record(CsvReader *r, Record *rec)
{
    int c;
    int end = 0;

    record_clear(rec);

    c = fgetc(r->file);
    if (c == EOF) {
        return ferror(r->file) ? CSV_ERROR : CSV_END;
    }

    if (c == '\n') {
        /*
        пустая строка. Что с ней делать, зависит от того,
        есть ли дальше в файле что-то кроме пустых строк.
        Запоминаем место, пролистываем все пустые строки вперёд и смотрим:
        если дошли до конца файла, то эти строки игнорируются и записей больше нет
        иначе возвращаемся назад и отдаём одну пустую запись.
         */
        long position = ftell(r->file);
        int next;
        while ((next = fgetc(r->file)) == '\n') {
            /* просто пропускаем */
        }
        if (next == EOF) {
            return ferror(r->file) ? CSV_ERROR : CSV_END;
        }
        fseek(r->file, position, SEEK_SET);
        return record_add_empty_field(rec) ? CSV_OK : CSV_ERROR;
    }

    ungetc(c, r->file);

    while (!end) {
        StrBuf buf;
        int quoted = 0;

        if (!buf_init(&buf)) {
            return CSV_ERROR;
        }
        if (read_field(r, &buf, &quoted, &end) != 0) {
            free(buf.data);
            return CSV_ERROR;
        }
        if (!record_add(rec, buf.data, buf.length, quoted)) {
            free(buf.data);
            return CSV_ERROR;
        }
    }
    return CSV_OK;
}
