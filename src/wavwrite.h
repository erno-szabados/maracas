#include <stdio.h>
#include <stdint.h>

#ifndef __WAVWRITE_H__
#define __WAVWRITE_H__

void write_wav_header(FILE *file, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample);

void finalize_wav_file(FILE *file);

#endif // __WAVWRITE_H__