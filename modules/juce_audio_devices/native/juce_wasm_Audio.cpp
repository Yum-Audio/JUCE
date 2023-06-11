/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2017 - ROLI Ltd.

   JUCE is an open source library subject to commercial or open-source
   licensing.

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   To use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

// Declarations from juce_emscripten_Messaging.
extern void registerCallbackToMainThread (std::function<void()> f);

int getAudioContextSampleRate() {
    return MAIN_THREAD_EM_ASM_INT({
        var AudioContext = window.AudioContext || window.webkitAudioContext;
        var ctx = new AudioContext();
        var sr = ctx.sampleRate;
        ctx.close();
        return sr;
    });
}

#if ASSUME_AL_FLOAT32
#define AL_FORMAT_MONO_FLOAT32                   0x10010
#define AL_FORMAT_STEREO_FLOAT32                 0x10011
#endif

//==============================================================================
class OpenALAudioIODevice : public AudioIODevice
{
    int bufferSize = 512;
    double sampleRate = 44100.0;

    int numIn = 1;
    int numOut = 2;

    std::atomic<int> numUnderRuns = 0;
    bool playing = false;

    ALCdevice* inDevice = nullptr;
    ALenum inFormat;

    ALCdevice* outDevice = nullptr;
    ALCcontext* outContext = nullptr;
    ALenum outFormat;

    ALuint source;
    Array<ALuint> bufferIds;
    ALuint frequency;
    ALenum errorCode = AL_NO_ERROR;

public:
    class AudioFeedStateMachine
    {
    public:
        enum StateType
        {
            StateWaitingForInteraction,
            StatePlaying,
            StateStopped
        };

        enum StatusType
        {
            StatusGood,
            StatusError,
            StatusNeedToWait
        };

    private:
        OpenALAudioIODevice* parent{nullptr};
        AudioIODeviceCallback* callback{nullptr};

        int16* formatBuffer = nullptr;
        int16* inFormatBuffer = nullptr;

        AudioSampleBuffer inBuffer;
        AudioSampleBuffer outBuffer;

        StateType state = StateWaitingForInteraction;

        static int getBytesPerSample (ALCenum format)
        {
            switch(format)
            {
                case AL_FORMAT_MONO8:          return 1;
                case AL_FORMAT_MONO16:         return 2;
                case AL_FORMAT_STEREO8:        return 1;
                case AL_FORMAT_STEREO16:       return 2;
            #ifdef ASSUME_AL_FLOAT32
                case AL_FORMAT_MONO_FLOAT32:   return 4;
                case AL_FORMAT_STEREO_FLOAT32: return 4;
            #endif
            }
        }

    public:
        AudioFeedStateMachine (OpenALAudioIODevice* parent) : parent(parent) { }

        ~AudioFeedStateMachine ()
        {
            OpenALAudioIODevice::sessionsOnMainThread.removeAllInstancesOf (this);

            if (StatePlaying)
                callback->audioDeviceStopped();

            delete[] formatBuffer;
            delete[] inFormatBuffer;
        }

        StateType getState () const { return state; }

        void start (AudioIODeviceCallback* callback)
        {
            this->callback = callback;
            callback->audioDeviceAboutToStart (parent);
        }

        static void convertFloatToInt16 (AudioSampleBuffer& juce, int16_t* openAl)
        {
            constexpr auto scale = 0x7fff;

            for (int c = 0; c < juce.getNumChannels(); c ++)
            {
                for (int i = 0; i < juce.getNumSamples(); i ++)
                {
                    float x = std::min(juce.getArrayOfWritePointers()[c][i], 1.0f);
                    x = std::max(x, -1.0f);
                    openAl[c + i * juce.getNumChannels()] = x * scale;
                }
            }
        }

        static void convertInt16ToFloat (int16_t* openAl, AudioSampleBuffer& juce, int numSamples)
        {
            constexpr float scale = 1.0f / 0x7fff;

            const auto samples = jmin (numSamples, juce.getNumSamples());

            for (int c = 0; c < juce.getNumChannels(); c ++)
            {
                for (int i = 0; i < samples; i ++)
                {
                    const auto index = c + i * juce.getNumChannels();
                    juce.setSample (c, i, openAl[index] * scale);
                }
            }
        }

        // Loosely adapted from https://kcat.strangesoft.net/openal-tutorial.html
        StatusType nextStep (bool shouldStop = false)
        {
            const auto source = parent->source;

            const auto numIn = parent->numIn;
            const auto numOut = parent->numOut;
            const auto bufferSize = parent->bufferSize;

            if (state == StateWaitingForInteraction)
            {
                if (shouldStop)
                {
                    state = StateStopped;
                    return StatusGood;
                }

                state = StatePlaying;

                alSourceQueueBuffers (source, parent->bufferIds.size(), parent->bufferIds.data());
                alSourcePlay (source);
                if ((parent->errorCode = alGetError()) != AL_NO_ERROR)
                {
                    DBG("OpenAL error occurred when starting to play.");
                    return StatusError;
                }

                inBuffer.setSize (parent->numIn, 16384);
                outBuffer.setSize (parent->numOut, parent->bufferSize);

                formatBuffer = new int16[bufferSize * numOut];
                inFormatBuffer = new int16[parent->getAvailableBufferSizes().getLast() * numIn];
            }

            else if (state == StatePlaying)
            {
                if (shouldStop)
                {
                    state = StateStopped;
                    callback->audioDeviceStopped();
                    return StatusGood;
                }

                ALuint buffer;
                ALint val;

                alGetSourcei (source, AL_SOURCE_STATE, & val);
                if(val != AL_PLAYING)
                    alSourcePlay (source);
                
                alGetSourcei (source, AL_BUFFERS_PROCESSED, & val);
                if (val <= 0)
                    return StatusNeedToWait;

                if (inBuffer.getNumSamples() > 0 && inBuffer.getNumChannels() > 0)
                    inBuffer.clear();

                if (outBuffer.getNumSamples() > 0 && outBuffer.getNumChannels() > 0)
                    outBuffer.clear();

                if (val == parent->bufferIds.size())
                    parent->numUnderRuns++;

                int bytePerSampleOut = getBytesPerSample (parent->outFormat) * numOut;

                // inCapturedSamples will remain 0 until the user clicks allow for the microphone
                // https://emscripten.org/docs/porting/Audio.html#emscripten-specific-capture-behavior
                ALCint inCapturedSamples = 0;
                alcGetIntegerv (parent->inDevice, ALC_CAPTURE_SAMPLES, 1, &inCapturedSamples);
                alcCaptureSamples (parent->inDevice, inFormatBuffer, inCapturedSamples);

                convertInt16ToFloat (inFormatBuffer, inBuffer, inCapturedSamples);

                while (val --)
                {
                    AudioIODeviceCallbackContext ctx;

                    callback->audioDeviceIOCallbackWithContext (
                        inBuffer.getArrayOfReadPointers(), inBuffer.getNumChannels(),
                        outBuffer.getArrayOfWritePointers(), outBuffer.getNumChannels(),
                        bufferSize, ctx);

                    convertFloatToInt16 (outBuffer, formatBuffer);

                    alSourceUnqueueBuffers (source, 1, &buffer);
                    alBufferData (buffer, parent->outFormat, formatBuffer,
                                  bufferSize * bytePerSampleOut, parent->frequency);

                    alSourceQueueBuffers (source, 1, &buffer);

                    if ((parent->errorCode = alGetError()) != AL_NO_ERROR)
                    {
                        DBG("OpenAL error occurred when playing.");
                        DBG(String(alcGetString(nullptr, parent->errorCode)));
                        return StatusError;
                    }
                }
            }
            return StatusGood;
        }
    };

    struct AudioThread : public Thread
    {
        AudioFeedStateMachine stateMachine;
        OpenALAudioIODevice* parent;

        AudioThread (OpenALAudioIODevice* parent)
          : Thread("OpenAL Audio Thread"), stateMachine(parent)
        {
            this->parent = parent;
        }

        ~AudioThread () { }

        void start (AudioIODeviceCallback* callback)
        {
            stateMachine.start (callback);
            juce::Thread::RealtimeOptions options;
            options.priority = 10;
            startRealtimeThread (options);
        }

        void stop ()
        {
            stopThread (500);
        }

        void run () override
        {
            while (stateMachine.getState() != AudioFeedStateMachine::StateStopped)
            {
                auto status = stateMachine.nextStep (threadShouldExit());
                if (status == AudioFeedStateMachine::StatusNeedToWait)
                    sleep (1);
            }
        }
    };

public:
    OpenALAudioIODevice (bool threadBased = false)
    : AudioIODevice ("OpenAL", "OpenAL")
    , threadBased(threadBased)
    {
    }

    ~OpenALAudioIODevice ()
    {
        if (isDeviceOpen)
            closeInternal();
    }

    StringArray getOutputChannelNames () override
    {
        return { "Out #1", "Out #2" };
    }

    StringArray getInputChannelNames () override
    {
        return { "In #1" };
    }

    Array<double> getAvailableSampleRates () override {
        // OfflineAudioContexts are required to support sample rates ranging
        // from 8000 to 96000.
        static const Array<double> sampleRates =
        {
            22050, 32000, 37800, 44100, 48000, 88200, 96000
        };

        return sampleRates;
    }

    Array<int> getAvailableBufferSizes () override 
    {
        // https://webaudio.github.io/web-audio-api/#dom-scriptprocessornode-buffersize
        return { 256, 512, 1024, 2048, 4096, 8192, 16384 };
    }
    
    int getDefaultBufferSize () override
    {
        return 2048;
    }

    String open (const BigInteger& inputChannels,
                 const BigInteger& outputChannels,
                 double sampleRate,
                 int bufferSizeSamples) override
    {
        const ScopedLock lock (sessionsLock);
        return openInternal (inputChannels, outputChannels,
                             sampleRate, bufferSizeSamples);
    }
    
    void close () override
    {
        closeInternal();
    }

    bool isOpen () override
    {
        return isDeviceOpen;
    }

    void start (AudioIODeviceCallback* newCallback) override
    {
        const ScopedLock lock (sessionsLock);
        startInternal (newCallback);
    }

    void stop () override
    {
        const ScopedLock lock (sessionsLock);

        if (isPlaying())
            stopInternal();
    }

    bool isPlaying () override
    {
        return playing;
    }

    String getLastError () override
    {
        if (errorCode != AL_NO_ERROR)
            return getAlcError (errorCode);

        return {};
    }

    //==============================================================================
    int getCurrentBufferSizeSamples () override           { return bufferSize; }
    double getCurrentSampleRate () override               { return sampleRate; }
    int getCurrentBitDepth () override                    { return 16; }

    BigInteger getActiveOutputChannels () const override
    {
        BigInteger b;
        b.setRange (0, numOut, true);

        return b;
    }
    BigInteger getActiveInputChannels () const override
    {
        BigInteger b;
        b.setRange (0, numIn, true);

        return b;
    }

    int getOutputLatencyInSamples () override
    {
        return bufferIds.size() * bufferSize;
    }

    int getInputLatencyInSamples () override
    {
        return bufferIds.size() * bufferSize;
    }

    int getXRunCount () const noexcept override
    {
        return numUnderRuns;
    }
    
    static CriticalSection sessionsLock;
    static Array<AudioFeedStateMachine*> sessionsOnMainThread;

private:
    bool isDeviceOpen = false;
    bool threadBased = false;

    static String getAlcError(ALCenum err)
    {
        return alcGetString(nullptr, err);
    }

    static String getDeviceError (ALCdevice* device)
    {
        const auto err = alcGetError (device);

        if (err != AL_NO_ERROR)
            return getAlcError (err);

        return {};
    }

    String openInputDevice()
    {
        if (numIn == 1)
            inFormat = AL_FORMAT_MONO16;
        else if (numOut == 2)
            inFormat = AL_FORMAT_STEREO16;
        else
            return "Invalid input channel configuration.";

        errorCode = alGetError();
        inDevice = alcCaptureOpenDevice(nullptr, sampleRate, inFormat, bufferSize);

        if (inDevice == nullptr)
            return "Failed to open output device - " + getDeviceError (inDevice);

        return {};
    }

    String openOutputDevice()
    {
        errorCode = alGetError();
        outDevice = alcOpenDevice (nullptr);

        if (outDevice == nullptr)
            return "Failed to open output device - " + getDeviceError (inDevice);

        outContext = alcCreateContext (outDevice, nullptr);
        alcMakeContextCurrent (outContext);

        if (outContext == nullptr || (errorCode = alGetError()) != AL_NO_ERROR)
            return "Failed to create output context " + getAlcError (errorCode);

        return {};
    }

    String openInternal (const BigInteger& inputChannels,
                         const BigInteger& outputChannels,
                         double sampleRate,
                         int bufferSizeSamples)
    {
        closeInternal();

        this->numIn = inputChannels.countNumberOfSetBits();
        this->numOut = outputChannels.countNumberOfSetBits();
        this->bufferSize = bufferSizeSamples;
        this->sampleRate = sampleRate;

        if (numOut > 0)
        {
            const auto openOutResult = openOutputDevice();
            if (openOutResult.isNotEmpty())
                return openOutResult;
        }

        bufferIds.resize(numIn + numOut);

        alGenBuffers (bufferIds.size(), bufferIds.data());
        alGenSources (1, &source);

        if ((errorCode = alGetError()) != AL_NO_ERROR)
            return "Failed to generate sources.";

        if (numOut == 1)
            outFormat = AL_FORMAT_MONO16;
        else if (numOut == 2)
            outFormat = AL_FORMAT_STEREO16;
        else
            return "Invalid output channel configuration.";

        frequency = (ALuint) sampleRate;
        isDeviceOpen = true;

        if (numIn > 0)
        {
            const auto openInResult = openInputDevice();
            if (openInResult.isNotEmpty())
                return openInResult;
        }

        return {};
    }
    
    void closeInternal ()
    {
        const ScopedLock lock (sessionsLock);
        stopInternal();

        if (isDeviceOpen)
        {
            alDeleteSources (1, &source);
            alDeleteBuffers (bufferIds.size(), bufferIds.data());
        }

        if (outContext)
        {
            alcMakeContextCurrent (nullptr);
            alcDestroyContext (outContext);
            outContext = nullptr;
        }

        const auto closeDevice = [this](ALCdevice*& dev)
        {
            if (dev == nullptr)
                return;

            if (dev == inDevice)
                alcCaptureCloseDevice (dev);
            else
                alcCloseDevice (dev);

            dev = nullptr;
        };

        closeDevice (outDevice);
        closeDevice (inDevice);

        isDeviceOpen = false;
    }

    void startInternal (AudioIODeviceCallback* newCallback)
    {
        numUnderRuns = 0;

        if (threadBased)
        {
            audioThread.reset (new AudioThread(this));
            audioThread->start (newCallback);
        }
        else
        {
            audioStateMachine.reset (new AudioFeedStateMachine (this));
            audioStateMachine->start (newCallback);
            sessionsOnMainThread.add (audioStateMachine.get());
        }

        if (inDevice != nullptr)
            alcCaptureStart (inDevice);

        playing = true;
    }

    void stopInternal ()
    {
        if (audioThread)
            audioThread->stop ();

        audioStateMachine.reset (nullptr);

        if (inDevice != nullptr)
            alcCaptureStop (inDevice);
    }

    std::unique_ptr<AudioThread> audioThread;
    std::unique_ptr<AudioFeedStateMachine> audioStateMachine;
};

Array<OpenALAudioIODevice::AudioFeedStateMachine*> OpenALAudioIODevice::sessionsOnMainThread;
CriticalSection OpenALAudioIODevice::sessionsLock;

//==============================================================================
struct OpenALAudioIODeviceType  : public AudioIODeviceType
{
    OpenALAudioIODeviceType () : AudioIODeviceType ("OpenAL")
    {
        if (! openALMainThreadRegistered)
        {
            // The audio callback must be on the main thread
            // See https://emscripten.org/docs/porting/Audio.html#guidelines-for-audio-on-emscripten
            registerCallbackToMainThread ([]()
            {
                using AudioFeedStateMachine = OpenALAudioIODevice::AudioFeedStateMachine;
                const ScopedLock lock (OpenALAudioIODevice::sessionsLock);

                for (auto* session : OpenALAudioIODevice::sessionsOnMainThread)
                    if (session->getState() != AudioFeedStateMachine::StateStopped)
                        session->nextStep ();

            });
            openALMainThreadRegistered = true;
        }
    }

    bool openALMainThreadRegistered{false};

    StringArray getDeviceNames (bool) const override                       { return StringArray ("OpenAL"); }
    void scanForDevices () override                                        {}
    int getDefaultDeviceIndex (bool) const override                        { return 0; }
    int getIndexOfDevice (AudioIODevice* device, bool) const override      { return device != nullptr ? 0 : -1; }
    bool hasSeparateInputsAndOutputs () const override                     { return false; }

    AudioIODevice* createDevice (const String& outputName, const String& inputName) override
    {
        if (outputName == "OpenAL" || inputName == "OpenAL")
            return new OpenALAudioIODevice();

        return nullptr;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenALAudioIODeviceType)
};

} // namespace juce
