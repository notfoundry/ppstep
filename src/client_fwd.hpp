#ifndef PPSTEP_CLIENT_FWD_HPP
#define PPSTEP_CLIENT_FWD_HPP

#include <exception>

namespace ppstep {
    template <class TokenT, class ContainerT>
    struct client;

    enum class break_condition {
        INVALID = 0,
        CALL = 1 << 0,
        EXPANDED = 1 << 1,
        RESCANNED = 1 << 2,
        LEXED = 1 << 3
    };

    enum class stepping_mode {
        INVALID = 0,
        FREE = 1 << 0,
        UNTIL_BREAK = 1 << 1
    };

    struct session_terminate : std::exception {
        using std::exception::exception;
    };
}

#endif // PPSTEP_CLIENT_FWD_HPP