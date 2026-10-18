#pragma once
// Read-only evidence, collected while the journal/storage lock is held.
struct ActiveGameRepairInspection {
    int present=0,unreadable=0,error=0;
    bool had_active=false,active=false,ram_active=false,stale_latch=false;
};
struct QueueRepairInspection {
    int present=0,unreadable=0,error=0;
    bool non_empty=false,busy=false;
};
struct StorageRepairStatus {
    int pending=0;
    bool allowed=false;
    char reason[160]={};
    char checks[512]={};
};
