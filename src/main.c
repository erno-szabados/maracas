// src/main.c
#include <gtk/gtk.h>
#include <pulse/pulseaudio.h>
#include <errno.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *label;
    pa_mainloop *mainloop;
    pa_mainloop_api *mainloop_api;
    pa_context *context;
} MaracasApp;

static void state_callback(pa_context *c, void *userdata) {
    MaracasApp *app = (MaracasApp *)userdata;
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY:
            g_print("PulseAudio connection ready.\n");
            // Next steps will go here (e.g., listing sources)
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

static void activate(GtkApplication *app, gpointer user_data) {
    MaracasApp *maracas_app = (MaracasApp *)g_malloc0(sizeof(MaracasApp));
    maracas_app->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(maracas_app->window), "Maracas");
    gtk_window_set_default_size(GTK_WINDOW(maracas_app->window), 300, 150);
    g_signal_connect(maracas_app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL); 

    maracas_app->label = gtk_label_new("Connecting to PulseAudio...");
    gtk_container_add(GTK_CONTAINER(maracas_app->window), maracas_app->label);
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

    pa_context_set_state_callback(maracas_app->context, state_callback, maracas_app);
    pa_context_connect(maracas_app->context, NULL, PA_CONTEXT_NOFLAGS, NULL);

    pa_mainloop_run(maracas_app->mainloop, NULL); 
}

int main(int argc, char *argv[]) {
    GtkApplication *app;
    int status;

    app = gtk_application_new("org.esgdevq.maracas", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
