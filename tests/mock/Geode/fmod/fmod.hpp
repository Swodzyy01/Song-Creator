#pragma once
// Compile-check mock of the FMOD subset used by src/gd/Songs.cpp (signatures follow docs.geode-sdk.org / FMOD 2.x).
typedef unsigned int FMOD_MODE;
enum FMOD_RESULT { FMOD_OK = 0, FMOD_ERR_FILE_EOF = 1, FMOD_ERR_FORMAT = 2 };
enum FMOD_SOUND_TYPE { FMOD_SOUND_TYPE_UNKNOWN };
enum FMOD_SOUND_FORMAT {
    FMOD_SOUND_FORMAT_NONE, FMOD_SOUND_FORMAT_PCM8, FMOD_SOUND_FORMAT_PCM16, FMOD_SOUND_FORMAT_PCM24,
    FMOD_SOUND_FORMAT_PCM32, FMOD_SOUND_FORMAT_PCMFLOAT, FMOD_SOUND_FORMAT_BITSTREAM
};
typedef unsigned int FMOD_TIMEUNIT;
#define FMOD_TIMEUNIT_MS 0x1
#define FMOD_TIMEUNIT_PCM 0x2
#define FMOD_OPENONLY 0x2000
#define FMOD_LOOP_OFF 0x1
#define FMOD_2D 0x8
struct FMOD_CREATESOUNDEXINFO {};
namespace FMOD {
class Sound {
public:
    FMOD_RESULT getFormat(FMOD_SOUND_TYPE*, FMOD_SOUND_FORMAT*, int*, int*) { return FMOD_OK; }
    FMOD_RESULT getDefaults(float*, int*) { return FMOD_OK; }
    FMOD_RESULT getLength(unsigned int*, FMOD_TIMEUNIT) { return FMOD_OK; }
    FMOD_RESULT readData(void*, unsigned int, unsigned int*) { return FMOD_OK; }
    FMOD_RESULT seekData(unsigned int) { return FMOD_OK; }
    FMOD_RESULT release() { return FMOD_OK; }
};
class System {
public:
    FMOD_RESULT createSound(const char*, FMOD_MODE, FMOD_CREATESOUNDEXINFO*, Sound**) { return FMOD_OK; }
};
}
