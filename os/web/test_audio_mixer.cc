// Exercise the real browser mixer with SDL's own conversion of the whole
// sound, fed frame by frame, as a reference. No audio device or game assets
// are needed.
#include <SDL.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

static int putCalls = 0;
static int countedPut(SDL_AudioStream* s, const void* data, int count)
{
    putCalls++;
    return SDL_AudioStreamPut(s, data, count);
}
// SDL headers are already included: test the web branch with the host SDL.
#define __EMSCRIPTEN__ 1
#define SDL_AudioStreamPut countedPut
#include "../../src/audio_engine.cc"
#undef SDL_AudioStreamPut
namespace fallout { bool gProgramIsActive = true; }
using namespace fallout;

static std::vector<Uint8> source(int frames, int bits, int channels)
{
    std::vector<Uint8> data(frames * (bits / 8) * channels);
    for (int i = 0; i < frames * channels; i++) {
        const double wave = std::sin(i * 0.031) * 0.4;
        if (bits == 16) reinterpret_cast<Sint16*>(data.data())[i] = wave * 32767;
        else reinterpret_cast<Sint8*>(data.data())[i] = wave * 127;
    }
    return data;
}
static void setup(int bits, int channels, int rate, const std::vector<Uint8>& data, bool loop)
{
    gAudioEngineSpec = {};
    gAudioEngineSpec.freq = 48000;
    gAudioEngineSpec.channels = 2;
    gAudioEngineSpec.format = AUDIO_F32SYS;
    gAudioEngineSoundBuffers = std::make_unique<AudioEngineSoundBuffer[]>(2);
    audioEngineSoundBufferCount = 2;
    gAudioEngineDeviceId = 1;
    assert(audioEngineCreateSoundBuffer(data.size(), bits, channels, rate) == 0);
    memcpy(gAudioEngineSoundBuffers[0].data, data.data(), data.size());
    assert(audioEngineSoundBufferPlay(0, loop ? AUDIO_ENGINE_SOUND_BUFFER_PLAY_LOOPING : 0));
}
static std::vector<float> reference(const std::vector<Uint8>& data, int bits, int channels, int rate)
{
    SDL_AudioStream* converter = SDL_NewAudioStream(bits == 16 ? AUDIO_S16 : AUDIO_S8, channels, rate,
        AUDIO_F32SYS, 2, 48000);
    assert(converter);
    // Frame by frame: SDL2's resampler (2.30; 2.32, the web build's) drops
    // part of a big Put, while small ones convert exactly, whatever their
    // size up to the mixer's 256 frames.
    const size_t frameSize = bits / 8 * channels;
    for (size_t pos = 0; pos < data.size(); pos += frameSize)
        assert(SDL_AudioStreamPut(converter, data.data() + pos, frameSize) == 0);
    assert(SDL_AudioStreamFlush(converter) == 0);
    const int available = SDL_AudioStreamAvailable(converter);
    assert(available >= 0);
    std::vector<float> result(available / sizeof(float));
    assert(SDL_AudioStreamGet(converter, result.data(), available) == available);
    SDL_FreeAudioStream(converter);
    return result;
}
static void compare(const std::vector<float>& actual, const std::vector<float>& expected)
{
    assert(actual.size() <= expected.size());
    for (size_t i = 0; i < actual.size(); i++) {
        if (std::abs(actual[i] - expected[i]) > 0.00002f) {
            std::fprintf(stderr, "PCM mismatch at %zu: %.8f vs %.8f\n", i, actual[i], expected[i]);
            std::abort();
        }
    }
}
int main()
{
    for (int rate : {11025, 22050, 44100, 48000}) {
        for (bool mono8 : {false, true}) {
            const int bits = mono8 ? 8 : 16, channels = mono8 ? 1 : 2;
            for (bool loop : {false, true}) {
                auto data = source(loop ? 1021 : 3333, bits, channels);
                setup(bits, channels, rate, data, loop);
                auto full = data;
                if (loop) for (int i = 1; i < 128; i++) full.insert(full.end(), data.begin(), data.end());
                auto expected = reference(full, bits, channels, rate);
                const size_t samples = loop ? 2048 * 2 * 8 : ((expected.size() + 4095) / 4096 + 1) * 4096;
                expected.resize(std::max(samples, expected.size()), 0);
                std::vector<float> actual(samples, 1);
                putCalls = 0;
                for (size_t i = 0; i < samples; i += 4096)
                    audioEngineMixin(nullptr, reinterpret_cast<Uint8*>(actual.data() + i), 4096 * sizeof(float));
                compare(actual, expected);
                // A callback needs blocks, not thousands of one-frame Put calls.
                assert(putCalls < static_cast<int>(samples / 4096) * 24);
                assert(gAudioEngineSoundBuffers[0].playing == loop);
                if (loop) {
                    const auto position = gAudioEngineSoundBuffers[0].pos;
                    gProgramIsActive = false;
                    std::vector<float> paused(4096, 1);
                    audioEngineMixin(nullptr, reinterpret_cast<Uint8*>(paused.data()), paused.size() * sizeof(float));
                    assert(std::all_of(paused.begin(), paused.end(), [](float x) { return x == 0; }));
                    assert(gAudioEngineSoundBuffers[0].pos == position);
                    gProgramIsActive = true;
                }
                // Seeking must discard PCM queued for the previous position.
                assert(audioEngineSoundBufferSetCurrentPosition(0, 0));
                assert(audioEngineSoundBufferPlay(0, loop ? AUDIO_ENGINE_SOUND_BUFFER_PLAY_LOOPING : 0));
                std::vector<float> replay(4096);
                audioEngineMixin(nullptr, reinterpret_cast<Uint8*>(replay.data()), replay.size() * sizeof(float));
                compare(replay, expected);
            }
        }
    }
    // Very short buffers stress loop wraparound and resampler staging.
    auto tiny = source(3, 16, 2);
    setup(16, 2, 22050, tiny, true);
    std::vector<float> out(4096);
    audioEngineMixin(nullptr, reinterpret_cast<Uint8*>(out.data()), out.size() * sizeof(float));
    assert(gAudioEngineSoundBuffers[0].pos < tiny.size());
    // Multiple voices are mixed with their individual volumes.
    auto data = source(8192, 16, 2);
    setup(16, 2, 48000, data, false);
    assert(audioEngineCreateSoundBuffer(data.size(), 16, 2, 48000) == 1);
    memcpy(gAudioEngineSoundBuffers[1].data, data.data(), data.size());
    audioEngineSoundBufferSetVolume(0, SDL_MIX_MAXVOLUME / 4);
    audioEngineSoundBufferSetVolume(1, SDL_MIX_MAXVOLUME / 4);
    audioEngineSoundBufferPlay(1, 0);
    audioEngineMixin(nullptr, reinterpret_cast<Uint8*>(out.data()), out.size() * sizeof(float));
    auto expected = reference(data, 16, 2, 48000);
    for (auto& sample : expected) sample *= 0.5f;
    compare(out, expected);
    gAudioEngineDeviceId = -1;
    std::puts("Web mixer: PCM, resampling, loops, one-shot tails, seek, pause, volume and block feeding passed");
}
