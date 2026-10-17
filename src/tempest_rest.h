#pragma once
#include <stdint.h>

void tempest_rest_start();
uint32_t tempest_rest_stack_free();
void tempest_rest_trigger_now();
bool tempest_rest_refresh_pending();
