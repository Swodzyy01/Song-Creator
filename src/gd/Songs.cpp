#include "Songs.hpp"

#include "Common.hpp"
#include <Geode/Geode.hpp>
#if __has_include(<Geode/fmod/fmod.hpp>)
#include <Geode/fmod/fmod.hpp>
#elif __has_include(<fmod.hpp>)
#include <fmod.hpp>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

using namespace geode::prelude;

namespace aisgd {

namespace {

FMOD::System* fmodSystem() {
    auto* engine = FMODAudioEngine::sharedEngine();
    return engine ? engine->m_system : nullptr;
}

std::string resolve(std::string const& file) {
    auto* fu = CCFileUtils::sharedFileUtils();
    return std::string(fu->fullPathForFilename(file.c_str(), false));
}

bool allDigits(std::string const& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

}  // namespace

bool fileExists(std::string const& path) {
    if (path.empty()) return false;
    return CCFileUtils::sharedFileUtils()->isFileExist(path.c_str());
}

std::string customSongPath(int id) {
    return std::string(MusicDownloadManager::sharedState()->pathForSong(id));
}

std::string customSongTitle(int id) {
    if (auto* info = MusicDownloadManager::sharedState()->getSongInfoObject(id)) {
        std::string name = std::string(info->m_songName);
        if (!name.empty()) return name;
    }
    return "Song " + std::to_string(id);
}

std::vector<SongInfo> collectSongs() {
    std::vector<SongInfo> out;
    // --- the game's own tracks --------------------------------------------------------------------
    for (int i = 0; i < 80; ++i) {
        std::string file = std::string(LevelTools::getAudioFileName(i));
        if (file.empty()) continue;
        std::string full = resolve(file);
        if (!fileExists(full)) continue;
        bool dup = false;
        for (auto const& s : out)
            if (s.path == full) dup = true;
        if (dup) continue;
        SongInfo s;
        s.official = true;
        s.id = i;
        s.title = std::string(LevelTools::getAudioTitle(i));
        s.path = full;
        out.push_back(s);
    }
    // --- songs that were downloaded from Newgrounds / the Music Library ------------------------------
    std::vector<int> ids;
    std::string base = std::string(CCFileUtils::sharedFileUtils()->getWritablePath());
    std::error_code ec;
    for (std::filesystem::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        std::string name = it->path().filename().string();
        size_t dot = name.rfind('.');
        if (dot == std::string::npos) continue;
        std::string stem = name.substr(0, dot), ext = name.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (!allDigits(stem) || stem.size() > 9) continue;
        if (ext != "mp3" && ext != "ogg") continue;
        ids.push_back(std::atoi(stem.c_str()));
    }
    std::sort(ids.begin(), ids.end(), [](int a, int b) { return a > b; });
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    for (int id : ids) {
        SongInfo s;
        s.official = false;
        s.id = id;
        s.title = customSongTitle(id);
        s.path = customSongPath(id);
        if (!fileExists(s.path)) continue;
        out.push_back(s);
    }
    return out;
}

double probeDuration(std::string const& path) {
    FMOD::System* sys = fmodSystem();
    if (!sys || path.empty()) return 0.0;
    FMOD::Sound* snd = nullptr;
    if (sys->createSound(path.c_str(), FMOD_OPENONLY | FMOD_LOOP_OFF | FMOD_2D, nullptr, &snd) != FMOD_OK || !snd) return 0.0;
    unsigned int ms = 0;
    snd->getLength(&ms, FMOD_TIMEUNIT_MS);
    snd->release();
    return double(ms) / 1000.0;
}

// ---------------------------------------------------------------------------------------------
// decoder
// ---------------------------------------------------------------------------------------------

SongDecoder::~SongDecoder() { release(); }

void SongDecoder::release() {
    if (m_sound) {
        static_cast<FMOD::Sound*>(m_sound)->release();
        m_sound = nullptr;
    }
}

bool SongDecoder::begin(std::string const& path, double maxSeconds) {
    FMOD::System* sys = fmodSystem();
    if (!sys) {
        error = "audio engine not available";
        return false;
    }
    FMOD::Sound* snd = nullptr;
    FMOD_RESULT r = sys->createSound(path.c_str(), FMOD_OPENONLY | FMOD_LOOP_OFF | FMOD_2D, nullptr, &snd);
    if (r != FMOD_OK || !snd) {
        error = "could not open the song file";
        return false;
    }
    m_sound = snd;
    FMOD_SOUND_TYPE type;
    FMOD_SOUND_FORMAT fmt;
    int bits = 0;
    if (snd->getFormat(&type, &fmt, &m_channels, &bits) != FMOD_OK || m_channels < 1) {
        error = "unknown audio format";
        release();
        return false;
    }
    switch (fmt) {
        case FMOD_SOUND_FORMAT_PCM8: m_bytes = 1; break;
        case FMOD_SOUND_FORMAT_PCM16: m_bytes = 2; break;
        case FMOD_SOUND_FORMAT_PCM24: m_bytes = 3; break;
        case FMOD_SOUND_FORMAT_PCM32: m_bytes = 4; break;
        case FMOD_SOUND_FORMAT_PCMFLOAT: m_bytes = 4; break;
        default:
            error = "unsupported sample format";
            release();
            return false;
    }
    m_format = int(fmt);
    float freq = 44100.f;
    int prio = 0;
    snd->getDefaults(&freq, &prio);
    m_rate = int(freq + 0.5f);
    if (m_rate < 8000) m_rate = 44100;
    m_factor = m_rate >= 32000 ? std::max(1, int(std::lround(m_rate / 22050.0))) : 1;
    m_limit = size_t(std::max(5.0, maxSeconds) * m_rate);
    snd->seekData(0);  // some FMOD versions leave the read cursor at the end after opening
    m_pcm.sampleRate = m_rate / m_factor;
    m_pcm.mono.clear();
    m_pcm.mono.reserve(m_limit / size_t(m_factor) + 16);
    m_buf.resize(size_t(8192) * size_t(m_channels) * size_t(m_bytes));
    m_done = 0;
    m_acc = 0;
    m_accN = 0;
    m_ok = false;
    m_finished = false;
    return true;
}

float SongDecoder::progress() const {
    if (m_finished) return 1.f;
    return m_limit ? aclamp(float(double(m_done) / double(m_limit)), 0.f, 1.f) : 0.f;
}

bool SongDecoder::step(double budgetMs) {
    if (m_finished) return true;
    if (!m_sound) {
        m_finished = true;
        return true;
    }
    auto* snd = static_cast<FMOD::Sound*>(m_sound);
    auto start = std::chrono::steady_clock::now();
    unsigned int frameBytes = unsigned(m_channels * m_bytes);
    unsigned int want = unsigned(m_buf.size());
    while (true) {
        unsigned int got = 0;
        FMOD_RESULT r = snd->readData(m_buf.data(), want, &got);
        unsigned int frames = got / frameBytes;
        unsigned char const* p = m_buf.data();
        for (unsigned int f = 0; f < frames && m_done < m_limit; ++f, ++m_done) {
            double mono = 0;
            for (int c = 0; c < m_channels; ++c) {
                double v = 0;
                switch (m_format) {
                    case FMOD_SOUND_FORMAT_PCM8: v = (double(p[0]) - 128.0) / 128.0; break;
                    case FMOD_SOUND_FORMAT_PCM16: v = double(int16_t(uint16_t(p[0]) | (uint16_t(p[1]) << 8))) / 32768.0; break;
                    case FMOD_SOUND_FORMAT_PCM24: {
                        int32_t x = (int32_t(p[0]) << 8) | (int32_t(p[1]) << 16) | (int32_t(p[2]) << 24);
                        v = double(x) / 2147483648.0;
                        break;
                    }
                    case FMOD_SOUND_FORMAT_PCM32: {
                        int32_t x = int32_t(uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24));
                        v = double(x) / 2147483648.0;
                        break;
                    }
                    default: {
                        float x;
                        std::memcpy(&x, p, sizeof x);
                        v = double(x);
                        break;
                    }
                }
                p += m_bytes;
                mono += v;
            }
            m_acc += mono / m_channels;
            if (++m_accN >= m_factor) {
                m_pcm.mono.push_back(float(m_acc / m_accN));
                m_acc = 0;
                m_accN = 0;
            }
        }
        bool end = (r != FMOD_OK && r != FMOD_ERR_FILE_EOF) || got < want || frames == 0 || m_done >= m_limit;
        if (end) {
            m_finished = true;
            m_ok = m_pcm.mono.size() > size_t(m_pcm.sampleRate) * 4;
            if (!m_ok) error = "the song could not be decoded";
            release();
            return true;
        }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (ms >= budgetMs) return false;
    }
}

}  // namespace aisgd
