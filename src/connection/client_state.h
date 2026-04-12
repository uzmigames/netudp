#ifndef NETUDP_CLIENT_STATE_H
#define NETUDP_CLIENT_STATE_H

/**
 * @file client_state.h
 * @brief Client connection state machine (spec 05 REQ-05.5).
 */

#include <cstdint>

namespace netudp {

enum class ClientState : int {
    TOKEN_EXPIRED       = -6,
    INVALID_TOKEN       = -5,
    CONNECTION_TIMED_OUT = -4,
    RESPONSE_TIMED_OUT  = -3,
    REQUEST_TIMED_OUT   = -2,
    CONNECTION_DENIED   = -1,
    DISCONNECTED        =  0,
    SENDING_REQUEST     =  1,
    SENDING_RESPONSE    =  2,
    CONNECTED           =  3,
};

inline bool is_error_state(ClientState s) {
    return static_cast<int>(s) < 0;
}

inline bool is_connected(ClientState s) {
    return s == ClientState::CONNECTED;
}

inline const char* client_state_name(ClientState s) {
    switch (s) {
        case ClientState::TOKEN_EXPIRED:       return "TOKEN_EXPIRED";
        case ClientState::INVALID_TOKEN:       return "INVALID_TOKEN";
        case ClientState::CONNECTION_TIMED_OUT: return "CONNECTION_TIMED_OUT";
        case ClientState::RESPONSE_TIMED_OUT:  return "RESPONSE_TIMED_OUT";
        case ClientState::REQUEST_TIMED_OUT:   return "REQUEST_TIMED_OUT";
        case ClientState::CONNECTION_DENIED:   return "CONNECTION_DENIED";
        case ClientState::DISCONNECTED:        return "DISCONNECTED";
        case ClientState::SENDING_REQUEST:     return "SENDING_REQUEST";
        case ClientState::SENDING_RESPONSE:    return "SENDING_RESPONSE";
        case ClientState::CONNECTED:           return "CONNECTED";
    }
    return "UNKNOWN";
}

} // namespace netudp

#endif /* NETUDP_CLIENT_STATE_H */
