// Maracas - A simple audio recorder using GTK and PulseAudio
#include "maracas.h"

static void free_audio_source_info(gpointer data) {
    AudioSourceInfo *info = (AudioSourceInfo *)data;
    g_free(info->name);
    g_free(info->description);
    g_free(info);
}

static void record_state_callback(pa_stream *s, void *userdata) {
    MaracasApp *app = (MaracasApp *)userdata;
    switch (pa_stream_get_state(s)) {
        case PA_STREAM_READY:
            g_print("Audio recording stream is ready.\n");
            // Uncork the stream to start recording
            if (pa_stream_cork(app->record_stream, 0, NULL, NULL) < 0) {
                g_printerr("pa_stream_cork() failed: %s\n", pa_strerror(pa_context_errno(app->context)));
                pa_stream_unref(app->record_stream);
                app->record_stream = NULL;
                return;
            }
            break;
        case PA_STREAM_FAILED:
        case PA_STREAM_TERMINATED:
            g_printerr("Audio recording stream failed: %s\n", pa_strerror(pa_context_errno(app->context)));
            if (app->record_stream) {
                pa_stream_unref(app->record_stream);
                app->record_stream = NULL;
            }
            break;
        case PA_STREAM_CREATING:
            g_print("Connecting audio recording stream...\n");
            break;
        case PA_STREAM_UNCONNECTED:
        default:
            g_print("Audio recording stream state: %i\n", pa_stream_get_state(s));
            break;
    }
}

static void record_data_callback(pa_stream *s, size_t length, void *userdata) {
    const void *data; // Pointer to the audio data
    size_t bytes_read;

    // Read the data from the stream
    if (pa_stream_peek(s, &data, &bytes_read) < 0) {
        g_printerr("pa_stream_peek() failed: %s\n", pa_strerror(pa_context_errno(pa_stream_get_context(s))));
        return;
    }

    if (data) {
        // Process the audio data (for now, just print the size)
        g_print("Received %zu bytes of audio data.\n", bytes_read);
    }

    // Drop the data after processing
    pa_stream_drop(s);
}

static void start_recording(GtkWidget *widget, MaracasApp *app) {
    gint active_index = gtk_combo_box_get_active(GTK_COMBO_BOX(app->source_combo));
    if (active_index >= 0 && app->sources) {
        GSList *item = g_slist_nth(app->sources, active_index);
        if (item) {
            AudioSourceInfo *selected_source = (AudioSourceInfo *)item->data;
            g_print("Recording requested from source: %s (%s)\n", selected_source->name, selected_source->description);

            // Store the selected source name
            g_free(app->selected_source_name);
            app->selected_source_name = g_strdup(selected_source->name);

            // Define the desired audio format
            static const pa_sample_spec ss = {
                .format = PA_SAMPLE_S16LE,
                .rate = 44100,
                .channels = 1 // Mono for simplicity
            };

            // Create the record stream
            app->record_stream = pa_stream_new(app->context, "Maracas Record", &ss, NULL);
            if (!app->record_stream) {
                g_printerr("pa_stream_new() failed: %s\n", pa_strerror(pa_context_errno(app->context)));
                return;
            }

            // Set stream state callback
            pa_stream_set_state_callback(app->record_stream, record_state_callback, app);

            // Set stream read callback
            pa_stream_set_read_callback(app->record_stream, record_data_callback, app);

            // Connect the stream to the source for recording
            if (pa_stream_connect_record(app->record_stream, app->selected_source_name, NULL, PA_STREAM_START_CORKED) < 0) {
                g_printerr("pa_stream_connect_record() failed: %s\n", pa_strerror(pa_context_errno(app->context)));
                pa_stream_unref(app->record_stream);
                app->record_stream = NULL;
                return;
            }

        } else {
            g_printerr("Error: Could not retrieve selected source information.\n");
        }
    } else {
        g_print("Please select an audio source before recording.\n");
    }
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
    g_signal_connect(app, "shutdown", G_CALLBACK(g_slist_free_full), maracas_app->sources);

    // Create a vertical box to hold widgets
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(maracas_app->window), vbox);

    // Placeholder label (we might remove this later)
    maracas_app->label = gtk_label_new("Select an audio source:");
    gtk_box_pack_start(GTK_BOX(vbox), maracas_app->label, FALSE, FALSE, 0);

    // Create the source selection combo box
    maracas_app->source_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(vbox), maracas_app->source_combo, FALSE, FALSE, 0);

    // Create the record button
    maracas_app->record_button = gtk_button_new_with_label("Record");
    gtk_box_pack_start(GTK_BOX(vbox), maracas_app->record_button, FALSE, FALSE, 0);
    g_signal_connect(maracas_app->record_button, "clicked", G_CALLBACK(start_recording), maracas_app); // Connect the button


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