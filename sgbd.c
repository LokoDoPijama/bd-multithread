#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "sgbd.h"


/* -------------------------------------------------- */
/* Utility helpers                                    */
/* -------------------------------------------------- */

static void trim_whitespace(char *str) {
    if (!str) return;

    char *start = str;
    while (isspace((unsigned char)*start)) start++;

    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }

    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

static int starts_with(const char *str, const char *prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

static int is_valid_int(const char *s) {
    if (!s || *s == '\0') return 0;

    if (*s == '+' || *s == '-') s++;
    if (*s == '\0') return 0;

    while (*s) {
        if (!isdigit((unsigned char)*s)) return 0;
        s++;
    }
    return 1;
}

static void skip_spaces(const char **p) {
    while (**p && isspace((unsigned char)**p)) {
        (*p)++;
    }
}

static int expect_char(const char **p, char c) {
    skip_spaces(p);
    if (**p != c) return 0;
    (*p)++;
    return 1;
}

static int expect_keyword(const char **p, const char *kw) {
    skip_spaces(p);
    size_t len = strlen(kw);
    if (strncmp(*p, kw, len) != 0) return 0;
    *p += len;
    return 1;
}

static int parse_int_value(const char **p, int *out) {
    skip_spaces(p);

    char buffer[64];
    int i = 0;

    if (**p == '+' || **p == '-') {
        buffer[i++] = **p;
        (*p)++;
    }

    if (!isdigit((unsigned char)**p)) return 0;

    while (**p && isdigit((unsigned char)**p)) {
        if (i >= (int)sizeof(buffer) - 1) return 0;
        buffer[i++] = **p;
        (*p)++;
    }

    buffer[i] = '\0';

    if (!is_valid_int(buffer)) return 0;

    *out = atoi(buffer);
    return 1;
}

static int parse_quoted_string(const char **p, char *out, size_t out_size) {
    skip_spaces(p);

    if (**p != '"') return 0;
    (*p)++; /* skip opening quote */

    size_t i = 0;
    while (**p && **p != '"') {
        if (i + 1 >= out_size) return 0;
        out[i++] = **p;
        (*p)++;
    }

    if (**p != '"') return 0; /* missing closing quote */
    (*p)++; /* skip closing quote */

    out[i] = '\0';
    return 1;
}

static int only_spaces_remaining(const char *p) {
    while (*p) {
        if (!isspace((unsigned char)*p)) return 0;
        p++;
    }
    return 1;
}

/* -------------------------------------------------- */
/* CSV helpers                                        */
/* CSV format: id,"name",age                          */
/* -------------------------------------------------- */

static int is_header_line(const char *line) {
    char buffer[MAX_LINE];
    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    buffer[strcspn(buffer, "\r\n")] = '\0';
    trim_whitespace(buffer);
    return strcmp(buffer, "id,name,age") == 0;
}

static int ensure_csv_header(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fp = fopen(filename, "w");
        if (!fp) return 0;
        fprintf(fp, "id,name,age\n");
        fclose(fp);
        return 1;
    }

    char first_line[MAX_LINE];
    if (!fgets(first_line, sizeof(first_line), fp)) {
        fclose(fp);
        fp = fopen(filename, "w");
        if (!fp) return 0;
        fprintf(fp, "id,name,age\n");
        fclose(fp);
        return 1;
    }

    fclose(fp);

    if (is_header_line(first_line)) {
        return 1;
    }

    /* If file exists without header, recreate with header + old content */
    FILE *in = fopen(filename, "r");
    FILE *out = fopen("temp.csv", "w");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return 0;
    }

    fprintf(out, "id,name,age\n");

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), in)) {
        fputs(line, out);
    }

    fclose(in);
    fclose(out);

    remove(filename);
    rename("temp.csv", filename);
    return 1;
}

/* Parses CSV line: 123,"Arthur, Silva",20 */
static int parse_csv_line(const char *line, int *id, char *name, int *age) {
    if (!line || !id || !name || !age) return 0;
    if (is_header_line(line)) return 0;

    char buffer[MAX_LINE];
    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    buffer[strcspn(buffer, "\r\n")] = '\0';

    const char *p = buffer;

    if (!parse_int_value(&p, id)) return 0;
    if (!expect_char(&p, ',')) return 0;
    if (!parse_quoted_string(&p, name, MAX_NAME)) return 0;
    if (!expect_char(&p, ',')) return 0;
    if (!parse_int_value(&p, age)) return 0;

    skip_spaces(&p);
    return *p == '\0';
}

/* Returns malloc'd string with full CSV row, or NULL */
static db_entry *db_select_by_id(const char *filename, int target_id) {
    if (!ensure_csv_header(filename)) return NULL;

    FILE *fp = fopen(filename, "r");
    if (!fp) return NULL;
    
    db_entry *entry = NULL;

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {

        int id, age;
        char name[MAX_NAME];

        if (is_header_line(line)) continue;

        if (parse_csv_line(line, &id, name, &age) && id == target_id) {
            // char *result = malloc(strlen(line) + 1);
            // if (!result) return NULL;

            // strcpy(result, line);
            // result[strcspn(result, "\r\n")] = '\0';
            // return result;

            entry = malloc(sizeof(db_entry));

            entry->id = id;
            strcpy(entry->name, name);
            entry->age = age;

            break;
        }
    }

    fclose(fp);
    return entry;
}

static int db_insert(const char *filename, int id, const char *name, int age) {
    if (!ensure_csv_header(filename)) return 0;

    char *existing = db_select_by_id(filename, id);
    if (existing) {
        free(existing);
        return 0;
    }

    FILE *fp = fopen(filename, "a");
    if (!fp) return 0;

    fprintf(fp, "%d,\"%s\",%d\n", id, name, age);
    fclose(fp);

    return 1;
}

static int db_delete(const char *filename, int target_id) {
    if (!ensure_csv_header(filename)) return 0;

    FILE *in = fopen(filename, "r");
    FILE *out = fopen("temp.csv", "w");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return 0;
    }

    char line[MAX_LINE];
    int header_written = 0;

    while (fgets(line, sizeof(line), in)) {
        if (is_header_line(line)) {
            if (!header_written) {
                fprintf(out, "id,name,age\n");
                header_written = 1;
            }
            continue;
        }

        int id, age;
        char name[MAX_NAME];
        if (parse_csv_line(line, &id, name, &age)) {
            if (id != target_id) {
                fputs(line, out);
            }
        } else {
            fputs(line, out);
        }
    }

    fclose(in);
    fclose(out);

    remove(filename);
    rename("temp.csv", filename);

    return 1;
}

static int db_update(const char *filename, int target_id, const char *new_name, int new_age) {
    if (!ensure_csv_header(filename)) return 0;

    FILE *in = fopen(filename, "r");
    FILE *out = fopen("temp.csv", "w");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return 0;
    }

    fprintf(out, "id,name,age\n");

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), in)) {
        if (is_header_line(line)) continue;

        int id, age;
        char name[MAX_NAME];
        if (parse_csv_line(line, &id, name, &age)) {
            if (id == target_id) {
                fprintf(out, "%d,\"%s\",%d\n", target_id, new_name, new_age);
            } else {
                fprintf(out, "%d,\"%s\",%d\n", id, name, age);
            }
        } else {
            fputs(line, out);
        }
    }

    fclose(in);
    fclose(out);

    remove(filename);
    rename("temp.csv", filename);

    return 1;
}

/* -------------------------------------------------- */
/* Command parser                                     */
/* -------------------------------------------------- */

/*
   Supported formats:

   INSERT id = 1, name = "Arthur Silva", age = 20
   DELETE id = 1
   SELECT id = 1
   UPDATE id = 1, name = "Arthur, Silva", age = 21
*/

static int parse_id_only_assignment(const char *args, int *id) {
    const char *p = args;

    if (!expect_keyword(&p, "id")) return 0;
    if (!expect_char(&p, '=')) return 0;
    if (!parse_int_value(&p, id)) return 0;
    if (!only_spaces_remaining(p)) return 0;

    return 1;
}

static int parse_insert_or_update_assignments(const char *args, int *id, char *name, int *age) {
    const char *p = args;

    if (!expect_keyword(&p, "id")) return 0;
    if (!expect_char(&p, '=')) return 0;
    if (!parse_int_value(&p, id)) return 0;

    if (!expect_char(&p, ',')) return 0;

    if (!expect_keyword(&p, "name")) return 0;
    if (!expect_char(&p, '=')) return 0;
    if (!parse_quoted_string(&p, name, MAX_NAME)) return 0;

    if (!expect_char(&p, ',')) return 0;

    if (!expect_keyword(&p, "age")) return 0;
    if (!expect_char(&p, '=')) return 0;
    if (!parse_int_value(&p, age)) return 0;

    if (!only_spaces_remaining(p)) return 0;

    return 1;
}

int parse_command(const char *input, db_parsed_command *cmd) {
    if (!input || !cmd) return 0;

    char buffer[MAX_LINE];
    strncpy(buffer, input, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    trim_whitespace(buffer);

    cmd->type = CMD_INVALID;
    cmd->id = -1;
    cmd->name[0] = '\0';
    cmd->age = -1;

    if (starts_with(buffer, "INSERT ")) {
        cmd->type = CMD_INSERT;
        return parse_insert_or_update_assignments(buffer + 7, &cmd->id, cmd->name, &cmd->age);
    }

    if (starts_with(buffer, "DELETE ")) {
        cmd->type = CMD_DELETE;
        return parse_id_only_assignment(buffer + 7, &cmd->id);
    }

    if (starts_with(buffer, "SELECT ")) {
        cmd->type = CMD_SELECT;
        return parse_id_only_assignment(buffer + 7, &cmd->id);
    }

    if (starts_with(buffer, "UPDATE ")) {
        cmd->type = CMD_UPDATE;
        return parse_insert_or_update_assignments(buffer + 7, &cmd->id, cmd->name, &cmd->age);
    }

    return 0;
}

/* -------------------------------------------------- */
/* Dispatcher                                         */
/* -------------------------------------------------- */

db_response *db_execute_statement(const char *statement) {
    db_parsed_command cmd;

    db_response *response = (db_response *) malloc(sizeof(db_response));
    
    response->status = false;
    response->message = "Algo deu errado!";
    response->entry = NULL;


    if (!parse_command(statement, &cmd)) {
        return response;
    }

    switch (cmd.type) {
        case CMD_INSERT:
            response->status = db_insert(CSV_FILE, cmd.id, cmd.name, cmd.age);
            if (response->status)
                response->message = "Insert: realizado com sucesso!";
            else
                response->message = "Insert: algo deu errado!";
            
            return response;

        case CMD_DELETE:
            response->status = db_delete(CSV_FILE, cmd.id);
            if (response->status)
                response->message = "Delete: realizado com sucesso!";
            else
                response->message = "Delete: algo deu errado!";
                
            return response;

        case CMD_SELECT:
            response->entry = db_select_by_id(CSV_FILE, cmd.id);
            response->status = response->entry != NULL;
            if (response->status) {
                response->message = calloc(1024, sizeof(response->message));
                snprintf(response->message, 1024, "Id: %d; Nome: %s; Idade: %d", response->entry->id, response->entry->name, response->entry->age);
            } else {
                response->message = "Select: algo deu errado!";
            }
            return response;

        case CMD_UPDATE:
            response->status = db_update(CSV_FILE, cmd.id, cmd.name, cmd.age);
            if (response->status)
                response->message = "Update: realizado com sucesso!";
            else
                response->message = "Update: algo deu errado!";

            return response;

        default:
            return response;
    }
}