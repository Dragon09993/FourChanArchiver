#ifndef SETTINGS_H
#define SETTINGS_H

extern char redis_host[256];
extern int redis_port;
extern char board[256];

void load_settings();
void save_settings(const char *host, int port, const char *board);
void save_settings_callback();

#endif
