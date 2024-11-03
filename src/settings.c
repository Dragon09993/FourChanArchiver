#include <stdio.h>
#include <string.h>
#include "../include/settings.h"

char redis_host[256] = "localhost";
int redis_port = 6379;
char board[256] = "adv";

void load_settings() {
    FILE *file = fopen("config.ini", "r");
    if (file) {
        fscanf(file, "host = %255s\n", redis_host);
        fscanf(file, "port = %d\n", &redis_port);
        fscanf(file, "board = %255s\n", board);
        fclose(file);
    }

   fprintf(stderr, "ini Variables loaded");
}

void save_settings(const char *host, int port, const char *new_board) {
    strncpy(redis_host, host, sizeof(redis_host) - 1);
    redis_port = port;
    strncpy(board, new_board, sizeof(board) - 1);

    FILE *file = fopen("config.ini", "w");
    if (file) {
        fprintf(file, "host = %s\n", redis_host);
        fprintf(file, "port = %d\n", redis_port);
        fprintf(file, "board = %s\n", board);
        fclose(file);
    }
}
