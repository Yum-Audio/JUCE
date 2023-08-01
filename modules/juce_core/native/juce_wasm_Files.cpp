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

#include <emscripten.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/vfs.h>
#include <unistd.h>

namespace juce
{
//==============================================================================

bool File::isOnCDRomDrive() const
{
    return false;
}

bool File::isOnHardDisk() const
{
    return true;
}

bool File::isOnRemovableDrive() const
{
    return false;
}

String File::getVersion() const
{
    return {};
}

const char* const* juce_argv = nullptr;
int juce_argc = 0;

static File resolveXDGFolder (const char* const type, const char* const fallbackFolder)
{
    StringArray confLines;
    File ("~/.config/user-dirs.dirs").readLines (confLines);

    for (int i = 0; i < confLines.size(); ++i)
    {
        const String line (confLines[i].trimStart());

        if (line.startsWith (type))
        {
            // eg. resolve XDG_MUSIC_DIR="$HOME/Music" to /home/user/Music
            const File f (line.replace ("$HOME", File ("~").getFullPathName())
                              .fromFirstOccurrenceOf ("=", false, false)
                              .trim().unquoted());

            if (f.isDirectory())
                return f;
        }
    }

    return File (fallbackFolder);
}

File File::getSpecialLocation (const SpecialLocationType type)
{
    switch (type)
    {
        case userHomeDirectory:
        {
            if (const char* homeDir = getenv ("HOME"))
                return File (CharPointer_UTF8 (homeDir));

            if (auto* pw = getpwuid (getuid()))
                return File (CharPointer_UTF8 (pw->pw_dir));

            return {};
        }

        case userDocumentsDirectory:          return resolveXDGFolder ("XDG_DOCUMENTS_DIR", "~/Documents");
        case userMusicDirectory:              return resolveXDGFolder ("XDG_MUSIC_DIR",     "~/Music");
        case userMoviesDirectory:             return resolveXDGFolder ("XDG_VIDEOS_DIR",    "~/Videos");
        case userPicturesDirectory:           return resolveXDGFolder ("XDG_PICTURES_DIR",  "~/Pictures");
        case userDesktopDirectory:            return resolveXDGFolder ("XDG_DESKTOP_DIR",   "~/Desktop");
        case userApplicationDataDirectory:    return resolveXDGFolder ("XDG_CONFIG_HOME",   "~/.config");
        case commonDocumentsDirectory:
        case commonApplicationDataDirectory:  return File ("/opt");
        case globalApplicationsDirectory:     return File ("/usr");

        case tempDirectory:
        {
            if (const char* tmpDir = getenv ("TMPDIR"))
                return File (CharPointer_UTF8 (tmpDir));

            return File ("/tmp");
        }

        case invokedExecutableFile:
            if (juce_argv != nullptr && juce_argc > 0)
                return File (String (CharPointer_UTF8 (juce_argv[0])));
            // Falls through
            JUCE_FALLTHROUGH

//        case currentExecutableFile:
//        case currentApplicationFile:
//        {
//            const auto f = juce_getExecutableFile();
//            return f.isSymbolicLink() ? f.getLinkedTarget() : f;
//        }
//
//        case hostApplicationPath:
//        {
//           #if JUCE_BSD
//            return juce_getExecutableFile();
//           #else
//            const File f ("/proc/self/exe");
//            return f.isSymbolicLink() ? f.getLinkedTarget() : juce_getExecutableFile();
//           #endif
//        }

        default:
            jassertfalse; // unknown type?
            break;
    }

    return {};
}

bool Process::openDocument (const String& fileName, const String& parameters)
{
    const auto cmdString = [&]
    {
        if (fileName.startsWithIgnoreCase ("file:")
            || File::createFileWithoutCheckingPath (fileName).isDirectory())
        {
            const auto singleCommand = fileName.trim().quoted();

            StringArray cmdLines;

            for (auto browserName : { "xdg-open", "/etc/alternatives/x-www-browser", "firefox", "mozilla",
                                      "google-chrome", "chromium-browser", "opera", "konqueror" })
            {
                cmdLines.add (String (browserName) + " " + singleCommand);
            }

            return cmdLines.joinIntoString (" || ");
        }

        return (fileName.replace (" ", "\\ ", false) + " " + parameters).trim();
    }();

    MAIN_THREAD_EM_ASM({
        var elem = window.document.createElement('a');
        elem.href = UTF8ToString($0);
        elem.target = "_blank";
        document.body.appendChild(elem);
        elem.click();
        document.body.removeChild(elem);
    }, cmdString.toRawUTF8());
    return true;
}

void File::revealToUser() const
{
//    if (isDirectory())
//        startAsProcess();
//    else if (getParentDirectory().exists())
//        getParentDirectory().startAsProcess();
}

bool File::copyInternal (const File& dest) const
{
    FileInputStream in (*this);

    if (dest.deleteFile())
    {
        {
            FileOutputStream out (dest);

            if (out.failedToOpen())
                return false;

            if (out.writeFromInputStream (in, -1) == getSize())
                return true;
        }

        dest.deleteFile();
    }

    return false;
}

void File::findFileSystemRoots (Array<File>& destArray)
{
    destArray.add (File ("/"));
}

bool File::isHidden() const
{
    return getFileName().startsWithChar ('.');
}

bool File::isSymbolicLink() const
{
    return getNativeLinkedTarget().isNotEmpty();
}

String File::getNativeLinkedTarget() const
{
    constexpr int bufferSize = 8194;
    HeapBlock<char> buffer (bufferSize);
    auto numBytes = (int) readlink (getFullPathName().toRawUTF8(), buffer, bufferSize - 2);
    return String::fromUTF8 (buffer, jmax (0, numBytes));
}

int64 File::getVolumeTotalSize() const
{
    struct statfs buf;

    if (fstatfs (STDOUT_FILENO, &buf))
        return (int64) buf.f_bsize * (int64) buf.f_blocks;

    return 0;
}

int64 File::getBytesFreeOnVolume() const
{
    struct statfs buf;

    if (fstatfs (STDOUT_FILENO, &buf))
        return (int64) buf.f_bsize * (int64) buf.f_bavail; // Note: this returns space available to non-super user

    return 0;
}

//==============================================================================
#if JUCE_MAC || JUCE_IOS
 static int64 getCreationTime (const juce_statStruct& s) noexcept     { return (int64) s.st_birthtime; }
#else
 static int64 getCreationTime (const juce_statStruct& s) noexcept     { return (int64) s.st_ctime; }
#endif

void updateStatInfoForFile (const String& path, bool* isDir, int64* fileSize,
                            Time* modTime, Time* creationTime, bool* isReadOnly)
{
    if (isDir != nullptr || fileSize != nullptr || modTime != nullptr || creationTime != nullptr)
    {
        juce_statStruct info;
        const bool statOk = juce_stat (path, info);

        if (isDir != nullptr)         *isDir        = statOk && ((info.st_mode & S_IFDIR) != 0);
        if (fileSize != nullptr)      *fileSize     = statOk ? (int64) info.st_size : 0;
        if (modTime != nullptr)       *modTime      = Time (statOk ? (int64) info.st_mtime  * 1000 : 0);
        if (creationTime != nullptr)  *creationTime = Time (statOk ? getCreationTime (info) * 1000 : 0);
    }

    if (isReadOnly != nullptr)
        *isReadOnly = access (path.toUTF8(), W_OK) != 0;
}

class DirectoryIterator::NativeIterator::Pimpl
{
public:
    Pimpl (const File& directory, const String& wc)
        : parentDir (File::addTrailingSeparator (directory.getFullPathName())),
          wildCard (wc), dir (opendir (directory.getFullPathName().toUTF8()))
    {
    }

    ~Pimpl()
    {
        if (dir != nullptr)
            closedir (dir);
    }

    bool next (String& filenameFound,
               bool* const isDir, bool* const isHidden, int64* const fileSize,
               Time* const modTime, Time* const creationTime, bool* const isReadOnly)
    {
        if (dir != nullptr)
        {
            const char* wildcardUTF8 = nullptr;

            for (;;)
            {
                struct dirent* const de = readdir (dir);

                if (de == nullptr)
                    break;

                if (wildcardUTF8 == nullptr)
                    wildcardUTF8 = wildCard.toUTF8();

                if (fnmatch (wildcardUTF8, de->d_name, FNM_CASEFOLD) == 0)
                {
                    filenameFound = CharPointer_UTF8 (de->d_name);

                    updateStatInfoForFile (parentDir + filenameFound, isDir, fileSize, modTime, creationTime, isReadOnly);

                    if (isHidden != nullptr)
                        *isHidden = filenameFound.startsWithChar ('.');

                    return true;
                }
            }
        }

        return false;
    }

private:
    String parentDir, wildCard;
    DIR* dir;

    JUCE_DECLARE_NON_COPYABLE (Pimpl)
};

DirectoryIterator::NativeIterator::NativeIterator (const File& directory, const String& wildCardStr)
    : pimpl (new DirectoryIterator::NativeIterator::Pimpl (directory, wildCardStr))
{
}

DirectoryIterator::NativeIterator::~NativeIterator() {}

bool DirectoryIterator::NativeIterator::next (String& filenameFound,
                                              bool* isDir, bool* isHidden, int64* fileSize,
                                              Time* modTime, Time* creationTime, bool* isReadOnly)
{
    return pimpl->next (filenameFound, isDir, isHidden, fileSize, modTime, creationTime, isReadOnly);
}

//==============================================================================

void MemoryMappedFile::openInternal (const File&, AccessMode, bool)
{}

MemoryMappedFile::~MemoryMappedFile()
{}

} // namespace juce
