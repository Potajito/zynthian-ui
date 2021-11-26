/*  Audio file player library for Zynthian
    Copyright (C) 2021 Brian Walton <brian@riban.co.uk>
    License: LGPL V3
*/

#include <cstdint>

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief  Initialise library
*/
void init();

/** @brief  Enable debug output
*   @param  bEnable True to enable, false to disable
*/
void enableDebug(bool bEnable);

/** @brief  Open audio file
*   @param  filename Full path and name of file to load
*   @retval bool True on success
*/
bool open(const char* filename);

/** @brief  Get duration of a file without loading it
*   @param  filename Full path and name of file to load
*   @retval float Duration is seconds. Zero if cannot open file.
*/
float getFileDuration(const char* filename);

/** @brief  Save audio file
*   @param  filename Full path and name of file to create or overwrite
*   @retval bool True on success
*/
bool save(const char* filename);

/** @brief  Close audio file clearing all data
*/
void closeFile();

/** @brief  Get filename of currently loaded file
*   @retval const char* Filename or emtpy string if no file loaded
*/
const char* getFilename();

/** @brief  Get duration of audio
*   @retval float Duration in seconds
*/
float getDuration();

/** @brief  Set playhead position
*   @param  time Time in seconds since start of audio
*/
void setPosition(float time);

/** @brief  Get playhead position
*   @retval float Time in seconds since start of audio
*/
float getPosition();

/** @brief  Set loop mode
*   @param  bLoop True to loop at end of audio
*/
void setLoop(bool bLoop);

/** @brief  Start playback
*/
void startPlayback();

/** @brief  Stop playback
*/
void stopPlayback();

/** @brief  Get play state
*   @retval uint8_t Play state [STOPPED|STARTING|PLAYING|STOPPING]
*/
uint8_t getPlayState();

/** @brief  Get samplerate of currently loaded file
*   @retval int Samplerate in samples per seconds
*/
int getSamplerate();

/** @brief  Get quantity of channels in currently loaded file
*   @retval int Quantity of channels, e.g. 2 for stereo
*/
int getChannels();

/** @brief  Get quantity of frames (samples) in currently loaded file
*   @retval int Quantity of frames
*/
int getFrames();

/** @brief  Get format of currently loaded file
*   @retval int Bitwise OR of major and minor format type and optional endianness value
*   @see    sndfile.h for supported formats
*/
int getFormat();

/** @brief  Get info from file meta data
*   @param  filename Full path and filename of audio file
*   @param  type Info type to retrieve [SF_STR_TITLE | SF_STR_COPYRIGHT | SF_STR_SOFTWARE | SF_STR_ARTIST | SF_STR_COMMENT | SF_STR_DATE| SF_STR_ALBUM | SF_STR_LICENSE | SF_STR_TRACKNUMBER | SF_STR_GENRE]
*   @retval const char Info value as c-string
*/
const char* getFileInfo(const char* filename, int type);

#ifdef __cplusplus
}
#endif

// Private functions not exposed to C library API

void *fileThread(void*);
int onJackXrun(void *pArgs);