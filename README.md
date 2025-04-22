# Maracas

Version 0.1

A simple audio recorder application built with GTK and PulseAudio. 

## Description

Maracas allows users to select an audio input source and record audio, saving it as a WAV file to the desktop. It shows the time elapsed since beginning of the recording.

## Dependencies

-   GTK+ 3.0
-   PulseAudio

## Building

This project uses Autotools. To build:

```bash
./configure
make
```

## Running
After building, run the application from the src directory:
```bash
./maracas
```


### TODO

- allow user to select output file
- allow wav settings (sample rate, etc.)
- RPM package