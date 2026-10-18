#pragma once
// A separate recovery-admin credential; never reuse the catering operator PIN.
bool storage_admin_configured();
bool storage_admin_verify(const char *pin);
// Initial provisioning is exposed only on the physical settings screen.
bool storage_admin_set(const char *old_pin,const char *new_pin);
