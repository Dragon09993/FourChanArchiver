#ifndef GUI_H
#define GUI_H

void initialize_gui(int argc, char *argv[]);
void display_thread_list();
void show_thread_details(const char *thread_id);
void create_main_window();  // Proper declaration of create_main_window()

#endif
