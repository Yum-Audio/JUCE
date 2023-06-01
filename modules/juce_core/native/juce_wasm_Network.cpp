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

namespace juce
{

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
    }

    ~Pimpl()
    {
    }

    bool connect (WebInputStream::Listener* webInputListener, [[maybe_unused]] int numRetries = 0)
    {
        {
            const ScopedLock lock (createConnectionLock);
        }

        return false;
    }

    void cancel()
    {
        {
            const ScopedLock lock (createConnectionLock);
            hasBeenCancelled = true;
        }
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

    void withCustomRequestCommand (const String& customRequestCommand)    { httpRequestCmd = customRequestCommand; }
    void withConnectionTimeout (int timeoutInMs)                          { timeOutMs = timeoutInMs; }
    void withNumRedirectsToFollow (int maxRedirectsToFollow)              { numRedirectsToFollow = maxRedirectsToFollow; }
    StringPairArray getRequestHeaders() const                             { return WebInputStream::parseHttpHeaders (headers); }
    StringPairArray getResponseHeaders() const                            { return responseHeaders; }
    int getStatusCode() const                                             { return statusCode; }

    //==============================================================================
    bool isError() const                { return false; }//(connection == nullptr || connection->getHeaders() == nullptr); }
    int64 getTotalLength()              { return 0; }//connection == nullptr ? -1 : connection->getContentLength(); }
    bool isExhausted()                  { return finished; }
    int64 getPosition()                 { return position; }

    int read (void* buffer, int bytesToRead)
    {
        jassert (buffer != nullptr && bytesToRead >= 0);

        if (finished || isError())
            return 0;
    }

    bool setPosition (int64 wantedPos)
    {
        return true;
    }

    int statusCode = 0;

private:
    WebInputStream& owner;
    URL url;
    String headers;
    MemoryBlock postData;
    int64 position = 0;
    bool finished = false;
    const bool addParametersToRequestBody, hasBodyDataToSend;
    int timeOutMs = 0;
    int numRedirectsToFollow = 5;
    String httpRequestCmd;
    StringPairArray responseHeaders;
    CriticalSection createConnectionLock;
    bool hasBeenCancelled = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Pimpl)
};

} // namespace juce