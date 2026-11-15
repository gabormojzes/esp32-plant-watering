#include "network/ip_stack.hpp"

#include <esp_netif.h>

bool ip_stack::up() {
    return esp_netif_get_nr_of_ifs() > 0;
}
