// Makes a crashing test say why, on every platform, and never open a dialog
// on Windows (a dialog would hang the CI job).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>

#if defined(_MSC_VER)
 #include <crtdbg.h>
#endif

namespace
{
    [[noreturn]] void reportTerminate() noexcept
    {
        if (const auto current = std::current_exception())
        {
            try
            {
                std::rethrow_exception (current);
            }
            catch (const std::exception& e)
            {
                std::fprintf (stderr, "\nterminate: uncaught exception: %s\n", e.what());
            }
            catch (...)
            {
                std::fprintf (stderr, "\nterminate: uncaught exception of unknown type\n");
            }
        }
        else
        {
            std::fprintf (stderr, "\nterminate called without an exception\n");
        }
        std::fflush (stderr);
        std::_Exit (3);
    }

   #if defined(_MSC_VER)
    void reportInvalidParameter (const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, std::uintptr_t)
    {
        std::fwprintf (stderr, L"\nCRT invalid parameter: %ls in %ls (%ls:%u)\n",
                       expression != nullptr ? expression : L"?", function != nullptr ? function : L"?",
                       file != nullptr ? file : L"?", line);
        std::fflush (stderr);
        std::_Exit (4);
    }
   #endif

    [[maybe_unused]] const bool installed = [] {
        std::set_terminate (reportTerminate);
       #if defined(_MSC_VER)
        _set_invalid_parameter_handler (reportInvalidParameter);
        _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        for (const int type : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
        {
            _CrtSetReportMode (type, _CRTDBG_MODE_FILE);
            _CrtSetReportFile (type, _CRTDBG_FILE_STDERR);
        }
       #endif
        return true;
    }();
}
