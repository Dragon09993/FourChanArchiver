#include <gtk/gtk.h>
#include "../include/gui.h"
#include "../include/settings.h"


int main(int argc, char *argv[]) {
    fprintf(stderr, "Starting 4CHARK:");
    load_settings();  // Load settings at program start
    initialize_gui(argc, argv);  // Start GUI
    return 0;
}
