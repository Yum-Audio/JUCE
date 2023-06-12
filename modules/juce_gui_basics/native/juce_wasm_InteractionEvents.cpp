/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2023 - Raw Material Software Ltd.

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

namespace juce
{

// A singleton class that accepts mouse and keyboard events from browser
//   main thread and post them as messages onto the message thread.
// It is useful when main thread != message thread (-s PROXY_TO_PTHREAD).
class MainThreadEventProxy : public MessageListener
{
public:
    struct MouseEvent : public Message
    {
        String type;
        int x, y;
        int which;
        bool isShiftDown, isCtrlDown, isAltDown;
        int wheelDelta;
    };

    struct KeyboardEvent : public Message
    {
        String type;
        int keyCode;
        String key;
    };

    struct InputEvent : public Message
    {
        EmscriptenComponentPeer* target;
        String type;
        String data;
    };

    static MainThreadEventProxy& getInstance()
    {
        static MainThreadEventProxy thisProxy;
        return thisProxy;
    }

private:
    void handleMessage (const Message& msg) override
    {
        if (auto* e = dynamic_cast<const MouseEvent*>(& msg))
            handleMouseEvent (*e);
        else if (auto* e = dynamic_cast<const KeyboardEvent*>(& msg))
            handleKeyboardEvent (*e);
        else if (auto* e = dynamic_cast<const InputEvent*>(& msg))
            handleInputEvent (*e);
    }

    void handleMouseEvent (const MouseEvent& e);
    void handleKeyboardEvent (const KeyboardEvent& e);
    void handleInputEvent (const InputEvent& e);
};

extern "C" void juce_mouseCallback(const char* type, int x, int y, int which,
    int isShiftDown, int isCtrlDown, int isAltDown, int wheelDelta)
{
//    DBG(type << " " << x << " " << y << " " << which
//             << " " << isShiftDown << " " << wheelDelta);
    auto* e = new MainThreadEventProxy::MouseEvent();
    e->type = String(type);
    e->x = x;
    e->y = y;
    e->which = which;
    e->isShiftDown = isShiftDown;
    e->isCtrlDown = isCtrlDown;
    e->isAltDown = isAltDown;
    e->wheelDelta = wheelDelta;
    MainThreadEventProxy::getInstance().postMessage(e);
}

extern "C" void juce_keyboardCallback(const char* type, int keyCode, const char * key)
{
    DBG("key " << type << " " << keyCode << " " << key);
    auto* e = new MainThreadEventProxy::KeyboardEvent();
    e->type = String(type);
    e->keyCode = keyCode;
    e->key = String(key);
    MainThreadEventProxy::getInstance().postMessage(e);
}

extern "C" void juce_inputCallback(void* componentPeer,
    const char* type, const char* data)
{
    auto* e = new MainThreadEventProxy::InputEvent();
    e->target = static_cast<EmscriptenComponentPeer*>(componentPeer);
    e->type = String(CharPointer_UTF8(type));
    e->data = String(CharPointer_UTF8(data));
    MainThreadEventProxy::getInstance().postMessage(e);
}

static Point<int> recentMousePosition;

static int64 fakeMouseEventTime = 0;
static std::unordered_map<int, bool> keyDownStatus;

void MainThreadEventProxy::handleMouseEvent (const MouseEvent& e)
{
    recentMousePosition = {e.x, e.y};
    bool isDownEvent = e.type == "down";
    bool isUpEvent = e.type == "up";

    ModifierKeys& mods = ModifierKeys::currentModifiers;

    if (isDownEvent)
    {
        mods = mods.withoutMouseButtons();

        if (e.which == 0 || e.which > 2)
            mods = mods.withFlags(ModifierKeys::leftButtonModifier);
        else if (e.which == 1)
            mods = mods.withFlags(ModifierKeys::middleButtonModifier);
        else if (e.which == 2)
            mods = mods.withFlags(ModifierKeys::rightButtonModifier);
    }
    else if (isUpEvent)
    {
        mods = mods.withoutMouseButtons();
    }

    mods = e.isShiftDown ? mods.withFlags(ModifierKeys::shiftModifier)
                         : mods.withoutFlags(ModifierKeys::shiftModifier);
    mods = e.isCtrlDown  ? mods.withFlags(ModifierKeys::ctrlModifier)
                         : mods.withoutFlags(ModifierKeys::ctrlModifier);
    mods = e.isAltDown   ? mods.withFlags(ModifierKeys::altModifier)
                         : mods.withoutFlags(ModifierKeys::altModifier);

    EmscriptenComponentPeer::ZIndexComparator comparator;
    emComponentPeerList.sort(comparator);
    Point<int> posGlobal(e.x, e.y);

    for (int i = emComponentPeerList.size() - 1; i >= 0; i --)
    {
        EmscriptenComponentPeer* peer = emComponentPeerList[i];

        if (! peer -> isVisible())
            continue;

        bool isPosInPeerBounds = peer -> getBounds().contains(posGlobal);
        Point<float> pos = peer->globalToLocal(posGlobal.toFloat());

        if (isDownEvent && ! isPosInPeerBounds)
            continue;

        if (e.wheelDelta == 0)
        {
            peer->handleMouseEvent(MouseInputSource::InputSourceType::mouse,
                pos, mods, MouseInputSource::defaultPressure, 0.0f, fakeMouseEventTime);
        }
        else
        {
            MouseWheelDetails wheelInfo;
            wheelInfo.deltaX = 0.0f;
            wheelInfo.deltaY = e.wheelDelta / 480.0f;
            wheelInfo.isReversed = false;
            wheelInfo.isSmooth = false;
            wheelInfo.isInertial = false;
            peer->handleMouseWheel(MouseInputSource::InputSourceType::mouse,
                pos, fakeMouseEventTime, wheelInfo);
        }

        if (isPosInPeerBounds)
            break; // consume the event
    }
    fakeMouseEventTime ++;
}

void MainThreadEventProxy::handleKeyboardEvent (const KeyboardEvent& e)
{
    bool isChar = e.key.length() == 1;
    bool isDown = e.type == "down";
    juce_wchar keyChar = isChar ? (juce_wchar)e.key[0] : 0;
    int keyCode = e.keyCode;

    ModifierKeys& mods = ModifierKeys::currentModifiers;
    auto changedModifier = ModifierKeys::noModifiers;

    if (keyCode == 16)
        changedModifier = ModifierKeys::shiftModifier;
    else if (keyCode == 17)
        changedModifier = ModifierKeys::ctrlModifier;
    else if (keyCode == 18)
        changedModifier = ModifierKeys::altModifier;
    else if (keyCode == 91)
        changedModifier = ModifierKeys::commandModifier;

    if (changedModifier != ModifierKeys::noModifiers)
        mods = isDown ? mods.withFlags(changedModifier)
                      : mods.withoutFlags(changedModifier);

    if ((keyChar >= 'a' && keyChar <= 'z') ||
        (keyChar >= 'A' && keyChar <= 'Z'))
        keyCode = keyChar;

    keyDownStatus[keyCode] = isDown;

    for (int i = emComponentPeerList.size() - 1; i >= 0; i --)
    {
        EmscriptenComponentPeer* peer = emComponentPeerList[i];

        if (! peer->isVisible() || ! peer->isFocused())
            continue;

        if (changedModifier != ModifierKeys::noModifiers)
            peer->handleModifierKeysChange();

        peer->handleKeyUpOrDown (isDown);

        if (isDown)
            peer->handleKeyPress (KeyPress (keyCode, mods, keyChar));
    }
}

void MainThreadEventProxy::handleInputEvent (const InputEvent& e)
{
    TextInputTarget* input = e.target->findCurrentTextInputTarget();

    if (e.type == "compositionstart" || e.type == "compositionupdate")
    {
        Component* inputComponent = dynamic_cast<Component*>(input);
        if (inputComponent)
        {
            auto bounds = inputComponent->getScreenBounds();
            auto caret = input->getCaretRectangle();
            int x = bounds.getX() + caret.getX();
            int y = bounds.getY() + caret.getY();
            MAIN_THREAD_EM_ASM({
                var canvas = document.getElementById(UTF8ToString($0));
                canvas._duringInput = true;
                canvas._inputProxy.style.left = $1 + 'px';
                canvas._inputProxy.style.top = $2 + 'px';
                canvas._inputProxy.focus();
            }, e.target->getId().toRawUTF8(), x, y);
        }
    }
    else if (e.type == "compositionend" && e.data.length() > 0)
    {
        input->insertTextAtCaret (e.data);
    }

}

//==============================================================================

const int extendedKeyModifier       = 0x10000;

const int KeyPress::spaceKey                = 32;
const int KeyPress::returnKey               = 13;
const int KeyPress::escapeKey               = 27;
const int KeyPress::backspaceKey            = 8;
const int KeyPress::leftKey                 = 37;
const int KeyPress::rightKey                = 39;
const int KeyPress::upKey                   = 38;
const int KeyPress::downKey                 = 40;
const int KeyPress::pageUpKey               = 33;
const int KeyPress::pageDownKey             = 34;
const int KeyPress::endKey                  = 35;
const int KeyPress::homeKey                 = 36;
const int KeyPress::deleteKey               = 46;
const int KeyPress::insertKey               = 45;
const int KeyPress::tabKey                  = 9;
const int KeyPress::F1Key                   = 112;
const int KeyPress::F2Key                   = 113;
const int KeyPress::F3Key                   = 114;
const int KeyPress::F4Key                   = 115;
const int KeyPress::F5Key                   = 116;
const int KeyPress::F6Key                   = 117;
const int KeyPress::F7Key                   = 118;
const int KeyPress::F8Key                   = 119;
const int KeyPress::F9Key                   = 120;
const int KeyPress::F10Key                  = 121;
const int KeyPress::F11Key                  = 122;
const int KeyPress::F12Key                  = 123;
const int KeyPress::F13Key                  = extendedKeyModifier | 24;
const int KeyPress::F14Key                  = extendedKeyModifier | 25;
const int KeyPress::F15Key                  = extendedKeyModifier | 26;
const int KeyPress::F16Key                  = extendedKeyModifier | 27;
const int KeyPress::F17Key                  = extendedKeyModifier | 28;
const int KeyPress::F18Key                  = extendedKeyModifier | 29;
const int KeyPress::F19Key                  = extendedKeyModifier | 30;
const int KeyPress::F20Key                  = extendedKeyModifier | 31;
const int KeyPress::F21Key                  = extendedKeyModifier | 32;
const int KeyPress::F22Key                  = extendedKeyModifier | 33;
const int KeyPress::F23Key                  = extendedKeyModifier | 34;
const int KeyPress::F24Key                  = extendedKeyModifier | 35;
const int KeyPress::F25Key                  = extendedKeyModifier | 36;
const int KeyPress::F26Key                  = extendedKeyModifier | 37;
const int KeyPress::F27Key                  = extendedKeyModifier | 38;
const int KeyPress::F28Key                  = extendedKeyModifier | 39;
const int KeyPress::F29Key                  = extendedKeyModifier | 40;
const int KeyPress::F30Key                  = extendedKeyModifier | 41;
const int KeyPress::F31Key                  = extendedKeyModifier | 42;
const int KeyPress::F32Key                  = extendedKeyModifier | 43;
const int KeyPress::F33Key                  = extendedKeyModifier | 44;
const int KeyPress::F34Key                  = extendedKeyModifier | 45;
const int KeyPress::F35Key                  = extendedKeyModifier | 46;
const int KeyPress::numberPad0              = extendedKeyModifier | 27;
const int KeyPress::numberPad1              = extendedKeyModifier | 28;
const int KeyPress::numberPad2              = extendedKeyModifier | 29;
const int KeyPress::numberPad3              = extendedKeyModifier | 30;
const int KeyPress::numberPad4              = extendedKeyModifier | 31;
const int KeyPress::numberPad5              = extendedKeyModifier | 32;
const int KeyPress::numberPad6              = extendedKeyModifier | 33;
const int KeyPress::numberPad7              = extendedKeyModifier | 34;
const int KeyPress::numberPad8              = extendedKeyModifier | 35;
const int KeyPress::numberPad9              = extendedKeyModifier | 36;
const int KeyPress::numberPadAdd            = extendedKeyModifier | 37;
const int KeyPress::numberPadSubtract       = extendedKeyModifier | 38;
const int KeyPress::numberPadMultiply       = extendedKeyModifier | 39;
const int KeyPress::numberPadDivide         = extendedKeyModifier | 40;
const int KeyPress::numberPadSeparator      = extendedKeyModifier | 41;
const int KeyPress::numberPadDecimalPoint   = extendedKeyModifier | 42;
const int KeyPress::numberPadEquals         = extendedKeyModifier | 43;
const int KeyPress::numberPadDelete         = extendedKeyModifier | 44;
const int KeyPress::playKey                 = extendedKeyModifier | 45;
const int KeyPress::stopKey                 = extendedKeyModifier | 46;
const int KeyPress::fastForwardKey          = extendedKeyModifier | 47;
const int KeyPress::rewindKey               = extendedKeyModifier | 48;

}
