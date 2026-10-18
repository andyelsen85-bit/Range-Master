#pragma once
#include <stddef.h>
void network_test_init();
bool network_test_start(bool automatic=false);
void network_test_status(char *out,size_t capacity);
