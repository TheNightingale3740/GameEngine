// Audio/AudioDevice.h
//
// Audio decoding and mixing, on miniaudio.
//
// The engine owns one device. A sound is decoded to float PCM once, when it is
// loaded, and playback is a state machine per source that reads from that buffer.
//
// Mixing is done by the engine rather than by a library mixer. A source's gain
// and stereo position are computed from the listener and the distance settings,
// and `RenderFrames` sums the active sources into one buffer. A platform audio
// callback asks the device for that buffer; a test asks for it directly and gets
// the same numbers without any audio hardware. That is what makes the audio path
// testable on a machine with no sound card.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/FileSystem.h"
#include "Core/Math/Math.h"
#include "Core/Result.h"

namespace Ember
{
    /// Audio formats the engine can hold.
    enum class AudioFormat
    {
        Unknown,
        Mono8,
        Mono16,
        Stereo8,
        Stereo16,
        MonoFloat,
        StereoFloat
    };

    /// Channels an audio format carries.
    [[nodiscard]] std::size_t GetChannelCount(AudioFormat format) noexcept;

    /// Bytes one sample of a format occupies.
    [[nodiscard]] std::size_t GetBytesPerSample(AudioFormat format) noexcept;

    /// Bytes one frame of a format occupies, across all of its channels.
    [[nodiscard]] std::size_t GetBytesPerFrame(AudioFormat format) noexcept;

    /// Decoded sound data, ready to play.
    ///
    /// Frames are always float samples after decoding, whatever the file's own
    /// format was, so that mixing never has to convert.
    class Sound
    {
    public:
        Sound() = default;

        /// True when the sound holds frames that can be played.
        [[nodiscard]] bool IsValid() const noexcept { return m_Valid; }

        /// Playback length in seconds; zero for a sound with no frames.
        [[nodiscard]] float GetDuration() const noexcept;

        [[nodiscard]] std::uint32_t GetSampleRate() const noexcept { return m_SampleRate; }
        [[nodiscard]] std::uint32_t GetChannelCount() const noexcept { return m_ChannelCount; }

        /// Frames the sound holds, where one frame carries one sample per channel.
        [[nodiscard]] std::uint64_t GetFrameCount() const noexcept { return m_FrameCount; }

        /// Decoded samples, interleaved. Owned by the sound.
        [[nodiscard]] const float* GetFrames() const noexcept { return m_Frames.data(); }

    private:
        friend class AudioDevice;

        std::vector<float> m_Frames;
        std::uint64_t m_FrameCount = 0;
        std::uint32_t m_SampleRate = 0;
        std::uint32_t m_ChannelCount = 0;
        bool m_Valid = false;
    };

    /// A playing or pausable sound instance.
    ///
    /// A source is a value rather than a handle: destroying it stops playback, and
    /// two sources playing the same sound keep independent positions.
    class AudioSource
    {
    public:
        AudioSource() = default;
        ~AudioSource();

        AudioSource(const AudioSource&) = delete;
        AudioSource& operator=(const AudioSource&) = delete;
        AudioSource(AudioSource&& other) noexcept;
        AudioSource& operator=(AudioSource&& other) noexcept;

        /// Starts or restarts playback from the beginning.
        void Play();

        /// Stops producing audio, keeping the current position.
        void Pause();

        /// Stops playback and rewinds to the beginning.
        void Stop();

        /// True when the source is currently producing audio.
        [[nodiscard]] bool IsPlaying() const noexcept { return m_Playing; }

        /// Playback volume, in [0, 1].
        void SetVolume(float volume);
        [[nodiscard]] float GetVolume() const noexcept { return m_Volume; }

        /// Playback pitch multiplier. Higher is faster and higher pitched.
        void SetPitch(float pitch);
        [[nodiscard]] float GetPitch() const noexcept { return m_Pitch; }

        /// Whether playback repeats until stopped.
        void SetLooping(bool looping);
        [[nodiscard]] bool IsLooping() const noexcept { return m_Looping; }

        /// World position, used for attenuation and stereo panning.
        void SetPosition(const Vec3& position) noexcept;
        [[nodiscard]] const Vec3& GetPosition() const noexcept { return m_Position; }

        /// Frame the source has played to, for tests and for progress displays.
        [[nodiscard]] std::uint64_t GetPlayhead() const noexcept { return m_Playhead; }

        [[nodiscard]] const Sound* GetSound() const noexcept { return m_Sound; }

    private:
        friend class AudioDevice;

        const Sound* m_Sound = nullptr;
        Vec3 m_Position = Vec3(0.0f);
        std::uint64_t m_Playhead = 0;
        float m_Volume = 1.0f;
        float m_Pitch = 1.0f;
        bool m_Looping = false;
        bool m_Playing = false;
        bool m_Paused = false;
    };

    /// A listener that sounds are positioned relative to.
    struct AudioListener
    {
        Vec3 Position = Vec3(0.0f);
        Vec3 Forward = Vec3(0.0f, 0.0f, 1.0f);
        Vec3 Up = Vec3(0.0f, 1.0f, 0.0f);

        /// Volume applied to every source, in [0, 1].
        float MasterVolume = 1.0f;
    };

    /// Decodes sounds and mixes them against a listener.
    class AudioDevice
    {
    public:
        /// Largest number of sources that can play at once.
        static constexpr std::size_t MaxSources = 64;

        AudioDevice();
        ~AudioDevice();

        AudioDevice(const AudioDevice&) = delete;
        AudioDevice& operator=(const AudioDevice&) = delete;
        AudioDevice(AudioDevice&&) = delete;
        AudioDevice& operator=(AudioDevice&&) = delete;

        /// Prepares the device for mixing at a given rate and channel count.
        ///
        /// Deliberately opens no hardware: the platform's audio callback, if there
        /// is one, is attached separately with `AttachOutput`. A machine with no
        /// sound card therefore still mixes, and still runs the same tests.
        Result<void> Initialise(std::uint32_t sampleRate = 48000, std::uint32_t channelCount = 2);

        /// Stops every source and forgets the mixer settings.
        void Shutdown();

        [[nodiscard]] bool IsInitialised() const noexcept { return m_Initialised; }

        [[nodiscard]] std::uint32_t GetSampleRate() const noexcept { return m_SampleRate; }
        [[nodiscard]] std::uint32_t GetChannelCount() const noexcept { return m_ChannelCount; }

        // ------------------------------------------------------------------- sounds

        /// Decodes an audio file into a sound.
        ///
        /// Fails with AssetError for a file the decoder does not recognise, which
        /// is the common case of someone pointing the engine at the wrong asset.
        Result<Sound> LoadSound(const FilePath& path);

        /// Builds a sound from raw interleaved float samples.
        ///
        /// `frameCount` counts frames rather than samples: a stereo frame carries
        /// two samples.
        [[nodiscard]] Result<Sound> CreateSound(std::uint32_t sampleRate,
                                                std::uint32_t channelCount,
                                                const float* frames,
                                                std::uint64_t frameCount);

        /// Builds a `duration`-second tone, for tests and for placeholders.
        ///
        /// `frequency` is in hertz and `amplitude` in [0, 1].
        [[nodiscard]] Result<Sound> CreateTone(std::uint32_t sampleRate,
                                              std::uint32_t channelCount,
                                              float frequency,
                                              float amplitude,
                                              float seconds);

        // ------------------------------------------------------------------ playback

        /// Plays a sound and returns a source controlling it.
        ///
        /// A source that cannot get a slot comes back not playing: running out of
        /// voices is a load problem, not a scripting error.
        AudioSource PlaySound(const Sound& sound);

        /// Sources currently producing audio.
        [[nodiscard]] std::size_t GetActiveSourceCount() const noexcept;

        /// Slots the device can play at once.
        [[nodiscard]] static constexpr std::size_t GetSourceCapacity() noexcept { return MaxSources; }

        // ------------------------------------------------------------------ listener

        [[nodiscard]] AudioListener& GetListener() noexcept { return m_Listener; }
        [[nodiscard]] const AudioListener& GetListener() const noexcept { return m_Listener; }

        /// Distance beyond which a sound is inaudible.
        void SetMaxDistance(float distance);
        [[nodiscard]] float GetMaxDistance() const noexcept { return m_MaxDistance; }

        /// Distance at which a sound plays at full volume.
        void SetReferenceDistance(float distance);
        [[nodiscard]] float GetReferenceDistance() const noexcept { return m_ReferenceDistance; }

        /// How sharply a sound falls off. 0 is linear, 1 is inverse square.
        void SetRolloff(float rolloff);
        [[nodiscard]] float GetRolloff() const noexcept { return m_Rolloff; }

        // ------------------------------------------------------------------ mixing

        /// Mixes `frameCount` frames into `outFrames`.
        ///
        /// The buffer holds interleaved samples and must be at least
        /// `frameCount * channelCount` long. Playback positions advance as a result,
        /// so consecutive calls continue where the last one stopped.
        ///
        /// Returns the number of frames written, which is always `frameCount`: a
        /// silent buffer is still a correct answer, and a shorter one would leave
        /// the caller guessing what the rest of the buffer holds.
        std::uint64_t RenderFrames(float* outFrames, std::uint64_t frameCount);

        /// Total number of frames the device has produced.
        [[nodiscard]] std::uint64_t GetRenderedFrameCount() const noexcept { return m_RenderedFrames; }

        /// Tells a source that its sound has run out, so it stops playing.
        ///
        /// Called by the mixer; public so that a source knows when it ended.
        void NotifySourceFinished(AudioSource& source) noexcept;

    private:
        friend class AudioSource;

        /// Registers a source so the mixer can find it while summing.
        void Register(AudioSource& source);
        void Unregister(AudioSource& source) noexcept;

        /// A source's gain and stereo pan against the current listener.
        ///
        /// Returns zero gain for a source beyond the maximum distance.
        [[nodiscard]] float ComputeGainAndPan(const AudioSource& source, float& outPan) const noexcept;

        std::vector<AudioSource*> m_Sources;
        AudioListener m_Listener;
        std::uint32_t m_SampleRate = 48000;
        std::uint32_t m_ChannelCount = 2;
        std::uint64_t m_RenderedFrames = 0;
        float m_MaxDistance = 100.0f;
        float m_ReferenceDistance = 1.0f;
        float m_Rolloff = 1.0f;
        bool m_Initialised = false;
    };

    /// The process's audio device, or nullptr before one is created.
    ///
    /// Sources hold no device pointer, so a source can only reach the mixer
    /// through this. There is exactly one device per process, which is what makes a
    /// single pointer sufficient.
    [[nodiscard]] AudioDevice* GetActiveAudioDevice() noexcept;
}