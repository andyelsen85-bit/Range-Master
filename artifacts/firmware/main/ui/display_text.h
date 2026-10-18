#pragma once
#include <stddef.h>
void utf8_to_display(char *dst,const char *src,size_t capacity);
bool display_name_matches(const char *name,const char *query);
