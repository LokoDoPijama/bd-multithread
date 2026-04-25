#ifndef __SGBD_H__
#define __SGBD_H__

#define CSV_FILE "tabela.csv"
#define MAX_LINE 1024
#define MAX_NAME 256

typedef struct db_entry {
    int id;
    char name[MAX_NAME];
    int age;
} db_entry;

typedef struct db_response {
    db_entry *entry;
    bool status;
    char *message;
} db_response;


typedef enum {
    CMD_INVALID,
    CMD_INSERT,
    CMD_DELETE,
    CMD_SELECT,
    CMD_UPDATE
} db_command_type;

typedef struct {
    db_command_type type;
    int id;
    char name[MAX_NAME];
    int age;
} db_parsed_command;


int parse_command(const char *input, db_parsed_command *cmd);
db_response *db_execute_statement(const char *statement);

#endif