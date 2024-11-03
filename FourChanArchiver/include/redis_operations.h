#ifndef REDIS_OPERATIONS_H
#define REDIS_OPERATIONS_H

#include <hiredis/hiredis.h>

void redis_connect();  // Updated function name
void fetch_thread_list();
void fetch_thread_details(const char *thread_id);
void update_thread_title(const char *thread_id, const char *new_title);
void delete_thread(const char *thread_id);
void delete_message(const char *thread_id, const char *message_id);

#endif
