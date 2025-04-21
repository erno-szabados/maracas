#include <stdio.h>
#include <stdint.h>
#include <gtk/gtk.h>
#include <pulse/pulseaudio.h>
#include <errno.h>
#include <glib/gi18n.h>

#ifndef __MARACAS_H__
#define __MARACAS_H__

/// @brief Maracas application structure
typedef struct {
    GtkWidget *window;
    GtkWidget *source_combo;
    GtkWidget *record_button;
    GtkWidget *label;
    pa_mainloop *mainloop;
    pa_mainloop_api *mainloop_api;
    pa_context *context;
    GSList *sources;
    guint pulse_poll_id;
    pa_stream *record_stream; // PulseAudio stream for recording
    gchar *selected_source_name; // Name of the selected source
    FILE *output_file; 
} MaracasApp;
/// @brief Audio source information structure
typedef struct {
    gchar *name;
    gchar *description;
} AudioSourceInfo;

static void write_wav_header(FILE *file, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample);

/// @brief  Free the audio source information
/// @param data Pointer to the audio source information
static void free_audio_source_info(gpointer data);

/// @brief Callback function to handle source information
/// @param c PulseAudio context
/// @param userdata User data passed to the callback
static void context_state_callback(pa_context *c, void *userdata);

/// @brief Mainloop poll function for PulseAudio
/// @param user_data User data passed to the function
/// @return         
static gboolean pulse_mainloop_poll(gpointer user_data) ;

/// @brief Initialize the GTK application
/// @param app The GTK application
/// @param user_data User data passed to the function
static void activate(GtkApplication *app, gpointer user_data);

/**
 * @brief Start the audio recording process.
 * @param widget The button that was clicked.
 * @param app The MaracasApp instance containing the application state.
 */
static void start_recording(GtkWidget *widget, MaracasApp *app);

static void stop_recording(MaracasApp *app);

static void cleanup_maracas_app(GtkWidget *widget, gpointer user_data);

#endif // __MARACAS_H__