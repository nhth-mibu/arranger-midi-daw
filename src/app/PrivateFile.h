#pragma once

#include <juce_core/juce_core.h>

#if JUCE_MAC || JUCE_LINUX
 #include <cerrno>
 #include <cstring>
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace arranger
{

/** Writes `text` to `file` with mode 0600 from birth on macOS/Linux (atomic O_EXCL temp + rename),
    and via replaceWithText() on Windows (without a custom ACL). */
inline bool writePrivateText (const juce::File& file, const juce::String& text)
{
    file.getParentDirectory().createDirectory();
#if JUCE_MAC || JUCE_LINUX
    const auto temp = file.getSiblingFile (file.getFileName() + "." + juce::Uuid().toString() + ".tmp");
    const int fd = ::open (temp.getFullPathName().toRawUTF8(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        return false;
    ::fchmod (fd, 0600);
    const auto* utf8 = text.toRawUTF8();
    const auto len = std::strlen (utf8);
    std::size_t written = 0;
    while (written < len)
    {
        const auto n = ::write (fd, utf8 + written, len - written);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            ::close (fd);
            temp.deleteFile();
            return false;
        }
        written += static_cast<std::size_t> (n);
    }
    ::close (fd);
    if (::rename (temp.getFullPathName().toRawUTF8(), file.getFullPathName().toRawUTF8()) != 0)
    {
        temp.deleteFile();
        return false;
    }
    ::chmod (file.getFullPathName().toRawUTF8(), 0600);
    return true;
#else
    return file.replaceWithText (text);
#endif
}

} // namespace arranger
