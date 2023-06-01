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

#if ! defined(JUCE_WEBMIDI)
#define JUCE_WEBMIDI 1
#endif

#if JUCE_WEBMIDI
#define __WEB_MIDI_API__ 1
#endif

#include "wasm/RtMidi.cpp"

#undef __WEB_MIDI_API__

namespace juce
{

class MidiInput::Pimpl {};

// (These are just stub functions if ALSA is unavailable...)
MidiInput::MidiInput (const String& deviceName, const String& deviceID)
    : deviceInfo (deviceName, deviceID)
{
}

MidiInput::~MidiInput()                                                                   {}
void MidiInput::start()                                                                   {}
void MidiInput::stop()                                                                    {}
Array<MidiDeviceInfo> MidiInput::getAvailableDevices()                                    { return {}; }
MidiDeviceInfo MidiInput::getDefaultDevice()                                              { return {}; }
std::unique_ptr<MidiInput> MidiInput::openDevice (const String&, MidiInputCallback*)      { return {}; }
// std::unique_ptr<MidiInput> MidiInput::createNewDevice (const String&, MidiInputCallback*) { return {}; }
StringArray MidiInput::getDevices()                                                       { return {}; }
int MidiInput::getDefaultDeviceIndex()                                                    { return 0;}
std::unique_ptr<MidiInput> MidiInput::openDevice (int, MidiInputCallback*)                { return {}; }

class MidiOutput::Pimpl {};

MidiOutput::~MidiOutput()                                                                 {}
void MidiOutput::sendMessageNow (const MidiMessage&)                                      {}
Array<MidiDeviceInfo> MidiOutput::getAvailableDevices()                                   { return {}; }
MidiDeviceInfo MidiOutput::getDefaultDevice()                                             { return {}; }
std::unique_ptr<MidiOutput> MidiOutput::openDevice (const String&)                        { return {}; }
// std::unique_ptr<MidiOutput> MidiOutput::createNewDevice (const String&)                   { return {}; }
StringArray MidiOutput::getDevices()                                                      { return {}; }
int MidiOutput::getDefaultDeviceIndex()                                                   { return 0;}
std::unique_ptr<MidiOutput> MidiOutput::openDevice (int)                                  { return {}; }

    // TODO: implement real midi Pimpls

// struct JuceRtMidiContext {
//     RtMidiIn* rtmidi;
//     MidiInput* midiIn;
//     MidiInputCallback* callback{nullptr};
// };

// //==============================================================================
// MidiInput::MidiInput (const String& deviceName, const String& deviceID)
//     : deviceInfo (deviceName, deviceID) {
//     auto ctx = new JuceRtMidiContext();
//     ctx->rtmidi = new RtMidiIn();
//     ctx->midiIn = this;
//     internal = ctx;
// }

// MidiInput::~MidiInput() {
//     delete (RtMidiIn*) internal;
// }

// void MidiInput::start() { }

// void MidiInput::stop() { }

// Array<MidiDeviceInfo> MidiInput::getAvailableDevices() {
//     Array<MidiDeviceInfo> ret{};
//     RtMidiIn rtmidi{};

//     for (int i = 0; i < rtmidi.getPortCount(); i++)
//         ret.add(MidiDeviceInfo(rtmidi.getPortName(i), String::formatted("MidiIn_%d", i)));

//     return ret;
// }

// MidiDeviceInfo MidiInput::getDefaultDevice() {
//     return getAvailableDevices()[getDefaultDeviceIndex()];
// }

// void rtmidiCallback(double timeStamp, std::vector<unsigned char> *message, void *userData) {
//     auto ctx = (JuceRtMidiContext*) userData;
//     auto callback = ctx->callback;
//     auto midiIn = ctx->midiIn;
//     const void* data = message->data();
//     int numBytes = message->size();
//     // JUCE does not accept zero timestamp value, but RtMidi is supposed to send 0 for the first
//     // message. To resolve that conflict, we offset 0.0 to slightly positive time.
//     MidiMessage midiMessage{data, numBytes, timeStamp > 0.0 ? timeStamp : 0.00000001};

//     callback->handleIncomingMidiMessage(midiIn, midiMessage);
// }

// std::unique_ptr<MidiInput> MidiInput::openDevice (const String& deviceIdentifier, MidiInputCallback* callback) {
//     RtMidiIn rtmidiStatic{};

//     std::unique_ptr<MidiInput> ret{nullptr};
//     for (int i = 0; i < rtmidiStatic.getPortCount(); i++)
//         if (String::formatted("MidiIn_%d", i) == deviceIdentifier) {
//             ret.reset(new MidiInput(rtmidiStatic.getPortName(i), deviceIdentifier));
//             auto ctx = (JuceRtMidiContext*) ret->internal;
//             ctx->callback = callback;
//             auto rtmidi = ctx->rtmidi;
//             rtmidi->setCallback(rtmidiCallback, ctx);
//             rtmidi->openPort(i);
//             return std::move(ret);
//         }
//     jassertfalse;
//     return nullptr;
// }

// StringArray MidiInput::getDevices() {
//     StringArray ret{};
//     for (auto dev : getAvailableDevices())
//         ret.add(dev.name);
//     return {};
// }

// int MidiInput::getDefaultDeviceIndex() { return 0; }

// std::unique_ptr<MidiInput> MidiInput::openDevice (int index, MidiInputCallback* callback) {
//     return openDevice(getAvailableDevices()[index].identifier, callback);
// }

// //==============================================================================

// MidiOutput::~MidiOutput() {
//     delete (RtMidiOut*) internal;
// }

// void MidiOutput::sendMessageNow (const MidiMessage& message) {
//     ((RtMidiOut *) internal)->sendMessage(message.getRawData(), message.getRawDataSize());
// }

// Array<MidiDeviceInfo> MidiOutput::getAvailableDevices() {
//     Array<MidiDeviceInfo> ret{};
//     RtMidiOut rtmidi{};

//     for (int i = 0; i < rtmidi.getPortCount(); i++)
//         ret.add(MidiDeviceInfo(rtmidi.getPortName(i), String::formatted("MidiOut_%d", i)));

//     return ret;
// }

// MidiDeviceInfo MidiOutput::getDefaultDevice() {
//     return getAvailableDevices()[getDefaultDeviceIndex()];
// }

// std::unique_ptr<MidiOutput> MidiOutput::openDevice (const String& deviceIdentifier) {
//     RtMidiOut rtmidi{};
//     std::unique_ptr<MidiOutput> ret{nullptr};
//     for (int i = 0; i < rtmidi.getPortCount(); i++) {
//         if (String::formatted("MidiOut_%d", i) == deviceIdentifier) {
//             auto midiOut = new MidiOutput(rtmidi.getPortName(i), deviceIdentifier);
//             ret.reset(midiOut);
//             midiOut->internal = new RtMidiOut();
//             ((RtMidiOut *) ret->internal)->openPort(i);
//             return std::move(ret);
//         }
//     }
//     jassertfalse;
//     return nullptr;
// }

// StringArray MidiOutput::getDevices() {
//     StringArray ret{};
//     for (auto dev : getAvailableDevices())
//         ret.add(dev.name);
//     return {};
// }
// int MidiOutput::getDefaultDeviceIndex() { return 0; }

// std::unique_ptr<MidiOutput> MidiOutput::openDevice (int index) {
//     return openDevice(getAvailableDevices()[index].identifier);
// }

} // namespace juce
