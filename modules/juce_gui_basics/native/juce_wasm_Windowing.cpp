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

extern juce::JUCEApplicationBase* juce_CreateApplication(); // (from START_JUCE_APPLICATION)

namespace juce {

extern const char* const* juce_argv;  // declared in juce_core
extern int juce_argc;

std::unique_ptr<juce::ScopedJuceInitialiser_GUI> libraryInitialiser;

class EmscriptenComponentPeer;

} // namespace juce

//==============================================================================
void launchApp(int argc, char* argv[])
{
    using namespace juce;

    juce_argc = argc;
    juce_argv = argv;

    libraryInitialiser.reset (new ScopedJuceInitialiser_GUI());

    JUCEApplicationBase::createInstance = &juce_CreateApplication;
    JUCEApplicationBase* app = JUCEApplicationBase::createInstance();
    if (! app->initialiseApp())
        exit (app->getApplicationReturnValue());

    MessageManager::getInstance();
    MainThreadEventProxy::getInstance();

    jassert (MessageManager::getInstance()->isThisTheMessageThread());
    DBG (SystemStats::getJUCEVersion());

    MessageManager::getInstance()->runDispatchLoop();
}

#include <emscripten.h>
#include <emscripten/threading.h>
#include <unordered_map>

namespace juce
{

//==============================================================================
ComponentPeer* Component::createNewPeer (int styleFlags, void*)
{
    return new EmscriptenComponentPeer(*this, styleFlags);
}

//==============================================================================
bool Desktop::canUseSemiTransparentWindows() noexcept
{
    return true;
}

double Desktop::getDefaultMasterScale()
{
    return 1;
}

Desktop::DisplayOrientation Desktop::getCurrentOrientation() const
{
    // TODO
    return upright;
}

std::map<enum MouseCursor::StandardCursorType, String> cursorNames = {
    {MouseCursor::StandardCursorType::NoCursor, String("none")},
    {MouseCursor::StandardCursorType::NormalCursor, String("default")},
    {MouseCursor::StandardCursorType::WaitCursor, String("wait")},
    {MouseCursor::StandardCursorType::IBeamCursor, String("text")},
    {MouseCursor::StandardCursorType::CrosshairCursor, String("crosshair")},
    {MouseCursor::StandardCursorType::CopyingCursor, String("copy")},
    {MouseCursor::StandardCursorType::PointingHandCursor, String("pointer")},
    {MouseCursor::StandardCursorType::DraggingHandCursor, String("move")},
    {MouseCursor::StandardCursorType::LeftRightResizeCursor, String("ew-resize")},
    {MouseCursor::StandardCursorType::UpDownResizeCursor, String("ns-resize")},
    {MouseCursor::StandardCursorType::UpDownLeftRightResizeCursor, String("nwse-resize")},
    {MouseCursor::StandardCursorType::TopEdgeResizeCursor, String("n-resize")},
    {MouseCursor::StandardCursorType::BottomEdgeResizeCursor, String("s-resize")},
    {MouseCursor::StandardCursorType::LeftEdgeResizeCursor, String("w-resize")},
    {MouseCursor::StandardCursorType::RightEdgeResizeCursor, String("e-resize")},
    {MouseCursor::StandardCursorType::TopLeftCornerResizeCursor, String("nw-resize")},
    {MouseCursor::StandardCursorType::TopRightCornerResizeCursor, String("ne-resize")},
    {MouseCursor::StandardCursorType::BottomLeftCornerResizeCursor, String("sw-resize")},
    {MouseCursor::StandardCursorType::BottomRightCornerResizeCursor, String("se-resize")}
    };


class MouseCursor::PlatformSpecificHandle
{
public:
    explicit PlatformSpecificHandle (const MouseCursor::StandardCursorType type)
        : cursorHandleType (type) {}

    explicit PlatformSpecificHandle (const CustomMouseCursorInfo& info)
        : cursorHandleType (MouseCursor::StandardCursorType::NormalCursor) {}

    // ~PlatformSpecificHandle()
    // {
    //     if (cursorHandle != Cursor{})
    //         XWindowSystem::getInstance()->deleteMouseCursor (cursorHandle);
    // }

    static void showInWindow (PlatformSpecificHandle* handle, ComponentPeer* peer)
    {
        const auto type = handle != nullptr ? handle->cursorHandleType : MouseCursor::StandardCursorType::NormalCursor;
        const char *css = cursorNames[type].toRawUTF8();
        MAIN_THREAD_EM_ASM({
            document.body.style.cursor = UTF8ToString($0);
        }, css);
    }
private:
    MouseCursor::StandardCursorType cursorHandleType;
};



bool MouseInputSource::SourceList::addSource()
{
    addSource(sources.size(), MouseInputSource::InputSourceType::mouse);
    return true;
}

bool MouseInputSource::SourceList::canUseTouch()
{
    return false;
}

Point<float> MouseInputSource::getCurrentRawMousePosition()
{
    return recentMousePosition.toFloat();
}

void MouseInputSource::setRawMousePosition (Point<float>)
{
    // not needed
}

//==============================================================================
class Desktop::NativeDarkModeChangeDetectorImpl {
    // TODO
};

std::unique_ptr<Desktop::NativeDarkModeChangeDetectorImpl> Desktop::createNativeDarkModeChangeDetectorImpl()
{
    return std::make_unique<NativeDarkModeChangeDetectorImpl>();
}

//==============================================================================
bool KeyPress::isKeyCurrentlyDown (const int keyCode)
{
    return keyDownStatus[keyCode];
}

//==============================================================================
// TODO
JUCE_API bool JUCE_CALLTYPE Process::isForegroundProcess() { return true; }
JUCE_API void JUCE_CALLTYPE Process::makeForegroundProcess() {}
JUCE_API void JUCE_CALLTYPE Process::hide() {}

//==============================================================================
void Desktop::setScreenSaverEnabled (const bool isEnabled)
{
    // TODO
}

bool Desktop::isScreenSaverEnabled()
{
    return true;
}

//==============================================================================
void Desktop::setKioskComponent (Component* kioskModeComponent, bool enableOrDisable, bool allowMenusAndBars)
{
    // TODO
}

//==============================================================================
bool juce_areThereAnyAlwaysOnTopWindows()
{
    return false;
}

//==============================================================================
void Displays::findDisplays (float masterScale)
{
    Display d;
    int width = MAIN_THREAD_EM_ASM_INT({
        return document.documentElement.clientWidth;
    });
    int height = MAIN_THREAD_EM_ASM_INT({
        return document.documentElement.scrollHeight;
    });

    d.totalArea = (Rectangle<float>(width, height) / masterScale).toNearestIntEdges();
    d.userArea = d.totalArea;
    d.isMain = true;
    d.scale = masterScale;
    d.dpi = 96;

    displays.add(d);
}

//==============================================================================
Image juce_createIconForFile (const File& file)
{
    return Image();
}

//==============================================================================
void *dummy_cursor_info = (void*) 1;
// void* CustomMouseCursorInfo::create() const                                                     { return dummy_cursor_info; }
// void* MouseCursor::createStandardMouseCursor (const MouseCursor::StandardCursorType type)            { return (void*) (int*) type; }
// void MouseCursor::deleteMouseCursor (void* const /*cursorHandle*/, const bool /*isStandard*/)   {}




//==============================================================================
void LookAndFeel::playAlertSound()
{
}

//==============================================================================
void SystemClipboard::copyTextToClipboard (const String& text)
{
    MAIN_THREAD_EM_ASM({
        if (navigator.clipboard)
        {   // async copy
            navigator.clipboard.writeText(UTF8ToString($0));
        } else
        {   // fallback
            var textArea = document.createElement("textarea");
            textArea.value = UTF8ToString($0);
            textArea.style.position = "fixed";
            document.body.appendChild(textArea);
            textArea.focus();
            textArea.select();
            document.execCommand('copy');
            document.body.removeChild(textArea);
        }
    }, text.toRawUTF8());
}

EM_JS(const char*, emscriptenGetClipboard, (), {
    if (window.clipboardUpdater == undefined)
    {
        clipboardUpdater = function(e) {
            navigator.clipboard.readText().then(function(text) {
                window.juce_clipboard = text;
            });
        };
        window.setInterval(clipboardUpdater, 200);
    }
    var data = window.juce_clipboard;
    var dataLen = lengthBytesUTF8(data) + 1;
    var dataOnWASMHeap = _malloc(dataLen);
    stringToUTF8(data, dataOnWASMHeap, dataLen);
    return dataOnWASMHeap;
});

String SystemClipboard::getTextFromClipboard()
{
    const char* data = (const char*)
        emscripten_sync_run_in_main_runtime_thread(
            EM_FUNC_SIG_I, emscriptenGetClipboard);
    String ret = String::fromUTF8(data);
    free((void*)data);
    return ret;
}


// TODO: make async
void JUCE_CALLTYPE NativeMessageBox::showMessageBoxAsync (
    AlertWindow::AlertIconType iconType,
    const String &title,
    const String &message,
    Component *associatedComponent,
    ModalComponentManager::Callback *callback)
{
    MAIN_THREAD_EM_ASM({
        alert( UTF8ToString($0) );
    }, message.toRawUTF8());

    if(callback != nullptr) callback->modalStateFinished(1);
}

bool JUCE_CALLTYPE NativeMessageBox::showOkCancelBox(
    AlertWindow::AlertIconType iconType,
    const String &title,
    const String &message,
    Component *associatedComponent,
    ModalComponentManager::Callback *callback)
{
    int result = MAIN_THREAD_EM_ASM_INT({
        return window.confirm( UTF8ToString($0) );
    }, message.toRawUTF8());
    if(callback != nullptr) callback->modalStateFinished(result?1:0);

    return result?1:0;
}

//==============================================================================
static int showDialog (const MessageBoxOptions& options,
                       ModalComponentManager::Callback* callback,
                       Async async)
{
    const auto dummyCallback = [] (int) {};

    switch (options.getNumButtons())
    {
        case 2:
        {
            if (async == Async::yes && callback == nullptr)
                callback = ModalCallbackFunction::create (dummyCallback);

            return AlertWindow::showOkCancelBox (options.getIconType(),
                                                 options.getTitle(),
                                                 options.getMessage(),
                                                 options.getButtonText (0),
                                                 options.getButtonText (1),
                                                 options.getAssociatedComponent(),
                                                 callback) ? 1 : 0;
        }

        case 3:
        {
            if (async == Async::yes && callback == nullptr)
                callback = ModalCallbackFunction::create (dummyCallback);

            return AlertWindow::showYesNoCancelBox (options.getIconType(),
                                                    options.getTitle(),
                                                    options.getMessage(),
                                                    options.getButtonText (0),
                                                    options.getButtonText (1),
                                                    options.getButtonText (2),
                                                    options.getAssociatedComponent(),
                                                    callback);
        }

        case 1:
        default:
            break;
    }

   #if JUCE_MODAL_LOOPS_PERMITTED
    if (async == Async::no)
    {
        AlertWindow::showMessageBox (options.getIconType(),
                                     options.getTitle(),
                                     options.getMessage(),
                                     options.getButtonText (0),
                                     options.getAssociatedComponent());
    }
    else
   #endif
    {
        AlertWindow::showMessageBoxAsync (options.getIconType(),
                                          options.getTitle(),
                                          options.getMessage(),
                                          options.getButtonText (0),
                                          options.getAssociatedComponent(),
                                          callback);
    }

    return 0;
}

void JUCE_CALLTYPE NativeMessageBox::showAsync (const MessageBoxOptions& options,
                                                ModalComponentManager::Callback* callback)
{
    showDialog (options, callback, Async::yes);
}

void JUCE_CALLTYPE NativeMessageBox::showAsync (const MessageBoxOptions& options,
                                                std::function<void (int)> callback)
{
    showAsync (options, ModalCallbackFunction::create (callback));
}

#if JUCE_MODAL_LOOPS_PERMITTED
void JUCE_CALLTYPE NativeMessageBox::showMessageBox (MessageBoxIconType iconType,
                                                     const String& title, const String& message,
                                                     Component* /*associatedComponent*/)
{
    AlertWindow::showMessageBox (iconType, title, message);
}

int JUCE_CALLTYPE NativeMessageBox::show (const MessageBoxOptions& options)
{
    return showDialog (options, nullptr, Async::no);
}
#endif


bool DragAndDropContainer::performExternalDragDropOfFiles (
    const StringArray& files, bool canMoveFiles, Component* sourceComp,
    std::function<void()> callback)
{
    return false;
}

bool DragAndDropContainer::performExternalDragDropOfText (
    const String& text, Component* sourceComp, std::function<void()> callback)
{
    return false;
}

}
