#pragma once
// Start once, after UI construction. Slow tests never gate boot or sync.
void fat_selftest_after_ui();
bool offline_cache_pattern_step(int index,bool durable,char *error,unsigned capacity);
