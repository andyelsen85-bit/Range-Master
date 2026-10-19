#pragma once

enum class RuntimeTask {
    Census, Checkpoint, FatSelftest, NetworkTest, Gateway, Count
};

void runtime_boot_begin();
void runtime_ui_ready();
bool runtime_optional_tasks_allowed();
// Called outside the LVGL lock. Clears the panic streak after a stable minute
// and emits each registered task's stack watermark once after 60 seconds.
void runtime_safety_tick();
void runtime_task_watch(RuntimeTask task);
// Must precede task deletion: a finished task's watermark is retained, never
// a dangling TaskHandle_t. The delayed report still happens after 60 seconds.
void runtime_task_finished(RuntimeTask task);
void runtime_task_first_report(RuntimeTask task);
