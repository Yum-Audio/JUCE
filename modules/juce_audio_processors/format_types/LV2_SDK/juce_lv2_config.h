/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2022 - Raw Material Software Limited

   JUCE is an open source library subject to commercial or open-source
   licensing.

   By using JUCE, you agree to the terms of both the JUCE 7 End-User License
   Agreement and JUCE Privacy Policy.

   End User License Agreement: www.juce.com/juce-7-licence
   Privacy Policy: www.juce.com/juce-privacy-policy

   Or: You may also use this code under the terms of the GPL v3 (see
   www.gnu.org/licenses).

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

/*
    This header contains preprocessor config flags that would normally be
    generated by the build systems of the various LV2 libraries.

    Rather than using the generated platform-dependent headers, we use JUCE's
    platform detection macros to pick the right config values at build time.
*/

#pragma once

#define LILV_DYN_MANIFEST
#define LILV_STATIC
#define LV2_STATIC
#define SERD_STATIC
#define SORD_STATIC
#define SRATOM_STATIC
#define ZIX_STATIC

#define LILV_VERSION "0.24.12"
#define SERD_VERSION "0.30.10"
#define SORD_VERSION "0.16.9"

#define LILV_CXX 1

#if JUCE_WINDOWS
 #define LILV_DIR_SEP "\\"
 #define LILV_PATH_SEP ";"
#else
 #define LILV_DIR_SEP "/"
 #define LILV_PATH_SEP ":"
#endif

#ifndef LILV_DEFAULT_LV2_PATH
 #if JUCE_MAC || JUCE_IOS
  #define LILV_DEFAULT_LV2_PATH \
    "~/Library/Audio/Plug-Ins/LV2"  LILV_PATH_SEP \
    "~/.lv2"                        LILV_PATH_SEP \
    "/usr/local/lib/lv2"            LILV_PATH_SEP \
    "/usr/lib/lv2"                  LILV_PATH_SEP \
    "/Library/Audio/Plug-Ins/LV2"
 #elif JUCE_WINDOWS
  #define LILV_DEFAULT_LV2_PATH \
    "%APPDATA%\\LV2"                LILV_PATH_SEP \
    "%COMMONPROGRAMFILES%\\LV2"
 #elif JUCE_LINUX || JUCE_BSD || JUCE_ANDROID || JUCE_WASM
  #define LILV_DEFAULT_LV2_PATH \
    "~/.lv2"                        LILV_PATH_SEP \
    "/usr/lib/lv2"                  LILV_PATH_SEP \
    "/usr/local/lib/lv2"
 #else
  #error "Unsupported platform"
 #endif
#endif
