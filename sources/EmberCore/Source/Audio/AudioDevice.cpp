// Audio/AudioDevice.cpp

#include "Audio/AudioDevice.h"

#include <algorithm>
#include <cmath>

#include "Core/FileSystem.h"
#include "Core/Logging/Log.h"

// miniaudio is a single-header library whose implementation defines a large amount
// of global state. Exactly one engine translation unit includes it; nothing else in
// the engine may, or the program would have duplicate definitions.
//
// Only decoding is used. Playback and mixing are the engine's own, so that the
// audio path produces the same numbers on a machine with a sound card and on one
// without, and can therefore be tested without either.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"

namespace Ember
{
    namespace
    {
        /// The process's audio device. See `GetActiveAudioDevice`.
        AudioDevice* s_ActiveAudioDevice = nullptr;
    }

    AudioDevice* GetActiveAudioDevice() noexcept
    {
        return s_ActiveAudioDevice;
    }

    float Sound::GetDuration() const noexcept
    {
        return m_SampleRate > 0 ? static_cast<float>(m_FrameCount) / static_cast<float>(m_SampleRate) : 0.0f;
    }

    AudioDevice::AudioDevice() = default;

    AudioDevice::~AudioDevice()
    {
        Shutdown();
    }

    Result<void> AudioDevice::Initialise(std::uint32_t sampleRate, std::uint32_t channelCount)
    {
        if (channelCount == 0 || channelCount > 2)
        {
            return Error(ErrorCode::InvalidArgument,
                         "The engine mixes to one or two channels, not " + std::to_string(channelCount));
        }

        if (sampleRate == 0)
        {
            return {ErrorCode::InvalidArgument, "The sample rate must be greater than zero"};
        }

        if (s_ActiveAudioDevice != nullptr && s_ActiveAudioDevice != this)
        {
            return {ErrorCode::DeviceError, "An audio device already exists for this process"};
        }

        m_SampleRate = sampleRate;
        m_ChannelCount = channelCount;
        m_Sources.assign(MaxSources, nullptr);
        m_RenderedFrames = 0;
        m_Initialised = true;

        s_ActiveAudioDevice = this;

        EMBER_LOG_INFO("Audio initialised: {} Hz, {} channel(s), {} source slots",
                       m_SampleRate, m_ChannelCount, MaxSources);
        return {};
    }

    void AudioDevice::Shutdown()
    {
        for (AudioSource*& source : m_Sources)
        {
            if (source != nullptr)
            {
                source->Stop();
            }
        }

        m_Sources.clear();

        if (s_ActiveAudioDevice == this)
        {
            s_ActiveAudioDevice = nullptr;
        }

        m_Initialised = false;
    }

    Result<Sound> AudioDevice::CreateSound(std::uint32_t sampleRate,
                                           std::uint32_t channelCount,
                                           const float* frames,
                                           std::uint64_t frameCount)
    {
        if (sampleRate == 0 || channelCount == 0 || channelCount > 2)
        {
            return {ErrorCode::InvalidArgument, "A sound needs a sample rate and one or two channels"};
        }

        if (frameCount > 0 && frames == nullptr)
        {
            return {ErrorCode::InvalidArgument, "A sound with frames needs a frame buffer"};
        }

        Sound sound;
        sound.m_SampleRate = sampleRate;
        sound.m_ChannelCount = channelCount;
        sound.m_FrameCount = frameCount;

        if (frameCount > 0)
        {
            sound.m_Frames.assign(frames, frames + static_cast<std::size_t>(frameCount) * channelCount);
        }

        sound.m_Valid = true;
        return sound;
    }

    Result<Sound> AudioDevice::CreateTone(std::uint32_t sampleRate,
                                          std::uint32_t channelCount,
                                          float frequency,
                                          float amplitude,
                                          float seconds)
    {
        if (seconds <= 0.0f || frequency <= 0.0f)
        {
            return {ErrorCode::InvalidArgument, "A tone needs a positive frequency and length"};
        }

        const auto frameCount = static_cast<std::uint64_t>(seconds * static_cast<float>(sampleRate));
        std::vector<float> frames(static_cast<std::size_t>(frameCount) * channelCount);

        const float step = 2.0f * Pi * frequency / static_cast<float>(sampleRate);

        for (std::uint64_t frame = 0; frame < frameCount; ++frame)
        {
            const float sample =
                std::sin(step * static_cast<float>(frame)) * amplitude;

            for (std::uint32_t channel = 0; channel < channelCount; ++channel)
            {
                frames[static_cast<std::size_t>(frame) * channelCount + channel] = sample;
            }
        }

        return CreateSound(sampleRate, channelCount, frames.data(), frameCount);
    }

    Result<Sound> AudioDevice::LoadSound(const FilePath& path)
    {
        Result<std::vector<std::uint8_t>> bytes = FileSystem::ReadBinaryFile(path);
        if (bytes.IsFailure())
        {
            return Error(bytes.GetError().Code,
                         "Could not read sound '" + FileSystem::ToString(path) + "': " + bytes.GetError().Message);
        }

        // Decoded straight to float, at whatever rate and channel count the file
        // uses: the decoder resamples on the way out, and everything downstream
        // mixes from float, so anything narrower would lose precision twice.
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
        ma_decoder decoder;

        if (ma_decoder_init_memory(bytes.Value().data(), bytes.Value().size(), &config, &decoder) != MA_SUCCESS)
        {
            return Error(ErrorCode::AssetError,
                         "'" + FileSystem::ToString(path) + "' is not an audio file the engine can decode");
        }

        const std::uint32_t sampleRate = decoder.outputSampleRate;
        const std::uint32_t channelCount = decoder.outputChannels;

        if (sampleRate == 0 || channelCount == 0 || channelCount > 2)
        {
            ma_decoder_uninit(&decoder);
            return Error(ErrorCode::AssetError,
                         "'" + FileSystem::ToString(path) + "' is not in a format the engine plays");
        }

        // The file's length in frames is not available up front, so the sound is
        // read in blocks and the total grows. The block is large enough that a
        // typical sound needs one read, and small enough that a long track does
        // not need a second.
        constexpr std::uint64_t FramesPerBlock = 1 << 16;

        std::vector<float> frames;
        std::vector<float> block(static_cast<std::size_t>(FramesPerBlock) * channelCount);

        while (true)
        {
            std::uint64_t read = 0;
            const ma_result result =
                ma_decoder_read_pcm_frames(&decoder, block.data(), FramesPerBlock, &read);

            if (result != MA_SUCCESS && result != MA_AT_END)
            {
                ma_decoder_uninit(&decoder);
                return Error(ErrorCode::AssetError,
                             "'" + FileSystem::ToString(path) + "' could not be decoded");
            }

            if (read == 0)
            {
                break;
            }

            frames.insert(frames.end(),
                          block.begin(),
                          block.begin() + static_cast<std::ptrdiff_t>(read * channelCount));

            if (read < FramesPerBlock)
            {
                break;
            }
        }

        ma_decoder_uninit(&decoder);

        const auto frameCount = static_cast<std::uint64_t>(frames.size() / channelCount);
        return CreateSound(sampleRate, channelCount, frames.data(), frameCount);
    }

    void AudioDevice::Register(AudioSource& source)
    {
        // Registering twice would leave the source in two slots, so that stopping
        // it cleared only one and it never stopped being heard.
        for (const AudioSource* slot : m_Sources)
        {
            if (slot == &source)
            {
                return;
            }
        }

        for (AudioSource*& slot : m_Sources)
        {
            if (slot == nullptr)
            {
                slot = &source;
                return;
            }
        }

        // Every slot is busy. The source stays unregistered and therefore silent,
        // which is the same outcome as playing nothing rather than an error.
        EMBER_LOG_WARN("No free audio source slot; a sound will not be heard");
    }

    void AudioDevice::Unregister(AudioSource& source) noexcept
    {
        for (AudioSource*& slot : m_Sources)
        {
            if (slot == &source)
            {
                slot = nullptr;
                return;
            }
        }
    }

    void AudioDevice::NotifySourceFinished(AudioSource& source) noexcept
    {
        source.m_Playing = false;
        source.m_Paused = false;

        // The slot is released here rather than on the next frame: a game that
        // fires sounds faster than it mixes would otherwise run the pool dry.
        Unregister(source);
    }

    AudioSource AudioDevice::PlaySound(const Sound& sound)
    {
        AudioSource source;
        source.m_Sound = &sound;
        source.Play();
        return source;
    }

    std::size_t AudioDevice::GetActiveSourceCount() const noexcept
    {
        return static_cast<std::size_t>(
            std::count_if(m_Sources.begin(), m_Sources.end(),
                          [](const AudioSource* source) { return source != nullptr && source->IsPlaying(); }));
    }

    void AudioDevice::SetMaxDistance(float distance)
    {
        m_MaxDistance = std::max(distance, 0.01f);
    }

    void AudioDevice::SetReferenceDistance(float distance)
    {
        m_ReferenceDistance = std::max(distance, 0.01f);
    }

    void AudioDevice::SetRolloff(float rolloff)
    {
        m_Rolloff = Math::Clamp(rolloff, 0.0f, 1.0f);
    }

    float AudioDevice::ComputeGainAndPan(const AudioSource& source, float& outPan) const noexcept
    {
        outPan = 0.0f;

        float attenuation = 1.0f;
        const float distance = glm::distance(source.GetPosition(), m_Listener.Position);

        if (distance > m_ReferenceDistance)
        {
            const float range = m_MaxDistance - m_ReferenceDistance;
            const float t = range > 0.0f ? Math::Clamp((distance - m_ReferenceDistance) / range, 0.0f, 1.0f) : 1.0f;

            // Linear rolloff falls off as 1 - t; inverse square falls off faster as
            // the power rises. Interpolating between them keeps both endpoints exact.
            attenuation = 1.0f - std::pow(t, 1.0f + m_Rolloff);
        }

        if (attenuation <= 0.0f)
        {
            return 0.0f;
        }

        // Pan from the listener's right vector, so a sound to the listener's right
        // plays in the right speaker.
        const Vec3 forward = glm::length(m_Listener.Forward) > 0.0f ? glm::normalize(m_Listener.Forward)
                                                                   : Vec3(0.0f, 0.0f, 1.0f);
        // The listener's right is up cross forward. Getting this backwards is
        // silent: every sound pans, just to the wrong side.
        const Vec3 right = glm::length(m_Listener.Up) > 0.0f
                               ? glm::normalize(glm::cross(glm::normalize(m_Listener.Up), forward))
                               : Vec3(1.0f, 0.0f, 0.0f);

        const Vec3 offset = source.GetPosition() - m_Listener.Position;
        if (glm::length(offset) > 0.0f)
        {
            outPan = Math::Clamp(glm::dot(glm::normalize(offset), right), -1.0f, 1.0f);
        }

        return Math::Clamp(source.GetVolume() * m_Listener.MasterVolume, 0.0f, 1.0f) * attenuation;
    }

    std::uint64_t AudioDevice::RenderFrames(float* outFrames, std::uint64_t frameCount)
    {
        if (outFrames == nullptr || frameCount == 0)
        {
            return 0;
        }

        // Nothing is written before initialisation, and the buffer is left exactly
        // as the caller passed it: zeroing it and returning zero frames would claim
        // work it did not do.
        if (!m_Initialised)
        {
            return 0;
        }

        const std::size_t samples = static_cast<std::size_t>(frameCount) * m_ChannelCount;
        std::fill(outFrames, outFrames + samples, 0.0f);

        // Sources may unregister themselves while being mixed: a sound that runs
        // out is told so immediately rather than on the next frame, and a source
        // destroyed from a callback cannot free the slot mid-sum.
        std::vector<AudioSource*> finished;

        for (AudioSource* source : m_Sources)
        {
            if (source == nullptr || !source->IsPlaying())
            {
                continue;
            }

            const Sound& sound = *source->m_Sound;
            if (sound.GetSampleRate() == 0 || sound.GetFrameCount() == 0)
            {
                finished.push_back(source);
                continue;
            }

            float pan = 0.0f;
            const float gain = ComputeGainAndPan(*source, pan);

            // A mono source is played at the same level in both speakers; a stereo
            // source is panned by attenuating each side.
            const float leftGain = gain * (1.0f - std::max(pan, 0.0f) * 0.5f);
            const float rightGain = gain * (1.0f + std::min(pan, 0.0f) * 0.5f);

            // Pitch is applied by stepping the playhead by the pitch ratio.
            const float step = source->GetPitch();
            double playhead = static_cast<double>(source->m_Playhead);

            for (std::uint64_t frame = 0; frame < frameCount; ++frame)
            {
                const auto position = static_cast<std::uint64_t>(playhead);
                if (position >= sound.GetFrameCount())
                {
                    if (!source->IsLooping())
                    {
                        finished.push_back(source);
                        break;
                    }

                    playhead = std::fmod(playhead, static_cast<double>(sound.GetFrameCount()));
                    continue;
                }

                const float* frameData = sound.GetFrames() + static_cast<std::size_t>(position) *
                                                                      sound.GetChannelCount();

                if (sound.GetChannelCount() == 1)
                {
                    const float sample = frameData[0];
                    outFrames[frame * m_ChannelCount] += sample * leftGain;

                    if (m_ChannelCount == 2)
                    {
                        outFrames[frame * m_ChannelCount + 1] += sample * rightGain;
                    }
                }
                else
                {
                    outFrames[frame * m_ChannelCount] += frameData[0] * leftGain;

                    if (m_ChannelCount == 2)
                    {
                        outFrames[frame * m_ChannelCount + 1] += frameData[1] * rightGain;
                    }
                }

                playhead += static_cast<double>(step);
            }

            // Only what was actually consumed is carried, so a source that ran out
            // mid-buffer does not skip ahead on the next call.
            const auto consumed = static_cast<std::uint64_t>(playhead);
            source->m_Playhead = source->IsLooping() ? consumed % sound.GetFrameCount() : consumed;

            // A source whose sound ended exactly on the buffer's last frame has not
            // yet noticed, because the loop only checks for the end as it goes.
            if (!source->IsLooping() && source->m_Playhead >= sound.GetFrameCount())
            {
                finished.push_back(source);
            }
        }

        for (AudioSource* source : finished)
        {
            NotifySourceFinished(*source);
        }

        m_RenderedFrames += frameCount;
        return frameCount;
    }

    // ------------------------------------------------------------------ AudioSource

    AudioSource::~AudioSource()
    {
        Stop();
    }

    AudioSource::AudioSource(AudioSource&& other) noexcept
        : m_Sound(other.m_Sound)
        , m_Position(other.m_Position)
        , m_Playhead(other.m_Playhead)
        , m_Volume(other.m_Volume)
        , m_Pitch(other.m_Pitch)
        , m_Looping(other.m_Looping)
        , m_Playing(other.m_Playing)
        , m_Paused(other.m_Paused)
    {
        if (AudioDevice* device = GetActiveAudioDevice(); device != nullptr)
        {
            device->Register(*this);
        }

        other.m_Sound = nullptr;
        other.m_Playing = false;
        other.m_Paused = false;
    }

    AudioSource& AudioSource::operator=(AudioSource&& other) noexcept
    {
        if (this != &other)
        {
            Stop();

            m_Sound = other.m_Sound;
            m_Position = other.m_Position;
            m_Playhead = other.m_Playhead;
            m_Volume = other.m_Volume;
            m_Pitch = other.m_Pitch;
            m_Looping = other.m_Looping;
            m_Playing = other.m_Playing;
            m_Paused = other.m_Paused;

            if (AudioDevice* device = GetActiveAudioDevice(); device != nullptr)
            {
                device->Register(*this);
            }

            other.m_Sound = nullptr;
            other.m_Playing = false;
            other.m_Paused = false;
        }

        return *this;
    }

    void AudioSource::Play()
    {
        if (m_Sound == nullptr || !m_Sound->IsValid())
        {
            return;
        }

        if (AudioDevice* device = GetActiveAudioDevice(); device != nullptr)
        {
            device->Register(*this);
        }

        m_Playhead = 0;
        m_Playing = true;
        m_Paused = false;
    }

    void AudioSource::Pause()
    {
        // A paused source keeps its position, so resuming continues where it left
        // off rather than starting again.
        m_Playing = false;
        m_Paused = true;
    }

    void AudioSource::Stop()
    {
        m_Playing = false;
        m_Paused = false;
        m_Playhead = 0;

        if (AudioDevice* device = GetActiveAudioDevice(); device != nullptr)
        {
            device->Unregister(*this);
        }
    }

    void AudioSource::SetVolume(float volume)
    {
        m_Volume = Math::Clamp(volume, 0.0f, 1.0f);
    }

    void AudioSource::SetPitch(float pitch)
    {
        m_Pitch = std::max(pitch, 0.01f);
    }

    void AudioSource::SetLooping(bool looping)
    {
        m_Looping = looping;
    }

    void AudioSource::SetPosition(const Vec3& position) noexcept
    {
        m_Position = position;
    }
}