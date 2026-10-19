// Uncaught C++ exceptions end in std::terminate -> abort(), which on Horizon
// closes the program without a word. Log the exception's type and message to
// stderr (nxlink) and sdmc:/switch/mspacman/crash.txt first.
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <typeinfo>

static void switch_terminate()
{
    const char *what = "unknown (not a std::exception)";
    const char *type = "?";
    if (std::exception_ptr ep = std::current_exception()) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception &e) {
            what = e.what();
            type = typeid(e).name();
        } catch (...) {
        }
    } else {
        what = "std::terminate() called without an active exception";
    }
    std::fprintf(stderr, "[switch] FATAL: uncaught exception [%s]: %s\n", type, what);
    if (std::FILE *f = std::fopen("/switch/mspacman/crash.txt", "a")) {
        std::fprintf(f, "FATAL: uncaught exception [%s]: %s\n", type, what);
        std::fclose(f);
    }
    std::abort();
}

static const bool s_installed = (std::set_terminate(switch_terminate), true);
