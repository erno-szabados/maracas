// src/main.c
#include <gtk/gtk.h>
#include <pulse/pulseaudio.h>
#include <errno.h>
#include <glib/gi18n.h> // For g_slist

typedef struct {
    GtkWidget *window;
    GtkWidget *source_combo; // New ComboBox widget
    GtkWidget *record_button; // We'll add a record button later
    GtkWidget *label; // Placeholder label
    pa_mainloop *mainloop;
    pa_mainloop_api *mainloop_api;
    pa_context *context;
    GSList *sources; // List to store audio source information
} MaracasApp;

typedef struct {
    gchar *name;
    gchar *description;
} AudioSourceInfo;

static void free_audio_source_info(gpointer data) {
    AudioSourceInfo *info = (AudioSourceInfo *)data;
    g_free(info->name);
    g_free(info->description);
    g_free(info);
}

static void source_info_callback(pa_context *c, const pa_source_info *info, int eol, void *userdata) {
    MaracasApp *app = (MaracasApp *)userdata;
    if (info) {
        AudioSourceInfo *source_info = g_new(AudioSourceInfo, 1);
        source_info->name = g_strdup(info->name);
        source_info->description = g_strdup(info->description);
        app->sources = g_slist_prepend(app->sources, source_info);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->source_combo), info->description);
    } else if (eol) {
        // Reverse the list to maintain the order PulseAudio provided
        app->sources = g_slist_reverse(app->sources);
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->source_combo), 0); // Select the first source
    }
}

static void context_state_callback(pa_context *c, void *userdata) {
    MaracasApp *app = (MaracasApp *)userdata;
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY:
            g_print("PulseAudio connection ready. Listing input sources...\n");
            app->sources = NULL; // Initialize the list
            gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->source_combo)); // Clear previous entries
            pa_operation *o;
            if (!(o = pa_context_get_source_info_list(app->context, source_info_callback, app))) {
                g_printerr("pa_context_get_source_info_list() failed: %s\n", pa_strerror(pa_context_errno(app->context)));
            }
            if (o)
                pa_operation_unref(o);
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            g_printerr("PulseAudio connection failed: %s\n", pa_strerror(pa_context_errno(c)));
            g_application_quit(G_APPLICATION(gtk_window_get_application(GTK_WINDOW(app->window))));
            break;
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
        default:
            g_print("PulseAudio connection state: %i\n", pa_context_get_state(c));
            break;
    }
}

static gboolean pulse_mainloop_poll(gpointer user_data) {
    MaracasApp *app = (MaracasApp *)user_data;
    int retval;
    pa_mainloop_iterate(app->mainloop, 0, &retval);
    return G_SOURCE_CONTINUE; // Keep polling
}

static void activate(GtkApplication *app, gpointer user_data) {
    MaracasApp *maracas_app = (MaracasApp *)g_malloc0(sizeof(MaracasApp));
    maracas_app->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(maracas_app->window), "Maracas");
    gtk_window_set_default_size(GTK_WINDOW(maracas_app->window), 300, 150);
    g_signal_connect(maracas_app->window, "destroy", G_CALLBACK(gdk_window_destroy), maracas_app->window);
    g_signal_connect(app, "shutdown", G_CALLBACK(g_slist_free_full), maracas_app->sources); // Free the source list on shutdown

    // Create a vertical box to hold widgets
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(maracas_app->window), vbox);

    // Create the source selection combo box
    maracas_app->source_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(vbox), maracas_app->source_combo, FALSE, FALSE, 0);

    // Placeholder label (we might remove this later)
    maracas_app->label = gtk_label_new("Select an audio source:");
    gtk_box_pack_start(GTK_BOX(vbox), maracas_app->label, FALSE, FALSE, 0);

    gtk_widget_show_all(maracas_app->window);

    maracas_app->mainloop = pa_mainloop_new();
    if (!maracas_app->mainloop) {
        g_printerr("Failed to create PulseAudio mainloop.\n");
        g_free(maracas_app);
        return;
    }

    maracas_app->mainloop_api = pa_mainloop_get_api(maracas_app->mainloop);
    maracas_app->context = pa_context_new(maracas_app->mainloop_api, "Maracas");
    if (!maracas_app->context) {
        g_printerr("Failed to create PulseAudio context.\n");
        pa_mainloop_free(maracas_app->mainloop);
        g_free(maracas_app);
        return;
    }

    pa_context_set_state_callback(maracas_app->context, context_state_callback, maracas_app);
    pa_context_connect(maracas_app->context, NULL, PA_CONTEXT_NOFLAGS, NULL);

    // Integrate PulseAudio main loop with GTK main loop
    g_idle_add(pulse_mainloop_poll, maracas_app);
}

int main(int argc, char *argv[]) {
    GtkApplication *app;
    int status;

    app = gtk_application_new("org.esgdev.maracas", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(g_slist_free_full), NULL); // Fix: Free the list on app shutdown
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}