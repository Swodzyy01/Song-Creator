#pragma once
#include <string>
#include <vector>

#include "../core/Audio.hpp"

namespace aisgd {

struct SongInfo {
    bool official = false;
    int id = 0;  // official track number, or Newgrounds / Music Library song id
    std::string title;
    std::string path;
    double duration = 0;  // seconds, 0 = unknown
};

// Official tracks that exist on disk + custom songs that have been downloaded already.
std::vector<SongInfo> collectSongs();

// Path a custom song would have on disk (may not exist).
std::string customSongPath(int id);
bool fileExists(std::string const& path);
std::string customSongTitle(int id);

// Length in seconds, 0 if it cannot be read.
double probeDuration(std::string const& path);

// Decodes a song with the game's own FMOD in small slices (called every frame from the black screen),
// producing mono PCM at ~22 kHz for the analyser.
class SongDecoder {
public:
    SongDecoder() = default;
    SongDecoder(SongDecoder const&) = delete;
    SongDecoder& operator=(SongDecoder const&) = delete;
    ~SongDecoder();

    bool begin(std::string const& path, double maxSeconds);
    bool step(double budgetMs);  // returns true once finished (successfully or not)
    bool ok() const { return m_ok; }
    float progress() const;
    ais::PCM take() { return std::move(m_pcm); }
    std::string error;

private:
    void release();

    void* m_sound = nullptr;  // FMOD::Sound*
    int m_format = 0;         // FMOD_SOUND_FORMAT
    int m_channels = 2, m_bytes = 2, m_rate = 44100, m_factor = 1;
    size_t m_limit = 0, m_done = 0;
    double m_acc = 0;
    int m_accN = 0;
    bool m_ok = false, m_finished = false;
    ais::PCM m_pcm;
    std::vector<unsigned char> m_buf;
};

}  // namespace aisgd
