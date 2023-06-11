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

#include "wasm/libremidi/libremidi.hpp"

namespace juce
{

// WebMIDI is only available of HTTPS
// https://developer.mozilla.org/en-US/docs/Web/API/Web_MIDI_API
static bool isWedMidiAvailable()
{
    return libremidi::webmidi_helpers::midi_access_emscripten::instance().available();
}

class MidiDeviceObserver
{
public:
    MidiDeviceObserver()
    {
        configureObserver();
    }

    ~MidiDeviceObserver()
    {
        observer = nullptr;
        clearSingletonInstance();
    }

    Array<MidiDeviceInfo> getInputs()
    {
        const ScopedReadLock lk (deviceLock);
        return inputDevices;
    }

    Array<MidiDeviceInfo> getOutputs()
    {
        const ScopedReadLock lk (deviceLock);
        return outputDevices;
    }

    JUCE_DECLARE_SINGLETON (MidiDeviceObserver, false)

private:
    void configureObserver()
    {
        if (isWedMidiAvailable())
        {
            const ScopedWriteLock lk (deviceLock);
            libremidi::observer::callbacks callbacks
            {
                .input_added = [&] (int idx, const std::string& name)
                {
                    const ScopedWriteLock lk (deviceLock);
                    std::cout << "MIDI Input connected: " << idx << " - " << name << std::endl;
                    inputDevices.add ({ name, String (idx) });
                },

                .input_removed = [&] (int idx, const std::string& name)
                {
                    const ScopedWriteLock lk (deviceLock);
                    inputDevices.removeIf([name](MidiDeviceInfo& dev){ return dev.name.toStdString() == name; });
                },

                .output_added = [&] (int idx, const std::string& name)
                {
                    const ScopedWriteLock lk (deviceLock);
                    std::cout << "MIDI Output connected: " << idx << " - " << name << std::endl;
                    outputDevices.add ({ name, String (idx) });
                },

                .output_removed = [&] (int idx, const std::string& name)
                {
                    const ScopedWriteLock lk (deviceLock);
                    outputDevices.removeIf([name](MidiDeviceInfo& dev){ return dev.name.toStdString() == name; });
                }
            };

            observer = std::make_unique<libremidi::observer>(libremidi::API::EMSCRIPTEN_WEBMIDI, std::move(callbacks));
        }
    }

    ReadWriteLock deviceLock;

    Array<MidiDeviceInfo> inputDevices;
    Array<MidiDeviceInfo> outputDevices;

    std::unique_ptr<libremidi::observer> observer;
};

JUCE_IMPLEMENT_SINGLETON (MidiDeviceObserver)

class MidiInput::Pimpl
{
public:
    Pimpl (const MidiDeviceInfo& device)
    {
        input = std::make_shared<libremidi::midi_in>();
        input->set_callback([this](const libremidi::message& msg)
        {
            if (onMessageReceived != nullptr)
                onMessageReceived (msg);
        });

        input->open_port (device.identifier.getIntValue(), device.name.toRawUTF8());
    }

    ~Pimpl()
    {
        input->set_callback (nullptr);
    }

    std::function<void(const libremidi::message&)> onMessageReceived;
    MidiInputCallback* callback = nullptr;

private:
    std::shared_ptr<libremidi::midi_in> input;
};

//==============================================================================
MidiInput::MidiInput (const String& deviceName, const String& deviceIdentifier)
    : deviceInfo (deviceName, deviceIdentifier)
{
}

MidiInput::~MidiInput()
{
}

void MidiInput::start()
{
    if (internal != nullptr)
    {
        internal->onMessageReceived = [this](const libremidi::message& msg)
        {
            if (internal->callback != nullptr)
            {
                if (msg.get_message_type() == libremidi::message_type::SYSTEM_EXCLUSIVE)
                {
                    internal->callback->handlePartialSysexMessage (this, msg.bytes.data(), msg.bytes.size(), msg.timestamp);
                }
                else
                {
                    MidiMessage message (msg.bytes.data(), msg.bytes.size(), msg.timestamp > 0.0 ? msg.timestamp : 0.00000001);
                    internal->callback->handleIncomingMidiMessage (this, message);
                }
            }
        };
    }
}

void MidiInput::stop()
{
    if (internal != nullptr)
        internal->onMessageReceived = nullptr;
}

Array<MidiDeviceInfo> MidiInput::getAvailableDevices()
{
    return MidiDeviceObserver::getInstance()->getInputs();
}

MidiDeviceInfo MidiInput::getDefaultDevice()
{
    return getAvailableDevices().getFirst();
}

std::unique_ptr<MidiInput> MidiInput::openDevice (const String& deviceIdentifier, MidiInputCallback* callback)
{
    for (auto& device : getAvailableDevices())
    {
        if (device.identifier == deviceIdentifier)
        {
            auto midiInput = rawToUniquePtr (new MidiInput (device.name, device.identifier));
            midiInput->internal = std::make_unique<Pimpl> (device);
            midiInput->internal->callback = callback;

            return midiInput;
        }
    }

    return nullptr;
}

StringArray MidiInput::getDevices()
{
    StringArray deviceNames;

    for (auto& d : getAvailableDevices())
        deviceNames.add (d.name);

    return deviceNames;
}

int MidiInput::getDefaultDeviceIndex()
{
    return 0;
}

std::unique_ptr<MidiInput> MidiInput::openDevice (int index, MidiInputCallback* callback)
{
    return openDevice (getAvailableDevices()[index].identifier, callback);
}

// //==============================================================================

class MidiOutput::Pimpl
{
public:
    Pimpl (const MidiDeviceInfo& device)
    {
        output = std::make_shared<libremidi::midi_out>();
        output->open_port (device.identifier.getIntValue(), device.name.toRawUTF8());
    }

    void sendMessage (const MidiMessage& message)
    {
        if (output != nullptr)
            output->send_message (message.getRawData(), message.getRawDataSize());
    }

private:
    std::shared_ptr<libremidi::midi_out> output;

};

MidiOutput::~MidiOutput() noexcept
{}

void MidiOutput::sendMessageNow (const MidiMessage& message)
{
    internal->sendMessage (message);
}

Array<MidiDeviceInfo> MidiOutput::getAvailableDevices()
{
    return MidiDeviceObserver::getInstance()->getOutputs();
}

MidiDeviceInfo MidiOutput::getDefaultDevice()
{
    return getAvailableDevices().getFirst();
}

std::unique_ptr<MidiOutput> MidiOutput::openDevice (const String& deviceIdentifier)
{
    for (auto& device : getAvailableDevices())
    {
        if (device.identifier == deviceIdentifier)
        {
            auto midiOutput = rawToUniquePtr (new MidiOutput (device.name, device.identifier));
            midiOutput->internal = std::make_unique<Pimpl> (device);

            return midiOutput;
        }
    }

    return nullptr;
}

StringArray MidiOutput::getDevices()
{
    StringArray deviceNames;

    for (auto& d : getAvailableDevices())
        deviceNames.add (d.name);

    return deviceNames;
}

 int MidiOutput::getDefaultDeviceIndex()
 {
    return 0;
 }

std::unique_ptr<MidiOutput> MidiOutput::openDevice (int index)
{
    return openDevice (getAvailableDevices()[index].identifier);
}

} // namespace juce
