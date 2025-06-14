// Maracas - A simple audio recorder using GTK and PulseAudio
#include "maracas.h"
#include "wavwrite.h"
#include <time.h>     // For time(), localtime(), strftime()
#include <unistd.h>   // For stat()
#include <sys/stat.h> // For stat()
#include <string.h>   // For snprintf(), strerror()
#include <stdlib.h>   // For getenv()
#include <errno.h>    // For errno
#include <limits.h>   // For PATH_MAX

static gboolean update_timer_label(gpointer user_data)
{
    MaracasApp *app = (MaracasApp *)user_data;
    time_t now = time(NULL);
    time_t elapsed = now - app->start_time;
    int hours = elapsed / 3600;
    int minutes = (elapsed % 3600) / 60;
    int seconds = elapsed % 60;

    gchar *time_str = g_strdup_printf("%02d:%02d:%02d", hours, minutes, seconds);
    gtk_label_set_text(GTK_LABEL(app->timer_label), time_str);
    g_free(time_str);

    return G_SOURCE_CONTINUE; // Keep the timer running
}

static void free_audio_source_info(gpointer data)
{
    AudioSourceInfo *info = (AudioSourceInfo *)data;
    g_free(info->name);
    g_free(info->description);
    g_free(info);
}

static void record_state_callback(pa_stream *s, void *userdata)
{
    MaracasApp *app = (MaracasApp *)userdata;
    switch (pa_stream_get_state(s))
    {
    case PA_STREAM_READY:
        //g_print("Audio recording stream is ready.\n");
        // Uncork the stream to start recording
        if (pa_stream_cork(app->record_stream, 0, NULL, NULL) < 0)
        {
            g_printerr("pa_stream_cork() failed: %s\n", pa_strerror(pa_context_errno(app->context)));
            pa_stream_unref(app->record_stream);
            app->record_stream = NULL;
            return;
        }
        break;
    case PA_STREAM_FAILED:
    case PA_STREAM_TERMINATED:
        //g_printerr("Audio recording stream terminated: %s\n", pa_strerror(pa_context_errno(app->context)));
        if (app->record_stream)
        {
            pa_stream_unref(app->record_stream);
            app->record_stream = NULL;
        }
        break;
    case PA_STREAM_CREATING:
        //g_print("Connecting audio recording stream...\n");
        break;
    case PA_STREAM_UNCONNECTED:
    default:
        g_print("Audio recording stream state: %i\n", pa_stream_get_state(s));
        break;
    }
}

static void record_data_callback(pa_stream *s, size_t length, void *userdata)
{
    MaracasApp *app = (MaracasApp *)userdata;
    const void *data;
    size_t bytes_read;

    if (pa_stream_peek(s, &data, &bytes_read) < 0)
    {
        g_printerr("pa_stream_peek() failed: %s\n", pa_strerror(pa_context_errno(pa_stream_get_context(s))));
        return;
    }

    if (data && app->output_file)
    {
        fwrite(data, 1, bytes_read, app->output_file); // Write audio data to file
        // g_print("Saved %zu bytes of audio data.\n", bytes_read);
    }

    pa_stream_drop(s);
}

static void start_recording(GtkWidget *widget, MaracasApp *app)
{
    // Check if recording is already in progress
    if (app->record_stream)
    {
        // Stop recording
        stop_recording(app);

        // Update button caption to "Record"
        gtk_button_set_label(GTK_BUTTON(widget), "Record");
        //g_print("Recording stopped.\n");
        return;
    }

    gint active_index = gtk_combo_box_get_active(GTK_COMBO_BOX(app->source_combo));
    if (active_index >= 0 && app->sources)
    {
        GSList *item = g_slist_nth(app->sources, active_index);
        if (item)
        {
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
            if (!app->record_stream)
            {
                g_printerr("pa_stream_new() failed: %s\n", pa_strerror(pa_context_errno(app->context)));
                return;
            }

            // Set stream state callback
            pa_stream_set_state_callback(app->record_stream, record_state_callback, app);

            // Set stream read callback
            pa_stream_set_read_callback(app->record_stream, record_data_callback, app);

            // Connect the stream to the source for recording
            if (pa_stream_connect_record(app->record_stream, app->selected_source_name, NULL, PA_STREAM_START_CORKED) < 0)
            {
                g_printerr("pa_stream_connect_record() failed: %s\n", pa_strerror(pa_context_errno(app->context)));
                pa_stream_unref(app->record_stream);
                app->record_stream = NULL;
                return;
            }

            // --- Generate unique filename on Desktop ---
            char filename[PATH_MAX];
            char timestamp[20];
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);

            // Format timestamp YYYYMMDD_HHMMSS
            strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);

            // Get the localized Desktop directory
            const char *desktop_dir = g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
            if (!desktop_dir)
            {
                g_printerr("Warning: Could not get Desktop directory. Saving to current directory.\n");
                desktop_dir = g_strdup("."); // Fallback to current directory
            }

            char base_path[PATH_MAX];
            snprintf(base_path, sizeof(base_path), "%s/%s", desktop_dir, timestamp);
            int counter = 1;
            struct stat buffer;

            // Loop to find a non-existent filename like YYYYMMDD_HHMMSS_N.wav
            while (1)
            {
                snprintf(filename, sizeof(filename), "%s_%d.wav", base_path, counter);

                // Check if file exists using stat()
                if (stat(filename, &buffer) != 0)
                {
                    if (errno == ENOENT)
                    {
                        // File does not exist, use this filename
                        break;
                    }
                    else
                    {
                        // Another error occurred (e.g., permission issue on path)
                        g_printerr("Error checking file path %s: %s\n", filename, strerror(errno));
                        pa_stream_disconnect(app->record_stream); // Clean up stream before returning
                        pa_stream_unref(app->record_stream);
                        app->record_stream = NULL;
                        return; // Abort recording attempt
                    }
                }
                // File exists, increment counter
                counter++;
                if (counter > 999)
                { // Safety break
                    g_printerr("Error: Could not find a unique filename after %d tries.\n", counter - 1);
                    pa_stream_disconnect(app->record_stream);
                    pa_stream_unref(app->record_stream);
                    app->record_stream = NULL;
                    return;
                }
            }
            // --- End Generate filename ---

            // Open the output file with the generated name
            app->output_file = fopen(filename, "wb");
            if (!app->output_file)
            {
                g_printerr("Failed to open output file '%s' for writing: %s\n", filename, strerror(errno));
                pa_stream_disconnect(app->record_stream); // Clean up stream before returning
                pa_stream_unref(app->record_stream);
                app->record_stream = NULL;
                return;
            }
            g_print("Saving recording to: %s\n", filename);

            // Write the WAV header
            write_wav_header(app->output_file, 44100, 1, 16); // 44.1kHz, mono, 16-bit

            // Update button caption to "Stop"
            gtk_button_set_label(GTK_BUTTON(widget), "Stop");
            g_print("Recording started.\n");

            // Start the timer
            app->start_time = time(NULL);
            app->timer_id = g_timeout_add_seconds(1, update_timer_label, app);
        }
        else
        {
            g_printerr("Error: Could not retrieve selected source information.\n");
        }
    }
    else
    {
        g_print("Please select an audio source before recording.\n");
    }
}

static void stop_recording(MaracasApp *app)
{
    if (app->record_stream)
    {
        pa_stream_cork(app->record_stream, 1, NULL, NULL);
        pa_stream_disconnect(app->record_stream);
        pa_stream_unref(app->record_stream);
        app->record_stream = NULL;
    }

    if (app->output_file)
    {
        finalize_wav_file(app->output_file);
        fclose(app->output_file);
        app->output_file = NULL;
        //g_print("Recording stopped and file saved.\n");
    }

     // Stop the timer
     if (app->timer_id != 0)
     {
         g_source_remove(app->timer_id);
         app->timer_id = 0;
     }
}

// Modify the signature to match the "destroy" signal if needed,
// although Gtk sends the widget as the first arg, the user_data is what we need.
// Keep the original signature if it works, but ensure all resources are freed.
static void cleanup_maracas_app(GtkWidget *widget, gpointer user_data)
{                                              // Match typical GCallback signature for "destroy"
    MaracasApp *app = (MaracasApp *)user_data; // Cast user_data
    //g_print("Cleaning up MaracasApp...\n");

    // Stop recording if active (handles stream and file)
    stop_recording(app);

    // Disconnect and clean up PulseAudio context and mainloop
    if (app->context)
    {
        pa_context_disconnect(app->context);
        pa_context_unref(app->context);
        app->context = NULL;
    }
    if (app->mainloop)
    {
        pa_mainloop_free(app->mainloop);
        app->mainloop = NULL;
    }

    // Free the sources list
    if (app->sources)
    {
        g_slist_free_full(app->sources, free_audio_source_info);
        app->sources = NULL;
    }

    // Free selected source name string
    g_free(app->selected_source_name);
    app->selected_source_name = NULL;

    // Free the application structure itself
    g_free(app);
    //g_print("MaracasApp cleanup complete.\n");
}

static void source_info_callback(pa_context *c, const pa_source_info *info, int eol, void *userdata)
{
    MaracasApp *app = (MaracasApp *)userdata;
    if (info)
    {
        AudioSourceInfo *source_info = g_new(AudioSourceInfo, 1);
        source_info->name = g_strdup(info->name);
        source_info->description = g_strdup(info->description);
        app->sources = g_slist_prepend(app->sources, source_info);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->source_combo), info->description);
    }
    else if (eol)
    {
        // Reverse the list to maintain the order PulseAudio provided
        app->sources = g_slist_reverse(app->sources);
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->source_combo), 0); // Select the first source
    }
}

static void context_state_callback(pa_context *c, void *userdata)
{
    MaracasApp *app = (MaracasApp *)userdata;
    switch (pa_context_get_state(c))
    {
    case PA_CONTEXT_READY:
        //g_print("PulseAudio connection ready. Listing input sources...\n");
        app->sources = NULL;                                                  // Initialize the list
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->source_combo)); // Clear previous entries
        pa_operation *o;
        if (!(o = pa_context_get_source_info_list(app->context, source_info_callback, app)))
        {
            g_printerr("pa_context_get_source_info_list() failed: %s\n", pa_strerror(pa_context_errno(app->context)));
        }
        if (o)
            pa_operation_unref(o);
        break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        //g_printerr("PulseAudio connection terminated: %s\n", pa_strerror(pa_context_errno(c)));
        g_application_quit(G_APPLICATION(gtk_window_get_application(GTK_WINDOW(app->window))));
        break;
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
    default:
        //g_print("PulseAudio connection state: %i\n", pa_context_get_state(c));
        break;
    }
}

static gboolean pulse_mainloop_poll(gpointer user_data)
{
    MaracasApp *app = (MaracasApp *)user_data;
    int retval;
    pa_mainloop_iterate(app->mainloop, 0, &retval);
    return G_SOURCE_CONTINUE; // Keep polling
}

static void activate(GtkApplication *app, gpointer user_data)
{
    MaracasApp *maracas_app = (MaracasApp *)g_malloc0(sizeof(MaracasApp));
    maracas_app->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(maracas_app->window), "Maracas");
    gtk_window_set_default_size(GTK_WINDOW(maracas_app->window), 300, 150);
    gtk_window_set_resizable(GTK_WINDOW(maracas_app->window), FALSE);
    g_signal_connect(maracas_app->window, "destroy", G_CALLBACK(cleanup_maracas_app), maracas_app);

    // Create a vertical box to hold widgets
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(maracas_app->window), vbox);

    maracas_app->label = gtk_label_new("Select an audio source:");
    gtk_box_pack_start(GTK_BOX(vbox), maracas_app->label, FALSE, FALSE, 0);

    // Create the source selection combo box
    maracas_app->source_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(vbox), maracas_app->source_combo, FALSE, FALSE, 0);

    // Create the record button
    maracas_app->record_button = gtk_button_new_with_label("Record");
    gtk_box_pack_start(GTK_BOX(vbox), maracas_app->record_button, FALSE, FALSE, 0);
    g_signal_connect(maracas_app->record_button, "clicked", G_CALLBACK(start_recording), maracas_app); // Connect the button

    maracas_app->timer_label = gtk_label_new("00:00:00"); // Initial time
    gtk_box_pack_start(GTK_BOX(vbox), maracas_app->timer_label, FALSE, FALSE, 0);

    gtk_widget_show_all(maracas_app->window);

    maracas_app->mainloop = pa_mainloop_new();
    if (!maracas_app->mainloop)
    {
        g_printerr("Failed to create PulseAudio mainloop.\n");
        g_free(maracas_app);
        return;
    }

    maracas_app->mainloop_api = pa_mainloop_get_api(maracas_app->mainloop);
    maracas_app->context = pa_context_new(maracas_app->mainloop_api, "Maracas");
    if (!maracas_app->context)
    {
        g_printerr("Failed to create PulseAudio context.\n");
        pa_mainloop_free(maracas_app->mainloop);
        g_free(maracas_app);
        return;
    }

    pa_context_set_state_callback(maracas_app->context, context_state_callback, maracas_app);
    pa_context_connect(maracas_app->context, NULL, PA_CONTEXT_NOFLAGS, NULL);

    // Integrate PulseAudio main loop with GTK main loop
    maracas_app->pulse_poll_id = g_idle_add(pulse_mainloop_poll, maracas_app); // Store the ID if needed for removal
}

int main(int argc, char *argv[])
{
    GtkApplication *app;
    int status;

    app = gtk_application_new("org.esgdev.maracas", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}