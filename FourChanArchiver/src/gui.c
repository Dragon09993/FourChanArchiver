#include <gtk/gtk.h>
#include <hiredis/hiredis.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include "../include/settings.h"
#include "../include/gui.h"
#include "../include/redis_operations.h"

// Global variables for GUI widgets
GtkWidget *host_entry, *port_entry, *board_entry;
GtkWidget *thread_tree_view;  // TreeView for displaying thread titles
GtkWidget *search_entry;      // Search bar for filtering threads
GtkWidget *output_text_view;

static redisContext *redis_context = NULL;

extern char redis_host[256];
extern int redis_port;
extern char board[256];

// Function prototypes
void connect_to_redis();
void load_thread_titles(const char *filter);
void add_thread_from_scraper(const char *board, const char *thread_id);
void delete_thread_from_scraper(const char *board, const char *thread_id);
void delete_selected_thread();
void open_settings_dialog();
void refresh_thread_list_callback();
void open_add_thread_dialog();
void create_thread_list_tree_view(GtkWidget *parent_box);
void create_main_window();
void save_settings_callback();  // Declaration added for save_settings_callback
void open_set_title_dialog();  // Function to open dialog to set thread title
void set_thread_title(const char *board, const char *thread_id, const char *title);  // Function to set thread title
const char *get_selected_thread_id_and_title(char *thread_title_out, size_t title_len);  // Helper function to get ID and title
void update_stored_threads_from_scraper(); //updates threads and generates Markdown on server.
void detect_docker_host();//see if there is Docker host to auto-populate redis host ip
void *update_stored_threads_thread(void *arg);
void generate_audio_for_thread(); // Function for generating audio in a thread
void on_generate_audio_button_clicked(GtkWidget *widget, gpointer data);
void show_context_menu(GtkWidget *widget, GdkEventButton *event, gpointer data);
void copy_thread_id_callback(GtkWidget *menu_item, gpointer data);

gboolean update_output_text_view_safe(gchar *output);
gboolean update_output_text_view(gchar *output);

// Connect to Redis using stored settings
void connect_to_redis() {
    if (redis_context) redisFree(redis_context);
    redis_context = redisConnect(redis_host, redis_port);

    if (redis_context == NULL || redis_context->err) {
        fprintf(stderr, "Could not connect to Redis: %s\n", redis_context ? redis_context->errstr : "Unknown error");
    }
}

// Helper function to get the selected thread ID from TreeView
const char *get_selected_thread_id() {
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(thread_tree_view));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *thread_title;
        gtk_tree_model_get(model, &iter, 0, &thread_title, -1);

        // Extract the thread ID (assuming it’s the first part of the title)
        static char thread_id[256];
        sscanf(thread_title, "%255s", thread_id);
        g_free(thread_title);

        return thread_id;
    }
    return NULL;
}

// Delete the selected thread with confirmation
void delete_selected_thread() {
    const char *thread_id = get_selected_thread_id();
    if (thread_id == NULL) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "No thread selected.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    // Confirmation dialog
    GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "Are you sure you want to delete thread %s?", thread_id);
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response == GTK_RESPONSE_YES) {
        delete_thread_from_scraper(board, thread_id);  // Perform deletion
    }
}

// Delete a thread using the scraper script
void delete_thread_from_scraper(const char *board, const char *thread_id) {
    char command[512];
    snprintf(command, sizeof(command), "/usr/bin/docker exec 4chan_scraper-scraper-1 python FourChanScraper.py delete_thread %s %s", board, thread_id);


    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to execute scraper command\n");
        return;
    }

    // Capture command output and append to text view
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(output_text_view));
    char output[1024];
    while (fgets(output, sizeof(output), fp) != NULL) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, output, -1);
    }
    pclose(fp);

    load_thread_titles(NULL);  // Refresh the list to remove the deleted thread
    
}

void load_thread_titles(const char *filter) {
    if (!redis_context) {
        fprintf(stderr, "Redis connection not established.\n");
        return;
    }

    GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(thread_tree_view)));
    gtk_list_store_clear(store);

    char pattern[256];
    snprintf(pattern, sizeof(pattern), "%s*_title", board);  // Matches titles of threads for the specified board
    redisReply *reply = redisCommand(redis_context, "KEYS %s", pattern);

    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; i++) {
            const char *title_key = reply->element[i]->str;
            redisReply *title_reply = redisCommand(redis_context, "GET %s", title_key);

            if (title_reply->type == REDIS_REPLY_STRING) {
                char thread_id[256];
                sscanf(title_key, "%*[^0123456789]%[0123456789]", thread_id);  // Extract thread_id

                // Retrieve the count associated with the thread ID
                char count_key[256];
                snprintf(count_key, sizeof(count_key), "%s%s_count", board, thread_id);
                redisReply *count_reply = redisCommand(redis_context, "GET %s", count_key);

                int count = (count_reply && count_reply->type == REDIS_REPLY_STRING) ? atoi(count_reply->str) : 0;

                // Retrieve the status associated with the thread ID
                char status_key[256];
                snprintf(status_key, sizeof(status_key), "%s%s_status", board, thread_id);
                redisReply *status_reply = redisCommand(redis_context, "GET %s", status_key);

                const char *status = (status_reply && status_reply->type == REDIS_REPLY_STRING) ? status_reply->str : "Unknown";

                // Apply filter if provided
                if (filter == NULL || strstr(title_reply->str, filter) != NULL || strstr(thread_id, filter) != NULL) {
                    GtkTreeIter iter;
                    gtk_list_store_append(store, &iter);
                    gtk_list_store_set(store, &iter,
                                       0, thread_id,
                                       1, title_reply->str,
                                       2, count,
                                       3, status,  // Set the status in the new column
                                       -1);
                }

                if (count_reply) freeReplyObject(count_reply);
                if (status_reply) freeReplyObject(status_reply);
            }

            freeReplyObject(title_reply);
        }
    }

    freeReplyObject(reply);
}




// Add a thread using the scraper script
void add_thread_from_scraper(const char *board, const char *thread_id) {
    char command[512];
    snprintf(command, sizeof(command), "/usr/bin/docker exec 4chan_scraper-scraper-1 python FourChanScraper.py scrape_thread %s %s", board, thread_id);

    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to execute scraper command\n");
        return;
    }

    // Capture command output and append to text view
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(output_text_view));
    char output[1024];
    while (fgets(output, sizeof(output), fp) != NULL) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, output, -1);
    }
    pclose(fp);

     load_thread_titles(NULL);  // Refresh the list to include the new thread
    
}

// Open dialog to add a new thread
void open_add_thread_dialog() {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Add Thread", NULL, GTK_DIALOG_MODAL, "_Add", GTK_RESPONSE_ACCEPT, "_Cancel", GTK_RESPONSE_CANCEL, NULL);
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();

    gtk_box_pack_start(GTK_BOX(content_area), gtk_label_new("Enter Thread ID:"), FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content_area), entry, FALSE, FALSE, 5);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *thread_id = gtk_entry_get_text(GTK_ENTRY(entry));
        add_thread_from_scraper(board, thread_id);
    }

    gtk_widget_destroy(dialog);
}

// Open settings dialog
// Modified open_settings_dialog to include the Detect Host button
void open_settings_dialog() {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Settings", NULL, GTK_DIALOG_MODAL, "_Save", GTK_RESPONSE_ACCEPT, "_Cancel", GTK_RESPONSE_CANCEL, NULL);
    host_entry = gtk_entry_new();
    port_entry = gtk_entry_new();
    board_entry = gtk_entry_new();

    gtk_entry_set_text(GTK_ENTRY(host_entry), redis_host);
    gtk_entry_set_text(GTK_ENTRY(port_entry), g_strdup_printf("%d", redis_port));
    gtk_entry_set_text(GTK_ENTRY(board_entry), board);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(content_area), gtk_label_new("Redis Host:"), FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content_area), host_entry, FALSE, FALSE, 5);

    // Add "Detect Host" button
    GtkWidget *detect_host_button = gtk_button_new_with_label("Detect Host");
    g_signal_connect(detect_host_button, "clicked", G_CALLBACK(detect_docker_host), NULL);
    gtk_box_pack_start(GTK_BOX(content_area), detect_host_button, FALSE, FALSE, 5);

    gtk_box_pack_start(GTK_BOX(content_area), gtk_label_new("Redis Port:"), FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content_area), port_entry, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content_area), gtk_label_new("Board:"), FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content_area), board_entry, FALSE, FALSE, 5);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        save_settings_callback();
    }

    gtk_widget_destroy(dialog);
}

// Create a TreeView with a single column for thread titles
// Initialize the TreeView and Context Menu
// Create a TreeView with columns for Thread ID, Title, and Count
void create_thread_list_tree_view(GtkWidget *parent_box) {
    thread_tree_view = gtk_tree_view_new();
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();

    // Model with four columns: Thread_ID, Title, Count, and Status
    GtkListStore *store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING);
    gtk_tree_view_set_model(GTK_TREE_VIEW(thread_tree_view), GTK_TREE_MODEL(store));
    g_object_unref(store);

    // Create Thread ID column
    GtkTreeViewColumn *id_column = gtk_tree_view_column_new_with_attributes("Thread ID", renderer, "text", 0, NULL);
    gtk_tree_view_column_set_sort_column_id(id_column, 0); // Enable sorting by Thread ID
    gtk_tree_view_append_column(GTK_TREE_VIEW(thread_tree_view), id_column);

    // Create Title column
    GtkTreeViewColumn *title_column = gtk_tree_view_column_new_with_attributes("Title", renderer, "text", 1, NULL);
    gtk_tree_view_column_set_sort_column_id(title_column, 1); // Enable sorting by Title
    gtk_tree_view_append_column(GTK_TREE_VIEW(thread_tree_view), title_column);

    // Create Count column
    GtkTreeViewColumn *count_column = gtk_tree_view_column_new_with_attributes("Count", renderer, "text", 2, NULL);
    gtk_tree_view_column_set_sort_column_id(count_column, 2); // Enable sorting by Count
    gtk_tree_view_append_column(GTK_TREE_VIEW(thread_tree_view), count_column);

    // Create Status column
    GtkTreeViewColumn *status_column = gtk_tree_view_column_new_with_attributes("Status", renderer, "text", 3, NULL);
    gtk_tree_view_column_set_sort_column_id(status_column, 3); // Enable sorting by Status
    gtk_tree_view_append_column(GTK_TREE_VIEW(thread_tree_view), status_column);

    // Enable right-click event for context menu
    g_signal_connect(thread_tree_view, "button-press-event", G_CALLBACK(show_context_menu), NULL);

    // Add the TreeView to a scrollable container
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled_window), thread_tree_view);
    gtk_box_pack_start(GTK_BOX(parent_box), scrolled_window, TRUE, TRUE, 5);
}



// Refresh the thread list based on the search filter
void refresh_thread_list_callback() {
    const char *filter_text = gtk_entry_get_text(GTK_ENTRY(search_entry));
    load_thread_titles(filter_text);
}

void create_main_window() {
    // Create the main window
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "FourChanArchiver");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 500);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Create buttons and add to the top bar
    GtkWidget *settings_button = gtk_button_new_with_label("Show Settings");
    g_signal_connect(settings_button, "clicked", G_CALLBACK(open_settings_dialog), NULL);

    GtkWidget *refresh_button = gtk_button_new_with_label("Refresh");
    g_signal_connect(refresh_button, "clicked", G_CALLBACK(refresh_thread_list_callback), NULL);

    GtkWidget *add_thread_button = gtk_button_new_with_label("Add Thread");
    g_signal_connect(add_thread_button, "clicked", G_CALLBACK(open_add_thread_dialog), NULL);

    GtkWidget *delete_thread_button = gtk_button_new_with_label("Delete Thread");
    g_signal_connect(delete_thread_button, "clicked", G_CALLBACK(delete_selected_thread), NULL);

    GtkWidget *set_title_button = gtk_button_new_with_label("Set Title");
    g_signal_connect(set_title_button, "clicked", G_CALLBACK(open_set_title_dialog), NULL);

    GtkWidget *update_stored_threads_button = gtk_button_new_with_label("Update Stored Threads");
    g_signal_connect(update_stored_threads_button, "clicked", G_CALLBACK(update_stored_threads_from_scraper), NULL);

    // Top bar with buttons
    GtkWidget *top_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(top_bar), settings_button, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(top_bar), refresh_button, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(top_bar), add_thread_button, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(top_bar), delete_thread_button, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(top_bar), set_title_button, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(top_bar), update_stored_threads_button, FALSE, FALSE, 5);

    // Audio button row
    GtkWidget *audio_button = gtk_button_new_with_label("Generate Audio for Selected Thread");
    g_signal_connect(audio_button, "clicked", G_CALLBACK(on_generate_audio_button_clicked), NULL);

    GtkWidget *audio_button_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(audio_button_row), audio_button, TRUE, TRUE, 5);

    // Create search bar and add it below the audio button row
    search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Search by ID or title...");
    g_signal_connect(search_entry, "changed", G_CALLBACK(refresh_thread_list_callback), NULL);

    GtkWidget *search_bar_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(search_bar_box), search_entry, TRUE, TRUE, 5);

    // Label for the title section
    GtkWidget *title_label = gtk_label_new("Stored Thread Titles:");
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);

    // Terminal-like output display area
    output_text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(output_text_view), FALSE);  // Make it read-only
    GtkWidget *output_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(output_scrolled_window, -1, 100);  // Fixed height for terminal view
    gtk_container_add(GTK_CONTAINER(output_scrolled_window), output_text_view);

    // Main vertical box to hold all components
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_pack_start(GTK_BOX(main_box), top_bar, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(main_box), audio_button_row, FALSE, FALSE, 5);  // Audio button row added here
    gtk_box_pack_start(GTK_BOX(main_box), search_bar_box, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(main_box), title_label, FALSE, FALSE, 5);

    // Add the thread list TreeView
    create_thread_list_tree_view(main_box);

    // Add the output text view below the thread list
    gtk_box_pack_start(GTK_BOX(main_box), output_scrolled_window, FALSE, FALSE, 5);

    // Add main box to the window
    gtk_container_add(GTK_CONTAINER(window), main_box);
    gtk_widget_show_all(window);
}



// Initialize GTK and start the main GUI loop
void initialize_gui(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    connect_to_redis();
    create_main_window();
    gtk_main();
}

void save_settings_callback() {
    const char *new_host = gtk_entry_get_text(GTK_ENTRY(host_entry));
    int new_port = atoi(gtk_entry_get_text(GTK_ENTRY(port_entry)));
    const char *new_board = gtk_entry_get_text(GTK_ENTRY(board_entry));

    // Assuming save_settings is a function that saves the settings
    save_settings(new_host, new_port, new_board);  
    strncpy(redis_host, new_host, sizeof(redis_host) - 1);
    redis_port = new_port;
    strncpy(board, new_board, sizeof(board) - 1);

    connect_to_redis();  // Reconnect to Redis with new settings
}

const char *get_selected_thread_id_and_title(char *thread_title_out, size_t title_len) {
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(thread_tree_view));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *thread_info;
        gtk_tree_model_get(model, &iter, 0, &thread_info, -1);  // Column 0 assumed to have thread ID

        // Define a static buffer for the thread ID
        static char thread_id[256];

        // Check if thread_info is NULL or too long for the buffer
        if (thread_info && strlen(thread_info) < sizeof(thread_id)) {
            strncpy(thread_id, thread_info, sizeof(thread_id) - 1);
            thread_id[sizeof(thread_id) - 1] = '\0';  // Null-terminate to avoid overflow
            g_free(thread_info);

            // Get the title from the appropriate column, ensure it fits in the buffer
            gchar *title;
            gtk_tree_model_get(model, &iter, 1, &title, -1);  // Assuming title is in column 1
            strncpy(thread_title_out, title, title_len - 1);
            thread_title_out[title_len - 1] = '\0';
            g_free(title);

            return thread_id;  // Return static thread ID buffer
        }

        g_free(thread_info);  // Ensure memory is freed even if there's an issue
    }
    return NULL;  // Return NULL if selection or retrieval fails
}





void open_set_title_dialog() {
    char current_title[256] = "";
    const char *thread_id = get_selected_thread_id_and_title(current_title, sizeof(current_title));

    if (thread_id == NULL) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "No thread selected.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    printf("DEBUG: Opening set title dialog with thread ID: %s and title: \"%s\"\n", thread_id, current_title);

    // Create the set title dialog
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Set Thread Title", NULL, GTK_DIALOG_MODAL, "_Set", GTK_RESPONSE_ACCEPT, "_Cancel", GTK_RESPONSE_CANCEL, NULL);
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    // Create the title entry and set it with the current title
    GtkWidget *title_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(title_entry), current_title); // Set the current title in the entry field

    gtk_box_pack_start(GTK_BOX(content_area), gtk_label_new("Thread Title:"), FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content_area), title_entry, FALSE, FALSE, 5);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *new_title = gtk_entry_get_text(GTK_ENTRY(title_entry));
        printf("DEBUG: Setting new title: %s for thread ID: %s\n", new_title, thread_id);  // Debug print
        set_thread_title(board, thread_id, new_title);  // Call function to set the new title
    }

    gtk_widget_destroy(dialog);
}





void set_thread_title(const char *board, const char *thread_id, const char *title) {
    printf("DEBUG: Executing set_thread_title with board: %s, thread ID: %s, title: %s\n", board, thread_id, title);

    char command[1024];
    snprintf(command, sizeof(command), "/usr/bin/docker exec 4chan_scraper-scraper-1 python FourChanScraper.py set_title %s %s \"%s\"", board, thread_id, title);

    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to execute set_title command\n");
        return;
    }

    // Capture command output and append to text view
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(output_text_view));
    char output[1024];
    while (fgets(output, sizeof(output), fp) != NULL) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, output, -1);
    }
    pclose(fp);
    
    printf("DEBUG: Title updated successfully.\n");

    load_thread_titles(NULL);  // Refresh the list to include the updated thread title
}

void update_stored_threads_from_scraper() {
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, update_stored_threads_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create thread for updating stored threads\n");
    } else {
        pthread_detach(thread_id);  // Automatically free resources when thread finishes
    }
}

// Function to detect Docker host IP and set it in the host entry field
void detect_docker_host() {
    char command[] = "docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' 4chan_scraper-redis-1";
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to run Docker inspect command\n");
        return;
    }

    char ip_address[256];
    if (fgets(ip_address, sizeof(ip_address), fp) != NULL) {
        ip_address[strcspn(ip_address, "\n")] = 0;  // Remove newline
        gtk_entry_set_text(GTK_ENTRY(host_entry), ip_address);
    } else {
        fprintf(stderr, "Failed to retrieve IP address\n");
    }
    pclose(fp);
}

// Function that will run in a separate thread to perform the update
void *update_stored_threads_thread(void *arg) {
    
    char command[512];
    snprintf(command, sizeof(command), "/usr/bin/docker exec 4chan_scraper-scraper-1 python FourChanScraper.py update_stored_threads %s", board);
    
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to execute scraper command\n");
        return NULL;
    }

    // Read the command output and update the output text view
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(output_text_view));
    char output[1024];

    // While reading the output, periodically update the UI
    while (fgets(output, sizeof(output), fp) != NULL) {
        g_idle_add((GSourceFunc)update_output_text_view, g_strdup(output));
    }

    pclose(fp);
    return NULL;
}

// Function to update the output text view from the main thread
gboolean update_output_text_view(gchar *output) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(output_text_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, output, -1);
    g_free(output);  // Free the output string after updating
    return FALSE;    // Return FALSE to indicate one-time execution
}

// Function to be used in a worker thread, updated for g_idle_add use
void *generate_audio_thread_function(void *arg) {
    const char *thread_id = (const char *)arg;

    // Command to call the Python script for audio generation
    char command[512];
    snprintf(command, sizeof(command), "/usr/bin/docker exec 4chan_scraper-scraper-1 python3 FourChanScraper.py generate_audio %s %s", board, thread_id);

    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to execute generate_audio command\n");
        return NULL;
    }

    char output[1024];
    while (fgets(output, sizeof(output), fp) != NULL) {
        // Schedule the output to be inserted on the main thread
        g_idle_add((GSourceFunc)update_output_text_view_safe, g_strdup(output));
    }
    pclose(fp);
    return NULL;
}

// Function called when "Generate Audio" button is clicked
void on_generate_audio_button_clicked(GtkWidget *widget, gpointer data) {
    const char *thread_id = get_selected_thread_id();

    if (thread_id == NULL) {
        GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "No thread selected.");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    // Create a new thread for audio generation
    pthread_t audio_thread;
    if (pthread_create(&audio_thread, NULL, generate_audio_thread_function, (void *)thread_id) != 0) {
        fprintf(stderr, "Failed to create audio generation thread\n");
        return;
    }

    // Detach the thread to allow it to run independently
    pthread_detach(audio_thread);
}

// Function to create and show the context menu
void show_context_menu(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    // Check for right-click only (button 3)
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) { 
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(thread_tree_view));
        GtkTreePath *path;

        // Ensure the right-click occurred on a valid row in the TreeView
        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(thread_tree_view), event->x, event->y, &path, NULL, NULL, NULL)) {
            gtk_tree_selection_unselect_all(selection);
            gtk_tree_selection_select_path(selection, path);  // Select the clicked row
            gtk_tree_path_free(path);

            // Create and show the context menu
            GtkWidget *menu = gtk_menu_new();
            GtkWidget *copy_menu_item = gtk_menu_item_new_with_label("Copy Thread ID");

            g_signal_connect(copy_menu_item, "activate", G_CALLBACK(copy_thread_id_callback), widget);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy_menu_item);
            gtk_widget_show_all(menu);
            
            gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        }
    }
}

// Callback for "Copy Thread ID"
void copy_thread_id_callback(GtkWidget *menu_item, gpointer data) {
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(thread_tree_view));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *thread_id;
        gtk_tree_model_get(model, &iter, 0, &thread_id, -1); // Retrieve Thread ID (column 0)

        // Copy thread ID to clipboard
        GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_text(clipboard, thread_id, -1);

        g_free(thread_id);
        printf("Thread ID copied to clipboard.\n"); // Optional: For debugging or feedback
    }
}



// Wrapper function to safely add text to GtkTextView from any thread
gboolean update_output_text_view_safe(gchar *output) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(output_text_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, output, -1);
    g_free(output);  // Free the output string after updating
    return FALSE;    // Return FALSE to indicate one-time execution
}