/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2022 - Raw Material Software Limited

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

#include <emscripten/fetch.h>

namespace juce
{

StreamingSocket::StreamingSocket() {}
StreamingSocket::~StreamingSocket() {}
void StreamingSocket::close() {}
int StreamingSocket::write(void const*, int) { return 0; }
int StreamingSocket::read(void*, int, bool) { return 0; }
int StreamingSocket::waitUntilReady (bool, int) { return 0; }
bool StreamingSocket::connect(juce::String const&, int, int) { return false; }

NamedPipe::~NamedPipe() {}
void NamedPipe::close() {}
bool NamedPipe::isOpen() const { return false; }
int NamedPipe::write(void const*, int, int) { return 0; }
int NamedPipe::read(void*, int, int) { return 0; }

class NamedPipe::Pimpl
{
};

void MACAddress::findAllAddresses(Array<MACAddress>&)
{
}

class WebInputStream::Pimpl
{
public:
    Pimpl (WebInputStream& pimplOwner, const URL& urlToUse, bool addParametersToBody)
      : owner (pimplOwner),
        url (urlToUse),
        addParametersToRequestBody (addParametersToBody),
        hasBodyDataToSend (addParametersToRequestBody || url.hasBodyDataToSend()),
        httpRequestCmd (hasBodyDataToSend ? "POST" : "GET")
    {
        // If an exception is thrown from the user callback, it bubbles up to self.onerror but is otherwise completely
        // swallowed by xhr.send.
        EM_ASM({self.onerror = function() {
            console.log('Got error');
            HEAP32[$0 >> 2] = 2;
        };}, &result);
    }

    ~Pimpl()
    {
        cancel();
    }

    bool connect (WebInputStream::Listener* webInputListener, [[maybe_unused]] int numRetries = 0)
    {
        const ScopedLock lock (createConnectionLock);

        listenerCallback = webInputListener;

        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);

        attr.userData = this;
        strncpy (attr.requestMethod, httpRequestCmd.c_str(), 32);
        attr.attributes = EMSCRIPTEN_FETCH_REPLACE | EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_SYNCHRONOUS;

        if (hasBodyDataToSend)
        {
            WebInputStream::createHeadersAndPostData (url,
                                          headers,
                                          postData,
                                          addParametersToRequestBody);

            if (! postData.isEmpty())
            {
                attr.requestData = (const char*) postData.getData();
                attr.requestDataSize = postData.getSize();
            }
        }

        auto fetchHeaders = getStdHeaders (headers);

        if (fetchHeaders.size() > 1)
            attr.requestHeaders = fetchHeaders.data();

        attr.onreadystatechange = [](emscripten_fetch_t *fetch)
        {
            if(fetch->readyState != 2)
                return;

            auto* thisPimpl = (Pimpl*) fetch->userData;
            thisPimpl->parseFetchHeaders (fetch);
        };

        attr.onprogress = [](emscripten_fetch_t *fetch)
        {
            if (fetch->totalBytes > 0)
                printf("Downloading.. %.2f%% complete.\n", (fetch->dataOffset + fetch->numBytes) * 100.0 / fetch->totalBytes);
            else
                printf("Downloading.. %lld bytes complete.\n", fetch->dataOffset + fetch->numBytes);
        };

        attr.onsuccess = [](emscripten_fetch_t *fetch)
        {
            auto* thisPimpl = (Pimpl*) fetch->userData;
            thisPimpl->result = 0;

            thisPimpl->parseFetchHeaders (fetch);
        };

        attr.onerror = [](emscripten_fetch_t* fetch)
        {
            printf("Download failed!\n");
            auto* thisPimpl = (Pimpl*) fetch->userData;
            thisPimpl->fetchTask = nullptr;
        };

        fetchTask = emscripten_fetch (&attr, url.toString (true).toRawUTF8());

        statusCode = fetchTask->status;

        return result != -1;
    }

    void cancel()
    {
        const ScopedLock lock (createConnectionLock);
        emscripten_fetch_close(fetchTask);

        hasBeenCancelled = true;
    }

    //==============================================================================
    // WebInputStream methods
    void withExtraHeaders (const String& extraHeaders)
    {
        if (! headers.endsWithChar ('\n') && headers.isNotEmpty())
            headers << "\r\n";

        headers << extraHeaders;

        if (! headers.endsWithChar ('\n') && headers.isNotEmpty())
            headers << "\r\n";
    }

    void withCustomRequestCommand (const String& customRequestCommand)    { httpRequestCmd = customRequestCommand.toStdString(); }
    void withConnectionTimeout (int timeoutInMs)                          { timeOutMs = timeoutInMs; }
    void withNumRedirectsToFollow (int maxRedirectsToFollow)              { numRedirectsToFollow = maxRedirectsToFollow; }
    StringPairArray getRequestHeaders() const                             { return WebInputStream::parseHttpHeaders (headers); }
    StringPairArray getResponseHeaders() const                            { return responseHeaders; }
    int getStatusCode() const                                             { return statusCode; }

    //==============================================================================
    bool isError() const                { return fetchTask == nullptr; }
    int64 getTotalLength()              { return fetchTask == nullptr ? -1 : fetchTask->numBytes - 1; }
    bool isExhausted()                  { return position >= getTotalLength(); }
    int64 getPosition()                 { return position; }

    int read (void* buffer, int bytesToRead)
    {
        jassert (buffer != nullptr && bytesToRead >= 0);

        if (finished || isError() || bytesToRead <= 0 || fetchTask == nullptr)
            return 0;

        const auto readBytes = jmin ((uint64_t) bytesToRead, fetchTask->numBytes - 1);
        memcpy (buffer, fetchTask->data + position, readBytes);
        position += readBytes;

        return (int) readBytes;
    }

    bool setPosition (int64 wantedPos)
    {
        if (fetchTask)
        {
            if (position < fetchTask->numBytes - 1)
            {
                position = (int64) wantedPos;
                return true;
            }
        }

        return false;
    }

    int statusCode = 0;

private:
    static std::vector<const char*> getStdHeaders (const String& headers)
    {
        auto allSendHeaders = StringArray::fromLines (headers);
        allSendHeaders.removeEmptyStrings();
        std::vector<std::pair<std::string, std::string>> sendHeaders;

        std::vector<const char*> fetchHeaders;
        for (const auto& headerLine : allSendHeaders)
        {
            const auto key = headerLine.upToFirstOccurrenceOf (":", false, false);
            const auto value = headerLine.fromFirstOccurrenceOf (":", false, false);

            sendHeaders.emplace_back (key.toStdString(), value.toStdString());
        }

        for (size_t i = 0; i < sendHeaders.size(); i++)
        {
            fetchHeaders.push_back (sendHeaders[i].first.c_str());
            fetchHeaders.push_back (sendHeaders[i].second.c_str());
        }

        fetchHeaders.push_back (nullptr);
        return fetchHeaders;
    }

    void parseFetchHeaders (emscripten_fetch_t* fetch)
    {
        responseHeaders.clear();

        size_t headersLengthBytes = emscripten_fetch_get_response_headers_length (fetch) + 1;
        std::vector<char> headerString;
        headerString.resize (headersLengthBytes);
        emscripten_fetch_get_response_headers (fetch, headerString.data(), headersLengthBytes);

        char** unpacked = emscripten_fetch_unpack_response_headers (headerString.data());
        jassert (unpacked);

        int numHeaders = 0;
        for(; unpacked[numHeaders * 2]; ++numHeaders)
        {
            // Check both the header and its value are present.
            jassert(unpacked[(numHeaders * 2) + 1]);

            if (unpacked[(numHeaders * 2) + 1])
                responseHeaders.set (unpacked[numHeaders * 2], unpacked[(numHeaders * 2) + 1]);
        }

        emscripten_fetch_free_unpacked_response_headers (unpacked);
    }

    WebInputStream& owner;
    URL url;
    String headers;
    MemoryBlock postData;
    int64 position = 0;
    bool finished = false;
    const bool addParametersToRequestBody, hasBodyDataToSend;
    int timeOutMs = 0;
    int numRedirectsToFollow = 5;
    std::string httpRequestCmd;
    StringPairArray responseHeaders;
    CriticalSection createConnectionLock;
    bool hasBeenCancelled = false;

    int result = -1;
    emscripten_fetch_t* fetchTask;

    WebInputStream::Listener* listenerCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Pimpl)
};

} // namespace juce