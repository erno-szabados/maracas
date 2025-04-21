#include "wavwrite.h"

void write_wav_header(FILE *file, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample) {
    uint32_t byte_rate = sample_rate * channels * (bits_per_sample / 8);
    uint16_t block_align = channels * (bits_per_sample / 8);

    // Write the WAV header
    fwrite("RIFF", 1, 4, file); // Chunk ID
    fwrite("\0\0\0\0", 1, 4, file); // Chunk size (placeholder)
    fwrite("WAVE", 1, 4, file); // Format
    fwrite("fmt ", 1, 4, file); // Subchunk1 ID
    uint32_t subchunk1_size = 16; // PCM header size
    fwrite(&subchunk1_size, 4, 1, file);
    uint16_t audio_format = 1; // PCM format
    fwrite(&audio_format, 2, 1, file);
    fwrite(&channels, 2, 1, file);
    fwrite(&sample_rate, 4, 1, file);
    fwrite(&byte_rate, 4, 1, file);
    fwrite(&block_align, 2, 1, file);
    fwrite(&bits_per_sample, 2, 1, file);
    fwrite("data", 1, 4, file); // Subchunk2 ID
    fwrite("\0\0\0\0", 1, 4, file); // Subchunk2 size (placeholder)
}

// Update the WAV header with the correct file size
void finalize_wav_file(FILE *file) {
    uint32_t file_size = ftell(file);
    uint32_t data_chunk_size = file_size - 44; // Subtract header size

    fseek(file, 4, SEEK_SET);
    uint32_t riff_chunk_size = file_size - 8;
    fwrite(&riff_chunk_size, 4, 1, file);

    fseek(file, 40, SEEK_SET);
    fwrite(&data_chunk_size, 4, 1, file);

    fseek(file, 0, SEEK_END); // Return to the end of the file
}