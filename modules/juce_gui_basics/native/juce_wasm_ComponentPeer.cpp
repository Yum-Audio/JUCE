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

#include <emscripten.h>
#include <emscripten/threading.h>

namespace juce
{

class EmscriptenComponentPeer;

static Array<EmscriptenComponentPeer*> emComponentPeerList;

extern bool isMessageThreadProxied();

double getTimeSpentInCurrentDispatchCycle()
{
    static double timeDispatchBeginMS = 0;

    double currentTimeMS = Time::getMillisecondCounterHiRes();
    return (currentTimeMS - timeDispatchBeginMS) / 1000.0;
}

EM_JS(void, attachEventCallbackToWindow, (),
{
    if (window.juce_mouseCallback) return;

    // e.preventDefault stops the event being passed on to the browser window beneath

    // event name, x, y, which button, shift, ctrl, alt, wheel delta
    window.juce_mouseCallback = Module.cwrap(
        'juce_mouseCallback', 'void', ['string', 'number', 'number', 'number',
        'number', 'number', 'number', 'number']);

    window.onmousedown  = function(e) {
        e.preventDefault();
        window.juce_mouseCallback('down' ,
            e.pageX, e.pageY, e.button, e.shiftKey, e.ctrlKey, e.altKey, 0);
    };
    window.onmouseup    = function(e) {
        e.preventDefault();
        window.juce_mouseCallback('up'   ,
            e.pageX, e.pageY, e.button, e.shiftKey, e.ctrlKey, e.altKey, 0);
    };
    window.onmousewheel = function(e) {
        window.juce_mouseCallback('wheel',
            e.pageX, e.pageY, e.button, e.shiftKey, e.ctrlKey, e.altKey, e.wheelDelta);
    };
    window.onmouseenter = function(e) {
        window.juce_mouseCallback('enter',
            e.pageX, e.pageY, e.button, e.shiftKey, e.ctrlKey, e.altKey, 0);
    };
    window.onmouseleave = function(e) {
        window.juce_mouseCallback('leave',
            e.pageX, e.pageY, e.button, e.shiftKey, e.ctrlKey, e.altKey, 0);
    };
    window.onmousemove  = function(e) {
        window.juce_mouseCallback('move' ,
            e.pageX, e.pageY, e.button, e.shiftKey, e.ctrlKey, e.altKey, 0);
    };

    // window.onmouseout   = function(e) {
    //     window.juce_mouseCallback('out'  , e.pageX, e.pageY, e.which); };
    // window.onmouseover  = function(e) {
    //     window.juce_mouseCallback('over' , e.pageX, e.pageY, e.which); };

    // event name, key code, key
    window.juce_keyboardCallback = Module.cwrap(
        'juce_keyboardCallback', 'void', ['string', 'number', 'string']);

    // component peer pointer, event name, data
    window.juce_inputCallback = Module.cwrap(
        'juce_inputCallback', 'void', ['number', 'string', 'string']);

    window.addEventListener('keydown', function(e) {
        e.preventDefault();
        window.juce_keyboardCallback ('down', e.which || e.keyCode, e.key);
    });
    window.addEventListener('keyup', function(e) {
        window.juce_keyboardCallback ('up', e.which || e.keyCode, e.key);
    });

    window.juce_clipboard = "";

    window.addEventListener('copy', function(e) {
        navigator.clipboard.readText().then(function(text) {
            window.juce_clipboard = text;
        });
    });
});

class EmscriptenComponentPeer : public ComponentPeer,
                                public MessageListener
{
    Rectangle<int> bounds;
    String id;
    static int highestZIndex;
    int zIndex{0};
    bool focused{false};
    bool visibility{true};
    bool repaintMessagePosted{false};
    double desiredFPS{120.0};

    RectangleList<int> unfinishedRepaintAreas;
    RectangleList<int> pendingRepaintAreas;

    struct RepaintMessage : public Message {};

    public:
        EmscriptenComponentPeer(Component &component, int styleFlags)
        :ComponentPeer(component, styleFlags)
        {
            emComponentPeerList.add(this);
            DBG("EmscriptenComponentPeer");

            id = Uuid().toDashedString();

            DBG("id is " << id);

            emscripten_sync_run_in_main_runtime_thread(EM_FUNC_SIG_V,
                attachEventCallbackToWindow);

            MAIN_THREAD_EM_ASM_INT({
                var canvas = document.createElement('canvas');
                canvas.id  = UTF8ToString($0);
                canvas.style.zIndex   = $6;
                canvas.style.position = "absolute";
                canvas.style.left = $1;
                canvas.style.top  = $2;
                canvas.width  = $3;
                canvas.height = $4;
                canvas.oncontextmenu = function(e) { e.preventDefault(); };
                canvas.setAttribute('data-peer', $5);
                canvas.addEventListener ('wheel', function(e) {
                    event.preventDefault();
                }, true);
                canvas._duringInput = false;
                canvas._inputProxy = document.createElement('input');
                canvas._inputProxy.type = 'text';
                canvas._inputProxy.style.position = 'absolute';
                canvas._inputProxy.style.opacity = 0;
                canvas._inputProxy.style.zIndex = 0;
                canvas._inputProxy.addEventListener ('compositionstart', function (e)
                {
                    window.juce_inputCallback($5, e.type, e.data);
                });
                canvas._inputProxy.addEventListener ('compositionupdate', function (e)
                {
                    window.juce_inputCallback($5, e.type, e.data);
                });
                canvas._inputProxy.addEventListener ('compositionend', function (e)
                {
                    window.juce_inputCallback($5, e.type, e.data);
                    canvas._inputProxy.value = "";
                });
                canvas._inputProxy.addEventListener ('focus', function (e)
                {
                    if (! canvas._duringInput) canvas.focus();
                });
                canvas._inputProxy.addEventListener ('focusout', function (e)
                {
                    if (canvas._duringInput)
                        canvas._inputProxy.focus();
                });
                document.body.appendChild(canvas);
                document.body.appendChild(canvas._inputProxy);
            }, id.toRawUTF8(), bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), this, ++highestZIndex);

            zIndex = highestZIndex;

            grabFocus();

            //if (isFocused())
            //    handleFocusGain();
        }

        ~EmscriptenComponentPeer()
        {
            emComponentPeerList.removeAllInstancesOf(this);
            MAIN_THREAD_EM_ASM({
                var canvas = document.getElementById(UTF8ToString($0));
                canvas.parentElement.removeChild(canvas._inputProxy);
                canvas.parentElement.removeChild(canvas);
            }, id.toRawUTF8());
        }

        int getZIndex () const { return zIndex; }

        String getId() const { return id; }

        struct ZIndexComparator
        {
            int compareElements (const EmscriptenComponentPeer* first,
                                 const EmscriptenComponentPeer* second) {
                if(first -> getZIndex() < second -> getZIndex()) return -1;
                if(first -> getZIndex() > second -> getZIndex()) return 1;
                return 0;
            }
        };

        virtual void* getNativeHandle () const override
        {
            return (void*)this;
        }

        virtual void setVisible (bool shouldBeVisible) override
        {
            if (visibility == shouldBeVisible) return;

            MAIN_THREAD_EM_ASM({
                var canvas = document.getElementById(UTF8ToString($0));
                canvas.style.visibility = $1 ? "visible" : "hidden";
            }, id.toRawUTF8(), shouldBeVisible);

            visibility = shouldBeVisible;
        }

        bool isVisible () const { return visibility; }

        virtual void setTitle (const String &title) override
        {
            DBG("setTitle: " << title);
        }

        virtual void setBounds (const Rectangle< int > &newBounds, bool isNowFullScreen) override
        {
            DBG("setBounds " << newBounds.toString());

            auto oldBounds = bounds;
            bounds = newBounds;
            fullscreen = isNowFullScreen;

            MAIN_THREAD_EM_ASM({
                var canvas = document.getElementById (UTF8ToString($0));
                canvas.style.left = $1 + 'px';
                canvas.style.top  = $2 + 'px';
                if (canvas.width != $3 || canvas.height != $4)
                {
                    var canvasWasNotEmpty = canvas.width > 0 && canvas.height > 0;
                    if (canvasWasNotEmpty)
                    {
                        var copyCanvas = document.createElement ('canvas');
                        var copyCtx = copyCanvas.getContext ('2d');
                        copyCanvas.width = canvas.width;
                        copyCanvas.height = canvas.height;
                        copyCtx.drawImage (canvas, 0, 0);
                    }
                    canvas.width  = $3;
                    canvas.height = $4;
                    if (canvasWasNotEmpty)
                    {
                        var ctx = canvas.getContext ('2d');
                        ctx.drawImage (copyCanvas, 0, 0);
                    }
                }
            }, id.toRawUTF8(), bounds.getX(), bounds.getY(),
            bounds.getWidth(), bounds.getHeight());

            handleMovedOrResized();

            if (! newBounds.isEmpty() &&
                newBounds.withZeroOrigin() != oldBounds.withZeroOrigin())
            {
                repaint(newBounds.withZeroOrigin());
            }
        }

        virtual Rectangle<int> getBounds () const override
        {
            return bounds;
        }

        virtual Point<float>  localToGlobal (Point< float > relativePosition) override
        {
            return relativePosition + bounds.getPosition().toFloat();
        }

        virtual Point<float>  globalToLocal (Point< float > screenPosition) override
        {
            return screenPosition - bounds.getPosition().toFloat();
        }

        virtual void setMinimised (bool shouldBeMinimised) override
        {

        }

        virtual bool isMinimised () const override
        {
            return false;
        }

        bool fullscreen=false;
        virtual void setFullScreen (bool shouldBeFullScreen) override
        {
            MAIN_THREAD_EM_ASM({
                var canvas = document.getElementById(UTF8ToString($0));
                canvas.style.left='0px';
                canvas.style.top ='0px';

                canvas.width=window.innerWidth;
                canvas.height=window.innerHeight;
            }, id.toRawUTF8());

            bounds = bounds.withZeroOrigin();

            bounds.setWidth ( MAIN_THREAD_EM_ASM_INT({ return window.innerWidth; }, 0) );
            bounds.setHeight( MAIN_THREAD_EM_ASM_INT({ return window.innerHeight; }, 0) );

            this->setBounds(bounds, true);
        }

        virtual bool isFullScreen () const override
        {
            return fullscreen;
        }

        virtual void setIcon (const Image &newIcon) override
        {

        }

        virtual bool contains (Point< int > localPos, bool trueIfInAChildWindow) const override
        {
            Point<int> globalPos = localPos+bounds.getPosition();
            return bounds.contains(globalPos);
        }

        OptionalBorderSize getFrameSizeIfPresent() const override
        {
            return OptionalBorderSize {getFrameSize()};
        }

        virtual BorderSize< int >   getFrameSize () const override
        {
            return BorderSize<int>();
        }

        virtual bool setAlwaysOnTop (bool alwaysOnTop) override
        {
            return false;
        }

        virtual void toFront (bool makeActive) override
        {
            if (zIndex == highestZIndex) return;
            DBG("toFront " << id << " " << (makeActive ? "true" : "false"));

            highestZIndex = MAIN_THREAD_EM_ASM_INT({
                var canvas = document.getElementById(UTF8ToString($0));
                canvas.style.zIndex = parseInt($1)+1;
                return parseInt(canvas.style.zIndex);
            }, id.toRawUTF8(), highestZIndex);

            zIndex = highestZIndex;

            handleBroughtToFront();

            if (makeActive)
            {
                grabFocus();
            }
        }

        void updateZIndex()
        {
            zIndex = MAIN_THREAD_EM_ASM_INT({
                var canvas = document.getElementById(UTF8ToString($0));
                return canvas.zIndex;
            }, id.toRawUTF8());
        }

        virtual void toBehind (ComponentPeer *other) override
        {
            DBG("toBehind");

            if(EmscriptenComponentPeer* otherPeer = dynamic_cast<EmscriptenComponentPeer*>(other))
            {
                int newZIndex = MAIN_THREAD_EM_ASM_INT({
                    var canvas = document.getElementById(UTF8ToString($0));
                    var other  = document.getElementById(UTF8ToString($1));
                    canvas.zIndex = parseInt(other.zIndex)-1;
                    return parseInt(other.zIndex);
                }, id.toRawUTF8(), otherPeer->id.toRawUTF8());

                highestZIndex = std::max(highestZIndex, newZIndex);

                updateZIndex();
                otherPeer->updateZIndex();
                if (! otherPeer->isFocused())
                {
                    otherPeer->focused = true;
                    otherPeer->handleFocusGain();
                }
            }

            if (focused)
            {
                focused = false;
                handleFocusLoss();
            }
        }

        virtual bool isFocused() const override
        {
            return focused;
        }

        virtual void grabFocus() override
        {
            DBG("grabFocus " << id);
            if (! focused)
            {
                for (auto* other : emComponentPeerList)
                {
                    if (other != this && other -> isFocused())
                    {
                        other -> focused = false;
                        other -> handleFocusLoss();
                    }
                }
                focused = true;
                handleFocusGain();
            }
        }

        virtual void textInputRequired(Point< int > position, TextInputTarget &) override
        {
            MAIN_THREAD_EM_ASM({
                var canvas = document.getElementById(UTF8ToString($0));
                var left = parseInt(canvas.style.left, 10);
                var top = parseInt(canvas.style.top, 10);
                canvas._duringInput = true;
                canvas._inputProxy.style.left = (left + $1) + 'px';
                canvas._inputProxy.style.top = (top + $2) + 'px';
                canvas._inputProxy.focus();
            }, id.toRawUTF8(), position.x, position.y);
        }

        virtual void dismissPendingTextInput() override
        {
            MAIN_THREAD_EM_ASM({
                var canvas = document.getElementById(UTF8ToString($0));
                canvas._duringInput = false;
            }, id.toRawUTF8());
        }

        virtual void repaint (const Rectangle<int>& area) override
        {
            pendingRepaintAreas.add(area);
            if (! repaintMessagePosted)
            {
                postMessage (new RepaintMessage());
                repaintMessagePosted = true;
            }
        }

        virtual void performAnyPendingRepaintsNow() override
        {
            DBG("performAnyPendingRepaintsNow");
        }

        virtual void setAlpha (float newAlpha) override
        {
            DBG("setAlpha");
        }

        virtual StringArray getAvailableRenderingEngines() override
        {
            return StringArray();
        }

    private:

        void handleMessage (const Message& msg) override
        {
            if (dynamic_cast<const RepaintMessage*>(& msg))
            {
                // First finish remaining repaints from the last interrupted
                //   message cycle. This is to prevent a repaint area from being
                //   indefinitely postponed through multiple message cycles.
                for (int i = 0; i < unfinishedRepaintAreas.getNumRectangles(); i ++)
                {
                    Rectangle<int> area = unfinishedRepaintAreas.getRectangle(i);
                    internalRepaint (area);
                    pendingRepaintAreas.subtract (area);
                    if (getTimeSpentInCurrentDispatchCycle() > 1.0 / desiredFPS)
                    {
                        RectangleList<int> remaining;
                        for (int j = i + 1; j < unfinishedRepaintAreas.getNumRectangles(); j ++)
                        {
                            remaining.addWithoutMerging(
                                unfinishedRepaintAreas.getRectangle(j));
                        }
                        unfinishedRepaintAreas = remaining;
                        postMessage (new RepaintMessage());
                        return;
                    }
                }

                unfinishedRepaintAreas.clear();

                for (int i = 0; i < pendingRepaintAreas.getNumRectangles(); i ++)
                {
                    Rectangle<int> area = pendingRepaintAreas.getRectangle(i);
                    internalRepaint (area);

                    // Do not interrupt repaints if the message thread is different
                    //   from the main thread since the main loop is no longer blocked
                    //   by the message loop.
                    if (isMessageThreadProxied()) continue;

                    if (getTimeSpentInCurrentDispatchCycle() > 1.0 / desiredFPS)
                    {
                        for (int j = i + 1; j < pendingRepaintAreas.getNumRectangles(); j ++)
                        {
                            unfinishedRepaintAreas.addWithoutMerging(
                                pendingRepaintAreas.getRectangle(j));
                        }
                        pendingRepaintAreas.clear();
                        repaintMessagePosted = true;
                        postMessage (new RepaintMessage());
                        return;
                    }
                }
                repaintMessagePosted = false;
                pendingRepaintAreas.clear();
            }
        }

        void internalRepaint (const Rectangle<int> &area)
        {
            // DBG("repaint: " << area.toString());

            Image temp(Image::ARGB, area.getWidth(), area.getHeight(), true);
            LowLevelGraphicsSoftwareRenderer g(temp);
            g.setOrigin (-area.getPosition());
            handlePaint (g);

            Image::BitmapData bitmapData (temp, Image::BitmapData::readOnly);
            uint8* pixels = bitmapData.getPixelPointer(0, 0);
            int dataSize = bitmapData.width * bitmapData.height * 4;
            for (int i = 0; i < dataSize; i += 4)
                std::swap (pixels[i], pixels[i + 2]);

            MAIN_THREAD_EM_ASM({
                var id = UTF8ToString($0);
                var pointer = $1;
                var width   = $2;
                var height  = $3;

                var canvas = document.getElementById(id);
                var ctx = canvas.getContext("2d");

                var buffer = new Uint8ClampedArray(
                    Module.HEAPU8.buffer, pointer, width * height * 4);
                var imageData = ctx.createImageData(width, height);
                imageData.data.set(buffer);
                ctx.putImageData(imageData, $4, $5);
                // delete buffer;
            }, id.toRawUTF8(),
               pixels, bitmapData.width, bitmapData.height,
               area.getX(), area.getY());
        }
};

int EmscriptenComponentPeer::highestZIndex = 10;

}
