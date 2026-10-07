#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <vector>

#include "Audio/AudioDevice.h"
#include "Core/FileSystem.h"

using namespace Ember;

namespace
{
    constexpr std::uint32_t SampleRate = 8000;
    constexpr std::uint32_t Mono = 1;

    /// Renders `frames` frames into a throwaway buffer.
    ///
    /// Many tests care only that mixing ran, not what came out, so the buffer is
    /// deliberately not returned and the result is not marked nodiscard.
    /// An initialised audio device, shut down when the test ends.
    ///
    /// The device is a process singleton, so these tests cannot run in parallel
    /// with each other; GoogleTest runs them in sequence within one binary.
    class AudioFixture
    {
    public:
        AudioFixture()
        {
            m_Result = m_Device.Initialise(SampleRate, 2);
        }

        ~AudioFixture()
        {
            m_Device.Shutdown();
        }

        AudioFixture(const AudioFixture&) = delete;
        AudioFixture& operator=(const AudioFixture&) = delete;

        [[nodiscard]] const Result<void>& GetResult() const { return m_Result; }
        [[nodiscard]] AudioDevice& Get() { return m_Device; }

        /// Renders `frames` frames and returns the buffer.
        [[nodiscard]] std::vector<float> Render(std::uint64_t frames)
        {
            std::vector<float> buffer(static_cast<std::size_t>(frames) * m_Device.GetChannelCount());
            m_Device.RenderFrames(buffer.data(), frames);
            return buffer;
        }

        void RenderAndDiscard(std::uint64_t frames)
        {
            std::vector<float> buffer(static_cast<std::size_t>(frames) * m_Device.GetChannelCount());
            m_Device.RenderFrames(buffer.data(), frames);
        }

    private:
        AudioDevice m_Device;
        Result<void> m_Result;
    };

    /// Builds a constant-valued sound, which makes mixing arithmetic checkable.
    [[nodiscard]] Result<Sound> MakeConstant(AudioDevice& device,
                                             float value,
                                             std::uint64_t frameCount,
                                             std::uint32_t channels = Mono)
    {
        std::vector<float> frames(static_cast<std::size_t>(frameCount) * channels, value);
        return device.CreateSound(SampleRate, channels, frames.data(), frameCount);
    }
}

// ------------------------------------------------------------------- lifecycle

TEST(AudioDeviceTest, InitialiseSucceedsWithoutHardware)
{
    AudioFixture fixture;

    EXPECT_TRUE(fixture.GetResult().IsSuccess());
    EXPECT_TRUE(fixture.Get().IsInitialised());
    EXPECT_EQ(fixture.Get().GetSampleRate(), SampleRate);
    EXPECT_EQ(fixture.Get().GetChannelCount(), 2u);
}

TEST(AudioDeviceTest, InitialiseRejectsAnImpossibleChannelCount)
{
    AudioDevice device;

    EXPECT_TRUE(device.Initialise(SampleRate, 0).IsFailure());
    EXPECT_TRUE(device.Initialise(SampleRate, 3).IsFailure());
    EXPECT_TRUE(device.Initialise(0, 2).IsFailure());
}

TEST(AudioDeviceTest, ShutdownIsIdempotent)
{
    AudioDevice device;
    ASSERT_TRUE(device.Initialise(SampleRate, 2).IsSuccess());

    device.Shutdown();
    device.Shutdown();

    EXPECT_FALSE(device.IsInitialised());
    EXPECT_EQ(GetActiveAudioDevice(), nullptr);
}

TEST(AudioDeviceTest, OnlyOneDeviceAtATime)
{
    AudioFixture fixture;

    AudioDevice second;
    EXPECT_TRUE(second.Initialise(SampleRate, 2).IsFailure());
}

TEST(AudioDeviceTest, ActiveDeviceIsExposed)
{
    AudioFixture fixture;

    EXPECT_EQ(GetActiveAudioDevice(), &fixture.Get());
}

// ----------------------------------------------------------------------- sounds

TEST(SoundTest, CreateSoundStoresFrames)
{
    AudioFixture fixture;

    const Result<Sound> sound = fixture.Get().CreateSound(SampleRate, Mono, nullptr, 0);

    ASSERT_TRUE(sound.IsSuccess());
    EXPECT_TRUE(sound.Value().IsValid());
    EXPECT_EQ(sound.Value().GetSampleRate(), SampleRate);
    EXPECT_EQ(sound.Value().GetChannelCount(), Mono);
    EXPECT_FLOAT_EQ(sound.Value().GetDuration(), 0.0f);
}

TEST(SoundTest, CreateSoundRejectsAnEmptyBufferWithFrames)
{
    AudioFixture fixture;

    EXPECT_TRUE(fixture.Get().CreateSound(SampleRate, Mono, nullptr, 10).IsFailure());
    EXPECT_TRUE(fixture.Get().CreateSound(0, Mono, nullptr, 0).IsFailure());
    EXPECT_TRUE(fixture.Get().CreateSound(SampleRate, 0, nullptr, 0).IsFailure());
}

TEST(SoundTest, DurationFollowsFromFrameCountAndRate)
{
    AudioFixture fixture;

    const Result<Sound> sound = fixture.Get().CreateSound(1000, Mono, nullptr, 0);
    ASSERT_TRUE(sound.IsSuccess());

    // A sound with no frames has no length rather than an undefined one.
    EXPECT_FLOAT_EQ(sound.Value().GetDuration(), 0.0f);
}

TEST(SoundTest, ToneHasTheRequestedLength)
{
    AudioFixture fixture;

    const Result<Sound> tone = fixture.Get().CreateTone(SampleRate, Mono, 440.0f, 0.5f, 0.25f);

    ASSERT_TRUE(tone.IsSuccess());
    EXPECT_EQ(tone.Value().GetFrameCount(), static_cast<std::uint64_t>(SampleRate) / 4);
    EXPECT_NEAR(tone.Value().GetDuration(), 0.25f, 1e-4f);
}

TEST(SoundTest, ToneRejectsAnImpossibleRequest)
{
    AudioFixture fixture;

    EXPECT_TRUE(fixture.Get().CreateTone(SampleRate, Mono, 0.0f, 0.5f, 1.0f).IsFailure());
    EXPECT_TRUE(fixture.Get().CreateTone(SampleRate, Mono, 440.0f, 0.5f, 0.0f).IsFailure());
}

TEST(SoundTest, LoadOfAMissingFileFails)
{
    AudioFixture fixture;

    const Result<Sound> sound = fixture.Get().LoadSound("no-such-sound.wav");

    ASSERT_TRUE(sound.IsFailure());
    EXPECT_EQ(sound.GetError().Code, ErrorCode::FileNotFound);
}

TEST(SoundTest, LoadOfSomethingThatIsNotAudioFails)
{
    AudioFixture fixture;

    static int counter = 0;
    const FilePath path = std::filesystem::temp_directory_path() /
                          ("ember-audio-test-" + std::to_string(++counter) + ".wav");
    ASSERT_TRUE(FileSystem::WriteTextFile(path, "this is not a wav file at all").IsSuccess());

    const Result<Sound> sound = fixture.Get().LoadSound(path);
    std::error_code error;
    std::filesystem::remove(path, error);

    ASSERT_TRUE(sound.IsFailure());
    EXPECT_EQ(sound.GetError().Code, ErrorCode::AssetError);
}

// --------------------------------------------------------------------- sources

TEST(AudioSourceTest, PlaySoundProducesAudio)
{
    AudioFixture fixture;

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 16);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());
    EXPECT_TRUE(source.IsPlaying());
    EXPECT_EQ(fixture.Get().GetActiveSourceCount(), 1u);
}

TEST(AudioSourceTest, PlaySoundWithoutASoundDoesNothing)
{
    AudioFixture fixture;

    AudioSource source;
    source.Play();

    EXPECT_FALSE(source.IsPlaying());
}

TEST(AudioSourceTest, PauseKeepsThePositionAndResumeContinues)
{
    AudioFixture fixture;

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 100);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());
    fixture.RenderAndDiscard(10);
    ASSERT_EQ(source.GetPlayhead(), 10u);

    source.Pause();
    EXPECT_FALSE(source.IsPlaying());
    fixture.RenderAndDiscard(10);

    // A paused source is not mixed, so it does not advance.
    EXPECT_EQ(source.GetPlayhead(), 10u);

    source.Play();
    fixture.RenderAndDiscard(5);
    EXPECT_EQ(source.GetPlayhead(), 5u);
}

TEST(AudioSourceTest, StopRewinds)
{
    AudioFixture fixture;

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 100);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());
    fixture.RenderAndDiscard(20);
    ASSERT_EQ(source.GetPlayhead(), 20u);

    source.Stop();

    EXPECT_FALSE(source.IsPlaying());
    EXPECT_EQ(source.GetPlayhead(), 0u);
    EXPECT_EQ(fixture.Get().GetActiveSourceCount(), 0u);
}

TEST(AudioSourceTest, DestroyingASourceStopsIt)
{
    AudioFixture fixture;

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 100);
    ASSERT_TRUE(sound.IsSuccess());

    {
        AudioSource source = fixture.Get().PlaySound(sound.Value());
        EXPECT_EQ(fixture.Get().GetActiveSourceCount(), 1u);
    }

    EXPECT_EQ(fixture.Get().GetActiveSourceCount(), 0u);
}

TEST(AudioSourceTest, VolumeIsClamped)
{
    AudioFixture fixture;

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 10);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());

    source.SetVolume(2.0f);
    EXPECT_FLOAT_EQ(source.GetVolume(), 1.0f);

    source.SetVolume(-1.0f);
    EXPECT_FLOAT_EQ(source.GetVolume(), 0.0f);
}

TEST(AudioSourceTest, PitchIsClampedAboveZero)
{
    AudioFixture fixture;

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 10);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());
    source.SetPitch(0.0f);

    // A zero pitch would never advance the playhead, so it is floored.
    EXPECT_GT(source.GetPitch(), 0.0f);
}

TEST(AudioSourceTest, LoopingIsRemembered)
{
    AudioFixture fixture;

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 10);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());
    source.SetLooping(true);

    EXPECT_TRUE(source.IsLooping());
}

TEST(AudioSourceTest, SourcesAreIndependent)
{
    AudioFixture fixture;

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 100);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource first = fixture.Get().PlaySound(sound.Value());
    AudioSource second = fixture.Get().PlaySound(sound.Value());

    fixture.RenderAndDiscard(10);
    first.Stop();

    EXPECT_FALSE(first.IsPlaying());
    EXPECT_TRUE(second.IsPlaying());
    EXPECT_EQ(second.GetPlayhead(), 10u);
}

TEST(AudioSourceTest, MovingASourceUpdatesItsPosition)
{
    AudioFixture fixture;

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 10);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());
    source.SetPosition(Vec3(1.0f, 2.0f, 3.0f));

    EXPECT_TRUE(glm::all(glm::epsilonEqual(source.GetPosition(), Vec3(1.0f, 2.0f, 3.0f), 1e-6f)));
}

// ---------------------------------------------------------------------- mixing

TEST(AudioMixingTest, SilentSourcesProduceSilence)
{
    AudioFixture fixture;

    const std::vector<float> buffer = fixture.Render(16);

    for (const float sample : buffer)
    {
        EXPECT_FLOAT_EQ(sample, 0.0f);
    }
}

TEST(AudioMixingTest, MonoSourceIsSummedIntoBothChannels)
{
    AudioFixture fixture;

    const Result<Sound> sound = MakeConstant(fixture.Get(), 0.5f, 32);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());
    const std::vector<float> buffer = fixture.Render(8);

    ASSERT_EQ(buffer.size(), 16u);
    EXPECT_NEAR(buffer[0], 0.5f, 1e-5f);
    EXPECT_NEAR(buffer[1], 0.5f, 1e-5f);

    source.Stop();
}

TEST(AudioMixingTest, SourcesAreSummed)
{
    AudioFixture fixture;

    const Result<Sound> first = MakeConstant(fixture.Get(), 0.25f, 32);
    const Result<Sound> second = MakeConstant(fixture.Get(), 0.25f, 32);
    ASSERT_TRUE(first.IsSuccess());
    ASSERT_TRUE(second.IsSuccess());

    AudioSource a = fixture.Get().PlaySound(first.Value());
    AudioSource b = fixture.Get().PlaySound(second.Value());

    const std::vector<float> buffer = fixture.Render(4);

    EXPECT_NEAR(buffer[0], 0.5f, 1e-5f);

    a.Stop();
    b.Stop();
}

TEST(AudioMixingTest, SourceVolumeScalesTheOutput)
{
    AudioFixture fixture;

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 32);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());
    source.SetVolume(0.25f);

    const std::vector<float> buffer = fixture.Render(4);
    EXPECT_NEAR(buffer[0], 0.25f, 1e-5f);

    source.Stop();
}

TEST(AudioMixingTest, MasterVolumeScalesEverySource)
{
    AudioFixture fixture;
    fixture.Get().GetListener().MasterVolume = 0.5f;

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 32);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());
    const std::vector<float> buffer = fixture.Render(4);

    EXPECT_NEAR(buffer[0], 0.5f, 1e-5f);

    source.Stop();
}

TEST(AudioMixingTest, SilencePastTheMaximumDistance)
{
    AudioFixture fixture;
    fixture.Get().SetReferenceDistance(1.0f);
    fixture.Get().SetMaxDistance(10.0f);

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 32);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());
    source.SetPosition(Vec3(0.0f, 0.0f, 100.0f));

    const std::vector<float> buffer = fixture.Render(4);

    EXPECT_FLOAT_EQ(buffer[0], 0.0f);

    source.Stop();
}

TEST(AudioMixingTest, AudibleWithinTheReferenceDistance)
{
    AudioFixture fixture;
    fixture.Get().SetReferenceDistance(10.0f);

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 32);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());
    source.SetPosition(Vec3(0.0f, 0.0f, 5.0f));

    const std::vector<float> buffer = fixture.Render(4);

    EXPECT_NEAR(buffer[0], 1.0f, 1e-5f);

    source.Stop();
}

TEST(AudioMixingTest, VolumeFallsOffWithDistance)
{
    AudioFixture fixture;
    fixture.Get().SetReferenceDistance(1.0f);
    fixture.Get().SetMaxDistance(11.0f);

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 64);
    ASSERT_TRUE(sound.IsSuccess());

    // Each source is measured on its own: two sources playing at once are summed
    // into one buffer, so their levels could not be told apart.
    AudioSource near = fixture.Get().PlaySound(sound.Value());
    near.SetPosition(Vec3(0.0f, 0.0f, 2.0f));
    const std::vector<float> nearBuffer = fixture.Render(4);
    near.Stop();

    AudioSource far = fixture.Get().PlaySound(sound.Value());
    far.SetPosition(Vec3(0.0f, 0.0f, 8.0f));
    const std::vector<float> farBuffer = fixture.Render(4);
    far.Stop();

    const float nearLevel = std::fabs(nearBuffer[0]);
    const float farLevel = std::fabs(farBuffer[0]);

    EXPECT_GT(nearLevel, 0.0f);
    EXPECT_LT(farLevel, nearLevel);
}

TEST(AudioMixingTest, SoundToTheRightIsLouderInTheRightSpeaker)
{
    AudioFixture fixture;
    fixture.Get().SetReferenceDistance(100.0f);
    fixture.Get().GetListener().Forward = Vec3(0.0f, 0.0f, 1.0f);
    fixture.Get().GetListener().Up = Vec3(0.0f, 1.0f, 0.0f);

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 64);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());
    source.SetPosition(Vec3(5.0f, 0.0f, 0.0f));

    const std::vector<float> buffer = fixture.Render(4);

    EXPECT_GT(buffer[1], buffer[0]);

    source.Stop();
}

TEST(AudioMixingTest, SoundToTheLeftIsLouderInTheLeftSpeaker)
{
    AudioFixture fixture;
    fixture.Get().SetReferenceDistance(100.0f);

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 64);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());
    source.SetPosition(Vec3(-5.0f, 0.0f, 0.0f));

    const std::vector<float> buffer = fixture.Render(4);

    EXPECT_GT(buffer[0], buffer[1]);

    source.Stop();
}

TEST(AudioMixingTest, SourceStopsWhenItsSoundRunsOut)
{
    AudioFixture fixture;

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 8);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());
    fixture.RenderAndDiscard(8);

    EXPECT_FALSE(source.IsPlaying());
    EXPECT_EQ(fixture.Get().GetActiveSourceCount(), 0u);
}

TEST(AudioMixingTest, LoopingSourceKeepsPlaying)
{
    AudioFixture fixture;

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 8);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());
    source.SetLooping(true);

    fixture.RenderAndDiscard(32);

    EXPECT_TRUE(source.IsPlaying());
    EXPECT_LT(source.GetPlayhead(), 8u);

    source.Stop();
}

TEST(AudioMixingTest, RenderCountsFrames)
{
    AudioFixture fixture;

    fixture.RenderAndDiscard(16);
    fixture.RenderAndDiscard(16);

    EXPECT_EQ(fixture.Get().GetRenderedFrameCount(), 32u);
}

TEST(AudioMixingTest, RenderingBeforeInitialiseProducesSilence)
{
    AudioDevice device;

    std::vector<float> buffer(8, 1.0f);
    const std::uint64_t written = device.RenderFrames(buffer.data(), 4);

    EXPECT_EQ(written, 0u);
    for (const float sample : buffer)
    {
        EXPECT_FLOAT_EQ(sample, 1.0f);
    }
}

TEST(AudioMixingTest, RenderingIntoNothingIsSafe)
{
    AudioFixture fixture;

    EXPECT_EQ(fixture.Get().RenderFrames(nullptr, 4), 0u);

    std::vector<float> empty;
    EXPECT_EQ(fixture.Get().RenderFrames(empty.data(), 0), 0u);
}

TEST(AudioMixingTest, PitchShortensThePlayback)
{
    AudioFixture fixture;

    const Result<Sound> sound = MakeConstant(fixture.Get(), 1.0f, 100);
    ASSERT_TRUE(sound.IsSuccess());

    AudioSource source = fixture.Get().PlaySound(sound.Value());
    source.SetPitch(2.0f);

    fixture.RenderAndDiscard(10);

    EXPECT_EQ(source.GetPlayhead(), 20u);

    source.Stop();
}