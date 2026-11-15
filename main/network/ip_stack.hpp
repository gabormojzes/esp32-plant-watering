#pragma once

namespace ip_stack {

// True once a network interface exists. Before that the TCP/IP stack is not
// running and socket calls fault rather than fail.
bool up();

}  // namespace ip_stack
