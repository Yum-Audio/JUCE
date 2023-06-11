/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2013 - Raw Material Software Ltd.

   Permission is granted to use this software under the terms of either:
   a) the GPL v2 (or any later version)
   b) the Affero GPL v3

   Details of these licenses can be found at: www.gnu.org/licenses

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

   ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.juce.com for more information.

  ==============================================================================
*/

#include <emscripten.h>

namespace juce
{

class InternalMessageQueue
{
public:
    ~InternalMessageQueue()
    {
        clearSingletonInstance();
    }

    //==============================================================================
    void postMessage (MessageManager::MessageBase* const msg) noexcept
    {
        const ScopedLock sl { lock };
        queue.add (msg);
    }

    void dispatchPendingMessages()
    {
        while (auto msg = popNextMessage())
        {
            JUCE_TRY
            {
                msg->messageCallback();
            }
            JUCE_CATCH_EXCEPTION
        }
    }

    //==============================================================================
    JUCE_DECLARE_SINGLETON (InternalMessageQueue, false)

private:
    CriticalSection lock;
    ReferenceCountedArray <juce::MessageManager::MessageBase> queue;

    juce::MessageManager::MessageBase::Ptr popNextMessage() noexcept
    {
        const ScopedLock sl { lock };
        return queue.removeAndReturn (0);
    }
};

JUCE_IMPLEMENT_SINGLETON (InternalMessageQueue)

static bool appIsInsideEmrun{false};

static Thread::ThreadID messageThreadID = nullptr; // JUCE message thread
static Thread::ThreadID mainThreadID = nullptr;    // Javascript main thread

CriticalSection mainThreadLoopFuncsLock;
std::vector<std::function<void()>> mainThreadLoopFuncs;

extern bool isMessageThreadProxied()
{
    return messageThreadID != mainThreadID;
}

extern void registerCallbackToMainThread (std::function<void()> f)
{
    const ScopedLock lk (mainThreadLoopFuncsLock);
    mainThreadLoopFuncs.push_back (f);
}


void MessageManager::doPlatformSpecificInitialisation()
{
    InternalMessageQueue::getInstance();

    const auto createDirIfNotExists = [](File::SpecialLocationType type)
    {
        const auto dir = File::getSpecialLocation(type);
        if (! dir.exists())
            dir.createDirectory();
    };

    createDirIfNotExists(File::userHomeDirectory);
    createDirIfNotExists(File::userDocumentsDirectory);
    createDirIfNotExists(File::userMusicDirectory);
    createDirIfNotExists(File::userMoviesDirectory);
    createDirIfNotExists(File::userPicturesDirectory);
    createDirIfNotExists(File::userDesktopDirectory);
    createDirIfNotExists(File::userApplicationDataDirectory);
    createDirIfNotExists(File::commonDocumentsDirectory);
    createDirIfNotExists(File::commonApplicationDataDirectory);
    createDirIfNotExists(File::globalApplicationsDirectory);
    createDirIfNotExists(File::tempDirectory);

    messageThreadID = Thread::getCurrentThreadId();

    appIsInsideEmrun = MAIN_THREAD_EM_ASM_INT({
        return document.title == "Emscripten-Generated Code";
    });

    MAIN_THREAD_EM_ASM({
        if (window.juce_animationFrameCallback)
            return;

        window.juce_animationFrameCallback = Module.cwrap (
            'juce_animationFrameCallback', 'int', ['number']);

        if (window.juce_animationFrameCallback (-1.0) == 1)
        {
            window.juce_animationFrameWrapper = function (timestamp)
            {
                window.juce_animationFrameCallback (timestamp);
                window.requestAnimationFrame (window.juce_animationFrameWrapper);
            };

            window.requestAnimationFrame (window.juce_animationFrameWrapper);
        }
    });
}

void MessageManager::doPlatformSpecificShutdown()
{
    emscripten_cancel_main_loop();
    InternalMessageQueue::deleteInstance();
}

// If timestamp < 0, this callback tests if the calling thread (main thread) is
//   different from the message thread and return the result.
// If timestamp >= 0, it always returns 0.
extern "C" int juce_animationFrameCallback (double timestamp)
{
    if (timestamp < 0)
    {
        mainThreadID = Thread::getCurrentThreadId();
        return mainThreadID != messageThreadID;
    }

    const ScopedLock lk (mainThreadLoopFuncsLock);
    for (auto f : mainThreadLoopFuncs)
        f();

    return 0;
}

static void dispatchLoop()
{
    const auto* mm = MessageManager::getInstanceWithoutCreating();

    if (mm == nullptr)
        return;

    if (mm->hasStopMessageBeenSent())
    {
        emscripten_cancel_main_loop();
        return;
    }

    if (auto* queue = InternalMessageQueue::getInstanceWithoutCreating())
        queue->dispatchPendingMessages();
}

bool MessageManager::postMessageToSystemQueue (MessageManager::MessageBase* const message)
{
    if (auto* queue = InternalMessageQueue::getInstanceWithoutCreating())
    {
        queue->postMessage (message);
        return true;
    }

    return false;
}

void MessageManager::broadcastMessage (const String&)
{
}

void MessageManager::runDispatchLoop()
{
    emscripten_set_main_loop(dispatchLoop, 0, 0);
}

class MessageManager::QuitMessage   : public MessageManager::MessageBase
{
public:
    QuitMessage() {}

    void messageCallback() override
    {
        if (auto* mm = MessageManager::instance)
            mm->quitMessageReceived = true;
    }

    JUCE_DECLARE_NON_COPYABLE (QuitMessage)
};

void MessageManager::stopDispatchLoop()
{
    (new QuitMessage())->post();
    quitMessagePosted = true;
}

}
