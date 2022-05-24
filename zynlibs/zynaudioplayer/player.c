/*  Audio file player library for Zynthian
    Copyright (C) 2021 Brian Walton <brian@riban.co.uk>
    License: LGPL V3
*/

#include "player.h"

#include <stdio.h> //provides printf
#include <string.h> //provides strcmp, memset
#include <jack/jack.h> //provides interface to JACK
#include <jack/midiport.h> //provides JACK MIDI interface
#include <jack/ringbuffer.h> //provides jack ring buffer
#include <sndfile.h> //provides sound file manipulation
#include <samplerate.h> //provides samplerate conversion
#include <pthread.h> //provides multithreading
#include <unistd.h> //provides usleep
#include <stdlib.h> //provides exit
#include <arpa/inet.h> // provides inet_pton
#include <fcntl.h> //provides fcntl
#include <math.h> // provides pow

#ifdef ENABLE_OSC
#include "osc.h"
#endif //ENABLE_OSC

#define MAX_PLAYERS 16 // Maximum quanity of audio players the library can host

enum playState {
    STOPPED		= 0,
    STARTING	= 1,
    PLAYING		= 2,
    STOPPING	= 3
};

enum seekState {
    IDLE        = 0, // Not seeking
    SEEKING     = 1, // Seeking within file
    LOADING     = 2, // Seek complete, loading data from file
    LOOPING     = 3 // Reached end of file, need to load from start
};

struct AUDIO_PLAYER {
    unsigned int handle;

    jack_port_t* jack_out_a;
    jack_port_t* jack_out_b;
    jack_port_t * jack_midi_in;
    jack_client_t * jack_client;

    uint8_t file_open; // 0=file closed, 1=file opening, 2=file open - used to flag thread to close file or thread to flag file failed to open
    uint8_t file_read_status; // File reading status (IDLE|SEEKING|LOADING)

    uint8_t play_state; // Current playback state (STOPPED|STARTING|PLAYING|STOPPING)
    uint8_t loop; // 1 to loop at end of song
    float gain; // Audio level (volume) 0..1
    int track_a; // Which track to playback to left output (-1 to mix all stereo pairs)
    int track_b; // Which track to playback to right output (-1 to mix all stereo pairs)
    unsigned int buffer_size; // Quantity of frames read from file
    unsigned int buffer_count; // Factor by which ring buffer is larger than buffer
    unsigned int src_quality; // SRC quality [0..4]

    // Value of data at last notification
    uint8_t last_play_state;
    uint8_t last_loop;
    float last_position;
    float last_gain;
    int last_track_a;
    int last_track_b;
    unsigned int last_buffer_size;
    unsigned int last_buffer_count;
    unsigned int last_src_quality;


    struct SF_INFO  sf_info; // Structure containing currently loaded file info
    pthread_t file_thread; // ID of file reader thread
    // Note that jack_ringbuffer handles bytes so need to convert data between bytes and floats
    jack_ringbuffer_t * ringbuffer_a; // Used to pass A samples from file reader to jack process
    jack_ringbuffer_t * ringbuffer_b; // Used to pass B samples from file reader to jack process
    jack_nframes_t play_pos_frames; // Current playback position in frames since start of audio
    size_t frames; // Quanity of frames after samplerate conversion
    char filename[128];
    uint8_t last_note_played; // MIDI note number of last note that triggered playback
    double src_ratio; // Samplerate ratio of file
    double src_ratio_inv; // Samplerate ratio inverted used for playback position updates
    int pitch_shift; // Factor of pitch shift
    unsigned int pitch_bend; // Amount of MIDI pitch bend applied (0..16383, centre=8192 (0x2000))
    void * cb_object; // Pointer to the object hosting the callback function
    cb_fn_t * cb_fn; // Pointer to function to receive notification of chage
};

// **** Global variables ****
struct AUDIO_PLAYER * g_players[MAX_PLAYERS];
jack_nframes_t g_samplerate = 44100; // Playback samplerate set by jackd
uint8_t g_debug = 0;
uint8_t g_last_debug = 0;

#define DPRINTF(fmt, args...) if(g_debug) printf(fmt, ## args)
    
// **** Internal (non-public) functions ****


static inline struct AUDIO_PLAYER * get_player(int player_handle) {
    if(player_handle > MAX_PLAYERS || player_handle < 0)
        return NULL;
    return g_players[player_handle];
}

#ifdef ENABLE_OSC
void* osc_thread_fn(void * param) {
    char buffer[2048]; // declare a 2Kb buffer to read packet data into
    int len = 0;
    const int osc_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(OSC_PORT);
    sin.sin_addr.s_addr = INADDR_ANY;
    bind(osc_fd, (struct sockaddr *) &sin, sizeof(struct sockaddr_in));
    printf("OSC server listening on port %d\n", OSC_PORT);
    tosc_message osc_msg;

    while(g_run_osc) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(osc_fd, &readSet);
        struct timeval timeout = {1, 0}; // select times out after 1 second
        if (select(osc_fd + 1, &readSet, NULL, NULL, &timeout) > 0) {
            struct sockaddr sa; // can be safely cast to sockaddr_in
            socklen_t sa_len = sizeof(struct sockaddr_in);
            int len = 0;
            const char* path;
            size_t player;
            while ((len = (int) recvfrom(osc_fd, buffer, sizeof(buffer), 0, &sa, &sa_len)) > 0) {
                if(!tosc_parseMessage(&osc_msg, buffer, len)) {
                    path = tosc_getAddress(&osc_msg);
                    if(!strncmp(path, "/player", 7)) {
                        path += 7;
                        player = atoi(path);
                        while(path[0] != '\0' && path[0] != '/')
                            ++path;
                        if(path[0] == '/') {
                            if(!strcmp(path, "/transport")) {
                                if(osc_msg.format[0] == 'i')
                                    if(tosc_getNextInt32(&osc_msg))
                                        start_playback(player);
                                    else
                                        stop_playback(player);
                            } else if(!strcmp(path, "/load")) {
                                if(osc_msg.format[0] == 's')
                                    load(player, tosc_getNextString(&osc_msg));
                            } else if(!strcmp(path, "/save")) {
                                if(osc_msg.format[0] == 's')
                                    save(player, tosc_getNextString(&osc_msg));
                            } else if(!strcmp(path, "/unload")) {
                                unload(player);
                            } else if(!strncmp(path, "/position", 9)) {
                                if(osc_msg.format[0] == 'f')
                                    set_position(player, tosc_getNextFloat(&osc_msg));
                            }
                            else if(!strcmp(path, "/loop")) {
                                if(osc_msg.format[0] == 'i')
                                    enable_loop(player, tosc_getNextInt32(&osc_msg));
                            }
                            else if(!strcmp(path, "/quality")) {
                                if(osc_msg.format[0] == 'i')
                                    set_src_quality(player, tosc_getNextInt32(&osc_msg));
                            } else if(!strcmp(path, "/gain")) {
                                if(osc_msg.format[0] == 'f')
                                    set_gain(player, tosc_getNextFloat(&osc_msg));
                            } else if(!strcmp(path, "/track_a")) {
                                if(osc_msg.format[0] == 'i')
                                    set_track_a(player, tosc_getNextInt32(&osc_msg));
                            } else if(!strcmp(path, "/track_b")) {
                                if(osc_msg.format[0] == 'i')
                                    set_track_b(player, tosc_getNextInt32(&osc_msg));
                            } else if(!strcmp(path, "/buffersize")) {
                                if(osc_msg.format[0] == 'i')
                                    set_buffer_size(player, tosc_getNextInt32(&osc_msg));
                            } else if(!strcmp(path, "/buffercount")) {
                                if(osc_msg.format[0] == 'i')
                                    set_buffer_count(player, tosc_getNextInt32(&osc_msg));
                            }
                        }
                    }
                }
            }
        }
        usleep(100000);
    }
    close(osc_fd);
    printf("OSC server stopped\n");
    pthread_exit(NULL);
}
#endif //ENABLE_OSC

void send_notifications(struct AUDIO_PLAYER * pPlayer, int param) {
    // Send dynamic OSC notifications within this thread, not jack process
    if(!pPlayer || pPlayer->file_open != 2)
        return;
    if((param == NOTIFY_ALL || param == NOTIFY_TRANSPORT) && pPlayer->last_play_state != pPlayer->play_state) {
        pPlayer->last_play_state = pPlayer->play_state;
        if(pPlayer->cb_fn)
            ((cb_fn_t*)pPlayer->cb_fn)(pPlayer->cb_object, NOTIFY_TRANSPORT, (float)(pPlayer->play_state));
        #ifdef ENABLE_OSC
        sprintf(g_oscpath, "/player%d/transport", pPlayer->handle);
        sendOscInt(g_oscpath, pPlayer->play_state);
        #endif //ENABLE_OSC
    }
    if((param == NOTIFY_ALL || param == NOTIFY_POSITION) && fabs(get_position(pPlayer->handle) - pPlayer->last_position) >= 0.1) {
        pPlayer->last_position = get_position(pPlayer->handle);
        if(pPlayer->cb_fn)
            ((cb_fn_t*)pPlayer->cb_fn)(pPlayer->cb_object, NOTIFY_POSITION, pPlayer->last_position);
        #ifdef ENABLE_OSC
        sprintf(g_oscpath, "/player%d/position", pPlayer->handle);
        sendOscFloat(g_oscpath, pPlayer->last_position);
        #endif //ENABLE_OSC
    }
    if((param == NOTIFY_ALL || param == NOTIFY_GAIN) && fabs(pPlayer->gain - pPlayer->last_gain) >= 0.01) {
        pPlayer->last_gain = pPlayer->gain;
        if(pPlayer->cb_fn)
            ((cb_fn_t*)pPlayer->cb_fn)(pPlayer->cb_object, NOTIFY_GAIN, (float)(pPlayer->gain));
        #ifdef ENABLE_OSC
        sprintf(g_oscpath, "/player%d/gain", pPlayer->handle);
        sendOscFloat(g_oscpath, pPlayer->gain);
        #endif //ENABLE_OSC
    }
    if((param == NOTIFY_ALL || param == NOTIFY_LOOP) && pPlayer->loop != pPlayer->last_loop) {
        pPlayer->last_loop = pPlayer->loop;
        if(pPlayer->cb_fn)
            ((cb_fn_t*)pPlayer->cb_fn)(pPlayer->cb_object, NOTIFY_LOOP, (float)(pPlayer->loop));
        #ifdef ENABLE_OSC
        sprintf(g_oscpath, "/player%d/loop", player->handle);
        sendOscInt(g_oscpath, pPlayer->loop);
        #endif //ENABLE_OSC
    }
    if((param == NOTIFY_ALL || param == NOTIFY_TRACK_A) && pPlayer->track_a != pPlayer->last_track_a) {
        pPlayer->last_track_a = pPlayer->track_a;
        if(pPlayer->cb_fn)
            ((cb_fn_t*)pPlayer->cb_fn)(pPlayer->cb_object, NOTIFY_TRACK_A, (float)(pPlayer->track_a));
        #ifdef ENABLE_OSC
        sprintf(g_oscpath, "/player%d/track_a", pPlayer->handle);
        sendOscInt(g_oscpath, pPlayer->track_a);
        #endif //ENABLE_OSC
    }
    if((param == NOTIFY_ALL || param == NOTIFY_TRACK_B) && pPlayer->track_b != pPlayer->last_track_b) {
        pPlayer->last_track_b = pPlayer->track_b;
        if(pPlayer->cb_fn)
            ((cb_fn_t*)pPlayer->cb_fn)(pPlayer->cb_object, NOTIFY_TRACK_B, (float)(pPlayer->track_b));
        #ifdef ENABLE_OSC
        sprintf(g_oscpath, "/player%d/track_b", pPlayer->handle);
        sendOscInt(g_oscpath, pPlayer->track_b);
        #endif //ENABLE_OSC
    }
    if((param == NOTIFY_ALL || param == NOTIFY_QUALITY) && pPlayer->src_quality != pPlayer->last_src_quality) {
        pPlayer->last_src_quality = pPlayer->src_quality;
        if(pPlayer->cb_fn)
            ((cb_fn_t*)pPlayer->cb_fn)(pPlayer->cb_object, NOTIFY_QUALITY, (float)(pPlayer->src_quality));
        #ifdef ENABLE_OSC
        sprintf(g_oscpath, "/player%d/src_quality", pPlayer->handle);
        sendOscInt(g_oscpath, pPlayer->src_quality);
        #endif //ENABLE_OSC
    }
    if((param == NOTIFY_ALL || param == NOTIFY_DEBUG) && g_debug != g_last_debug) {
        g_last_debug = g_debug;
        if(pPlayer->cb_fn)
            ((cb_fn_t*)pPlayer->cb_fn)(pPlayer->cb_object, NOTIFY_DEBUG, (float)(g_debug));
        #ifdef ENABLE_OSC
        sendOscInt("/debug", g_debug);
        #endif //ENABLE_OSC
    }
}

void* file_thread_fn(void * param) {
    struct AUDIO_PLAYER * pPlayer = (struct AUDIO_PLAYER *) (param);
    pPlayer->sf_info.format = 0; // This triggers sf_open to populate info structure
    pPlayer->ringbuffer_a = NULL;
    pPlayer->ringbuffer_b = NULL;
    float pBufferOut[pPlayer->buffer_size]; // Buffer used to write converted sample data to
    float pBufferIn[pPlayer->buffer_size]; // Buffer used to read sample data from file
    SRC_STATE* pSrcState = NULL;
    SRC_DATA srcData;
    size_t nMaxFrames = pPlayer->buffer_size / pPlayer->sf_info.channels;
    size_t nUnusedFrames = 0; // Quantity of samples in input buffer not used by SRC

    SNDFILE* pFile = sf_open(pPlayer->filename, SFM_READ, &pPlayer->sf_info);
    if(!pFile || pPlayer->sf_info.channels < 1) {
        pPlayer->file_open = 0;
        fprintf(stderr, "libaudioplayer error: failed to open file %s: %s\n", pPlayer->filename, sf_strerror(pFile));
        pthread_exit(NULL);
    }
    if(pPlayer->sf_info.channels < 0) {
        pPlayer->file_open = 0;
        fprintf(stderr, "libaudioplayer error: file %s has no tracks\n", pPlayer->filename);
        int nError = sf_close(pFile);
        if(nError != 0)
            fprintf(stderr, "libaudioplayer error: failed to close file with error code %d\n", nError);
        pthread_exit(NULL);
    }
    if(pPlayer->file_open) {
        pPlayer->cb_fn = NULL;
        pPlayer->cb_object = NULL;
        pPlayer->last_play_state = -1;
        pPlayer->last_position = -1;
        pPlayer->play_pos_frames = 0;
        pPlayer->file_read_status = SEEKING;
        pPlayer->ringbuffer_a = jack_ringbuffer_create(pPlayer->buffer_size * pPlayer->buffer_count * sizeof(float));
        jack_ringbuffer_mlock(pPlayer->ringbuffer_a);
        pPlayer->ringbuffer_b = jack_ringbuffer_create(pPlayer->buffer_size * pPlayer->buffer_count * sizeof(float));
        jack_ringbuffer_mlock(pPlayer->ringbuffer_b);

        // Initialise samplerate converter
        srcData.data_in = pBufferIn;
        srcData.data_out = pBufferOut;
        pPlayer->src_ratio = (float)g_samplerate / pPlayer->sf_info.samplerate;
        if(pPlayer->src_ratio)
            pPlayer->src_ratio_inv = 1 / pPlayer->src_ratio;
        srcData.src_ratio = pPlayer->src_ratio;
        pPlayer->pitch_shift = 0;
        pPlayer->pitch_bend = 0x2000;
        srcData.output_frames = pPlayer->buffer_size / pPlayer->sf_info.channels;
        if(pPlayer->sf_info.samplerate)
            pPlayer->frames = pPlayer->sf_info.frames * pPlayer->src_ratio;
        else
            pPlayer->frames = pPlayer->sf_info.frames;
        int nError;
        pSrcState = src_new(pPlayer->src_quality, pPlayer->sf_info.channels, &nError);

        pPlayer->file_open = 2;
        DPRINTF("Opened file '%s' with samplerate %u, duration: %f\n", pPlayer->filename, pPlayer->sf_info.samplerate, get_duration(pPlayer->handle));
    }

    while(pPlayer->file_open == 2) {
        if(pPlayer->file_read_status == SEEKING) {
            // Main thread has signalled seek within file
            jack_ringbuffer_reset(pPlayer->ringbuffer_a);
            jack_ringbuffer_reset(pPlayer->ringbuffer_b);
            srcData.src_ratio = pPlayer->src_ratio * pow(1.059463094359, pPlayer->pitch_shift);
            size_t nNewPos = pPlayer->play_pos_frames;
            if(srcData.src_ratio) {
                nNewPos = pPlayer->play_pos_frames / srcData.src_ratio;
                pPlayer->src_ratio_inv = 1 / srcData.src_ratio;
            }
            sf_seek(pFile, nNewPos, SEEK_SET);
            DPRINTF("Seeking to %u frames (%fs) src ratio=%f\n", nNewPos, get_position(pPlayer->handle), srcData.src_ratio);
            pPlayer->file_read_status = LOADING;
            src_reset(pSrcState);
            nUnusedFrames = 0;
            nMaxFrames = pPlayer->buffer_size / pPlayer->sf_info.channels;
            srcData.end_of_input = 0;
        } else if(pPlayer->file_read_status == LOOPING) {
            // Reached end of file and need to read from start
            sf_seek(pFile, 0, SEEK_SET);
            pPlayer->file_read_status = LOADING;
            src_reset(pSrcState);
            srcData.end_of_input = 0;
            nMaxFrames = pPlayer->buffer_size / pPlayer->sf_info.channels;
            nUnusedFrames = 0;
        }
        if(pPlayer->file_read_status == LOADING)
        {
            // Load block of data from file to SRC or output buffer
            int nFramesRead;
            if(srcData.src_ratio == 1.0) {
                // No SRC required so populate SRC output buffer directly
                nFramesRead = sf_readf_float(pFile, pBufferOut, nMaxFrames);
            } else {
                // Populate SRC input buffer before SRC process
                nMaxFrames = (pPlayer->buffer_size / pPlayer->sf_info.channels) - nUnusedFrames;
                nFramesRead = sf_readf_float(pFile, pBufferIn + nUnusedFrames * pPlayer->sf_info.channels, nMaxFrames);
            }
            if(nFramesRead == nMaxFrames) {
                // Filled buffer from file so probably more data to read
                srcData.end_of_input = 0;
                DPRINTF("libzynaudioplayer read %u frames into ring buffer\n", nFramesRead);
            }
            else if(pPlayer->loop) {
                // Short read - looping so fill from start of file
                pPlayer->file_read_status = LOOPING;
                srcData.end_of_input = 1;
                DPRINTF("libzynaudioplayer read to end of input file - setting loading status to looping\n");
            } else {
                // Short read - assume at end of file
                pPlayer->file_read_status = IDLE;
                srcData.end_of_input = 1;
                DPRINTF("libzynaudioplayer read to end of input file - setting loading status to IDLE\n");
            }
            if(srcData.src_ratio != 1.0) {
                // We need to perform SRC on this block of code
                srcData.input_frames = nFramesRead;
                int rc = src_process(pSrcState, &srcData);
                nFramesRead = srcData.output_frames_gen;
                nUnusedFrames = nMaxFrames - srcData.input_frames_used;
                if(rc) {
                    DPRINTF("SRC failed with error %d, %u frames generated\n", nFramesRead, srcData.output_frames_gen);
                } else {
                    //DPRINTF("SRC suceeded - %u frames generated, %u frames unused\n", srcData.output_frames_gen, nUnusedFrames);
                }
                // Shift unused samples to start of buffer
                memcpy(pBufferIn, pBufferIn + (nFramesRead - nUnusedFrames) * sizeof(float), nUnusedFrames * sizeof(float));
            } else {
                //DPRINTF("No SRC, read %u frames\n", nFramesRead);
            }
            
            // Wait until there is sufficient space in ring buffer to add new sample data
            while(jack_ringbuffer_write_space(pPlayer->ringbuffer_a) < nFramesRead * sizeof(float)
                && jack_ringbuffer_write_space(pPlayer->ringbuffer_b) < nFramesRead * sizeof(float)) {
                send_notifications(pPlayer, NOTIFY_ALL);
                usleep(10000); //!@todo Tune sleep for responsiveness / cpu load
                if(pPlayer->file_open == 0)
                    break;
            }
            if(pPlayer->file_open == 2 && pPlayer->file_read_status == LOADING) {
                // Demux samples and populate playback ring buffers
                for(size_t frame = 0; frame < nFramesRead; ++frame) {
                    float fA = 0.0, fB = 0.0;
                    size_t sample = frame * pPlayer->sf_info.channels;
                    if(pPlayer->sf_info.channels > 1) {
                        if(pPlayer->track_a < 0) {
                            // Send sum of odd channels to A
                            for(int track = 0; track < pPlayer->sf_info.channels; track += 2)
                                fA += pBufferOut[sample + track] / (pPlayer->sf_info.channels / 2);
                        } else {
                            // Send pPlayer->track to A
                            fA = pBufferOut[sample + pPlayer->track_a];
                        }
                        if(pPlayer->track_b < 0) {
                            // Send sum of odd channels to B
                            for(int track = 0; track + 1 < pPlayer->sf_info.channels; track += 2)
                                fB += pBufferOut[sample + track + 1] / (pPlayer->sf_info.channels / 2);
                        } else {
                            // Send pPlayer->track to B
                            fB = pBufferOut[sample + pPlayer->track_b];
                        }
                    } else {
                        // Mono source so send to both outputs
                        fA = pBufferOut[sample] / 2;
                        fB = pBufferOut[sample] / 2;
                    }
                    int nWrote = jack_ringbuffer_write(pPlayer->ringbuffer_b, (const char*)(&fB), sizeof(float));
                    if(sizeof(float) < jack_ringbuffer_write(pPlayer->ringbuffer_a, (const char*)(&fA), nWrote)) {
                         // Shouldn't underun due to previous wait for space but just in case...
                        fprintf(stderr, "Underrun during writing to ringbuffer - this should never happen!!!\n");
                        break;
                    }
                }
            }
        }
        usleep(10000);
        send_notifications(pPlayer, NOTIFY_ALL);
    }
    pPlayer->play_state = STOPPED; // Already stopped when clearing file_open but let's be sure!
    if(pFile) {
        int nError = sf_close(pFile);
        if(nError != 0)
            fprintf(stderr, "libaudioplayer error: failed to close file with error code %d\n", nError);
        else
            pPlayer->filename[0] = '\0';
    }
    pPlayer->play_pos_frames = 0;
    pPlayer->cb_fn = NULL;
    pPlayer->cb_object = NULL;
    if(pPlayer->ringbuffer_a)
        jack_ringbuffer_free(pPlayer->ringbuffer_a);
    if(pPlayer->ringbuffer_b)
        jack_ringbuffer_free(pPlayer->ringbuffer_b);
    if(pSrcState)
        pSrcState = src_delete(pSrcState);

#ifdef ENABLE_OSC
    sprintf(g_oscpath, "/player%d/load", pPlayer->handle);
    sendOscString(g_oscpath, pPlayer->filename);
#endif //ENABLE_OSC

    DPRINTF("File reader thread ended\n");
    pthread_exit(NULL);
}

/**** player instance functions take 'handle' param to identify player instance****/

uint8_t load(int player_handle, const char* filename, void* cb_object, cb_fn_t cb_fn) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer)
        return 0;
    unload(player_handle);
    pPlayer->cb_fn;
    pPlayer->cb_object = NULL;
    pPlayer->track_a = 0;
    pPlayer->track_b = 0;
    strcpy(pPlayer->filename, filename);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    pPlayer->file_open = 1;
    if(pthread_create(&pPlayer->file_thread, &attr, file_thread_fn, pPlayer)) {
        fprintf(stderr, "libzynaudioplayer error: failed to create file reading thread\n");
        unload(player_handle);
        return 0;
    }
    while(pPlayer->file_open == 1) {
        usleep(10000); //!@todo Optimise wait for file open
    }

    if(pPlayer->file_open) {
        pPlayer->cb_object = cb_object;
        pPlayer->cb_fn = cb_fn;
        #ifdef ENABLE_OSC
        sprintf(g_oscpath, "/player%d/load", pPlayer->handle);
        sendOscString(g_oscpath, pPlayer->filename);
        sprintf(g_oscpath, "/player%d/duration", pPlayer->handle);
        sendOscFloat(g_oscpath, get_duration(pPlayer->handle));
        #endif //ENABLE_OSC
    }
    return (pPlayer->file_open == 2);
}

void unload(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open == 0)
        return;
    stop_playback(player_handle);
    pPlayer->file_open = 0;
    void* status;
    pthread_join(pPlayer->file_thread, &status);
    pPlayer->filename[0] = '\0';
}

uint8_t save(int player_handle, const char* filename) {
    //!@todo Implement save
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return 0;
    return 0;
}

const char* get_filename(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return "";
    return pPlayer->filename;
}

float get_duration(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(pPlayer && pPlayer->file_open == 2 && pPlayer->sf_info.samplerate)
        return (float)pPlayer->sf_info.frames / pPlayer->sf_info.samplerate;
    return 0.0f;
}

void set_position(int player_handle, float time) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return;
    if(time >= get_duration(player_handle))
        time = get_duration(player_handle);
    double frames = pPlayer->src_ratio * time * pPlayer->sf_info.samplerate;
    if(frames >= pPlayer->frames)
        frames = pPlayer->frames -1;
    pPlayer->play_pos_frames = frames;
    pPlayer->file_read_status = SEEKING;
    jack_ringbuffer_reset(pPlayer->ringbuffer_b);
    jack_ringbuffer_reset(pPlayer->ringbuffer_a);
    DPRINTF("New position requested, setting loading status to SEEKING\n");

#ifdef ENABLE_OSC
    sprintf(g_oscpath, "/player%d/position", player_handle);
    sendOscInt(g_oscpath, (int)time);
#endif //ENABLE_OSC
}

float get_position(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(pPlayer && pPlayer->file_open == 2 && pPlayer->sf_info.samplerate)
        return pPlayer->src_ratio_inv * pPlayer->play_pos_frames / pPlayer->sf_info.samplerate;
    return 0.0;
}

void enable_loop(int player_handle, uint8_t bLoop) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer)
        return;
    pPlayer->loop = bLoop;
    if(pPlayer->file_open != 2 && bLoop && pPlayer->file_read_status == IDLE) {
        pPlayer->file_read_status = LOOPING;
        DPRINTF("Looping requested, setting loading status to SEEKING\n");
    }
    send_notifications(pPlayer, NOTIFY_LOOP);
}

uint8_t is_loop(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return 0;
    return(pPlayer->loop);
}

void start_playback(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(pPlayer && pPlayer->jack_client && pPlayer->file_open == 2 && pPlayer->play_state != PLAYING)
        pPlayer->play_state = STARTING;
    send_notifications(pPlayer, NOTIFY_TRANSPORT);
}

void stop_playback(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(pPlayer && pPlayer->play_state != STOPPED)
        pPlayer->play_state = STOPPING;
    send_notifications(pPlayer, NOTIFY_TRANSPORT);
}

uint8_t get_playback_state(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return STOPPED;
    return pPlayer->play_state;
}

int get_samplerate(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return g_samplerate;
    return pPlayer->sf_info.samplerate;
}

int get_channels(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return 0;
    return pPlayer->sf_info.channels;
}

int get_frames(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return 0;
    return pPlayer->sf_info.frames;
}

int get_format(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return 0;
    return pPlayer->sf_info.format;
}

/*** Private functions not exposed as external C functions (not declared in header) ***/

// Clean up before library unloads
void end() {
    for(int player_handle = 0; player_handle < MAX_PLAYERS; ++player_handle)
        remove_player(player_handle);

#ifdef ENABLE_OSC
    g_run_osc = 0;
    void* status;
    pthread_join(g_osc_thread, &status);
#endif //ENABLE_OSC
}

void remove_player(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer)
        return;
    unload(player_handle);
    jack_client_close(pPlayer->jack_client);

    unsigned int count = 0;
    for(size_t i = 0; i < MAX_PLAYERS; ++i) {
        if(g_players[i] == pPlayer)
            g_players[i] = NULL;
        if(g_players[i])
            ++count;
    }
    free(pPlayer);
    if(count == 0)
        end();
}

// Handle JACK process callback
int on_jack_process(jack_nframes_t nFrames, void * arg) {
    size_t count;
    uint8_t eof = 0;
    struct AUDIO_PLAYER * pPlayer = (struct AUDIO_PLAYER *) (arg);
    if(!pPlayer || pPlayer->file_open != 2)
        return 0;
    jack_default_audio_sample_t *pOutA = (jack_default_audio_sample_t*)jack_port_get_buffer(pPlayer->jack_out_a, nFrames);
    jack_default_audio_sample_t *pOutB = (jack_default_audio_sample_t*)jack_port_get_buffer(pPlayer->jack_out_b, nFrames);
    count = 0;

    if(pPlayer->play_state == STARTING && pPlayer->file_read_status != SEEKING)
        pPlayer->play_state = PLAYING;

    if(pPlayer->play_state == PLAYING || pPlayer->play_state == STOPPING) {
        count = jack_ringbuffer_read(pPlayer->ringbuffer_a, (char*)pOutA, nFrames * sizeof(float));
        jack_ringbuffer_read(pPlayer->ringbuffer_b, (char*)pOutB, count);
        eof = (pPlayer->file_read_status == IDLE && jack_ringbuffer_read_space(pPlayer->ringbuffer_a) == 0);
    }
    count /= sizeof(float);
    for(size_t offset = 0; offset < count; ++offset) {
        // Set volume / gain / level
        pOutA[offset] *= pPlayer->gain;
        pOutB[offset] *= pPlayer->gain;
    }
    pPlayer->play_pos_frames += count;
    if(pPlayer->play_pos_frames >= pPlayer->frames)
        pPlayer->play_pos_frames %= pPlayer->frames;

    if(pPlayer->play_state == STOPPING || pPlayer->play_state == PLAYING && eof) {
        // Soft mute (not perfect for short last period of file but better than nowt)
        for(size_t offset = 0; offset < count; ++offset) {
            pOutA[offset] *= 1.0 - ((float)offset / count);
            pOutB[offset] *= 1.0 - ((float)offset / count);
        }
        pPlayer->play_state = STOPPED;
        if(eof) {
            // Recue to start if played to end of file
            pPlayer->play_pos_frames = 0;
            pPlayer->file_read_status = SEEKING;
        }

        DPRINTF("libzynaudioplayer: Stopped. Used %u frames from %u in buffer to soft mute (fade). Silencing remaining %u frames (%u bytes)\n", count, nFrames, nFrames - count, (nFrames - count) * sizeof(jack_default_audio_sample_t));
    }

    // Silence remainder of frame
    memset(pOutA + count, 0, (nFrames - count) * sizeof(jack_default_audio_sample_t));
    memset(pOutB + count, 0, (nFrames - count) * sizeof(jack_default_audio_sample_t));

    // Process MIDI input
    void* pMidiBuffer = jack_port_get_buffer(pPlayer->jack_midi_in, nFrames);
    jack_midi_event_t midiEvent;
    jack_nframes_t nCount = jack_midi_get_event_count(pMidiBuffer);
    for(jack_nframes_t i = 0; i < nCount; i++)
    {
        jack_midi_event_get(&midiEvent, pMidiBuffer, i);
        uint8_t cmd = midiEvent.buffer[0] & 0xF0;
        if((cmd == 0x80 || cmd == 0x90 && midiEvent.buffer[2] == 0) && pPlayer->last_note_played == midiEvent.buffer[1]) {
            // Note off
            stop_playback(pPlayer->handle);
            pPlayer->pitch_shift = 0;
            pPlayer->last_note_played = 0;
        } else if(cmd == 0x90) {
            // Note on
                stop_playback(pPlayer->handle);
                pPlayer->pitch_shift = 60 - midiEvent.buffer[1];
                set_position(pPlayer->handle, 0);
                start_playback(pPlayer->handle);
                pPlayer->last_note_played = midiEvent.buffer[1];
        } else if(cmd == 0xE0) {
            // Pitchbend
            //!@todo Pitchbend does nothing - want it to affect live playback (different to note-on that affects whole file)
            pPlayer->pitch_bend = midiEvent.buffer[1] + 128 * midiEvent.buffer[2];
        }
        #ifdef ENABLE_MIDI
        else if(cmd == 0xB0) {
            // CC
            switch(midiEvent.buffer[1])
            {
                case 1:
                    set_position(pPlayer->handle, midiEvent.buffer[2] * get_duration(pPlayer->handle) / 127);
                    break;
                case 7:
                    pPlayer->gain = (float)midiEvent.buffer[2] / 100.0;
                    break;
                case 68:
                    if(midiEvent.buffer[2] > 63)
                        start_playback(pPlayer->handle);
                    else
                        stop_playback(pPlayer->handle);
                    break;
                case 69:
                    enable_loop(pPlayer->handle, midiEvent.buffer[2] > 63);
                    break;
            }
        }
        #endif //ENABLE_MIDI
    }
    return 0;
}

// Handle JACK process callback
int on_jack_samplerate(jack_nframes_t nFrames, void *pArgs) {
    DPRINTF("libzynaudioplayer: Jack sample rate: %u\n", nFrames);
    g_samplerate = nFrames;
    return 0;
}
 
static void lib_init(void) { 
#ifdef ENABLE_OSC
    // Initialise OSC clients
    g_oscfd = socket(AF_INET, SOCK_DGRAM, 0);
    for(int i = 0; i < MAX_OSC_CLIENTS; ++i) {
        memset(g_oscClient[i].sin_zero, '\0', sizeof g_oscClient[i].sin_zero);
        g_oscClient[i].sin_family = AF_INET;
        g_oscClient[i].sin_port = htons(OSC_PORT);
        g_oscClient[i].sin_addr.s_addr = 0;
    }
    fcntl(g_oscfd, F_SETFL, O_NONBLOCK); // set the socket to non-blocking

    // Initialise OSC server
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    if(pthread_create(&g_osc_thread, &attr, osc_thread_fn, NULL))
        fprintf(stderr, "libzynaudioplayer error: failed to create OSC listening thread\n");
#endif //ENABLE_OSC

    printf("libzynaudioplayer initialised\n");
}

int init() {
    int player_handle;
    struct AUDIO_PLAYER * pPlayer = NULL;
    for(player_handle = 0; player_handle < MAX_PLAYERS; ++player_handle) {
        if(g_players[player_handle])
            continue;
        pPlayer = malloc(sizeof(struct AUDIO_PLAYER));
        break;
    }
    if(!pPlayer) {
        fprintf(stderr, "Failed to create instance of audio player\n");
        return -1;
    }

    pPlayer->file_open = 0;
    pPlayer->file_read_status = IDLE;
    pPlayer->play_state = STOPPED;
    pPlayer->loop = 0;
    pPlayer->play_pos_frames = 0;
    pPlayer->src_quality = SRC_SINC_FASTEST;
    pPlayer->filename[0] = '\0';
    pPlayer->gain = 1.0;
    pPlayer->track_a = 0;
    pPlayer->last_track_a = 0;
    pPlayer->last_track_b = 0;
    pPlayer->track_b = 0;
    pPlayer->handle = player_handle;
    pPlayer->buffer_size = 48000;
    pPlayer->buffer_count = 5;
    pPlayer->frames = 0;
    pPlayer->last_note_played = 0;

    char *sServerName = NULL;
    jack_status_t nStatus;
    jack_options_t nOptions = JackNoStartServer;
    char client_name[] = "audioplayer_xxx";
    sprintf(client_name, "audio_player_%03d", player_handle);

    if((pPlayer->jack_client = jack_client_open(client_name, nOptions, &nStatus, sServerName)) == 0) {
        fprintf(stderr, "libaudioplayer error: failed to start jack client: %d\n", nStatus);
        return -1;
    }

    // Create audio output ports
    if (!(pPlayer->jack_out_a = jack_port_register(pPlayer->jack_client, "output_a", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0))) {
        fprintf(stderr, "libaudioplayer error: cannot register audio output port A\n");
        return -1;
    }
    if (!(pPlayer->jack_out_b = jack_port_register(pPlayer->jack_client, "output_b", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0))) {
        fprintf(stderr, "libaudioplayer error: cannot register audio output port B\n");
        jack_port_unregister(pPlayer->jack_client, pPlayer->jack_out_a);
        return -1;
    }

    // Create MIDI input port
    if(!(pPlayer->jack_midi_in = jack_port_register(pPlayer->jack_client, "input", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0)))
    {
        fprintf(stderr, "libzynaudioplayer error: cannot register MIDI input port\n");
        jack_port_unregister(pPlayer->jack_client, pPlayer->jack_out_a);
        jack_port_unregister(pPlayer->jack_client, pPlayer->jack_out_b);
        return -1;
    }

    // Register the callback to process audio and MIDI
    jack_set_process_callback(pPlayer->jack_client, on_jack_process, pPlayer);
    jack_set_sample_rate_callback(pPlayer->jack_client, on_jack_samplerate, 0);

    if(jack_activate(pPlayer->jack_client)) {
        fprintf(stderr, "libaudioplayer error: cannot activate client\n");
        return -1;
    }

    g_players[player_handle] = pPlayer;
    g_samplerate = jack_get_sample_rate(pPlayer->jack_client);
    //printf("libzynaudioplayer: Created new audio player\n");
    return player_handle;
}

const char* get_jack_client_name(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer)
        return "";
    return jack_get_client_name(pPlayer->jack_client);
}

uint8_t set_src_quality(int player_handle, unsigned int quality) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return 0;
    if(quality > SRC_LINEAR)
        return 0;
    pPlayer->src_quality = quality;
    send_notifications(pPlayer, NOTIFY_QUALITY);
    return 1;
}

unsigned int get_src_quality(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return 2;
    return pPlayer->src_quality;
}

void set_gain(int player_handle, float gain) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return;
    if(gain < 0 || gain > 2)
        return;
    pPlayer->gain = gain;
    send_notifications(pPlayer, NOTIFY_GAIN);
}

float get_gain(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return 0.0;
    return pPlayer->gain;
}

void set_track_a(int player_handle, int track) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return;
    if(pPlayer->file_open == 2 && track < pPlayer->sf_info.channels) {
        if(pPlayer->sf_info.channels == 1)
            pPlayer->track_a = 0;
        else
            pPlayer->track_a = track;
    }
    set_position(player_handle, get_position(player_handle));
    send_notifications(pPlayer, NOTIFY_TRACK_A);
}

void set_track_b(int player_handle, int track) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return;
    if(pPlayer->file_open == 2 && track < pPlayer->sf_info.channels) {
        if(pPlayer->sf_info.channels == 1)
            pPlayer->track_b = 0;
        else
            pPlayer->track_b = track;
    }
    set_position(player_handle, get_position(player_handle));
    send_notifications(pPlayer, NOTIFY_TRACK_B);
}

int get_track_a(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return 0;
    return pPlayer->track_a;
}

int get_track_b(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(!pPlayer || pPlayer->file_open != 2)
        return 0;
    return pPlayer->track_b;
}

void set_buffer_size(int player_handle, unsigned int size) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(pPlayer && pPlayer->file_open == 0)
        pPlayer->buffer_size = size;
}

unsigned int get_buffer_size(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(pPlayer)
        return pPlayer->buffer_size;
    return 0;
}

void set_buffer_count(int player_handle, unsigned int count) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(pPlayer && pPlayer->file_open == 0 && count > 1)
        pPlayer->buffer_count = count;
}

unsigned int get_buffer_count(int player_handle) {
    struct AUDIO_PLAYER * pPlayer = get_player(player_handle);
    if(pPlayer)
        return pPlayer->buffer_count;
    return 0;
}

/**** Global functions ***/

float get_file_duration(const char* filename) {
    SF_INFO info;
    info.format = 0;
    info.samplerate = 0;
    SNDFILE* pFile = sf_open(filename, SFM_READ, &info);
    sf_close(pFile);
    if(info.samplerate)
        return (float)info.frames / info.samplerate;
    return 0.0f;
}

const char* get_file_info(const char* filename, int type) {
    SF_INFO info;
    info.format = 0;
    info.samplerate = 0;
    SNDFILE* pFile = sf_open(filename, SFM_READ, &info);
    const char* pValue = sf_get_string(pFile, type);
    if(pValue) {
        sf_close(pFile);
        return pValue;
    }
    sf_close(pFile);
    return "";
}

void enable_debug(int enable) {
    printf("libaudioplayer setting debug mode %s\n", enable?"on":"off");
    g_debug = enable;
}

int is_debug() {
    return g_debug;
}

unsigned int get_player_count() {
    int count = 0;
    for(int i = 0; i < MAX_PLAYERS; ++i)
        if(g_players)
            ++count;
    return count;
}

#ifdef ENABLE_OSC
int addOscClient(const char* client) {
    int index = _addOscClient(client);
    if(index != -1) {
        for(int player_handle = 0; player_handle < MAX_PLAYERS; ++player_handle) {
            struct AUDIO_PLAYER * pPlayer = g_players[player_handle];
            if(!pPlayer || pPlayer->file_open != 2)
                continue;
            sprintf(g_oscpath, "/player%d/open", pPlayer->handle);
            sendOscString(g_oscpath, pPlayer->filename);
            sprintf(g_oscpath, "/player%d/duration", pPlayer->handle);
            sendOscFloat(g_oscpath, get_duration(pPlayer->handle));
            send_notifications(pPlayer->handle, NOTIFY_ALL);
        }
    }
    return index;
}
#endif //ENABLE_OSC