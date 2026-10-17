#pragma once
#include <stdbool.h>

void web_config_server_begin();
void web_config_server_loop();
void web_config_server_network_changed();
void web_config_server_set_setup_ap(bool up);
void network_credentials_changed();
