#pragma once
#include <stdint.h>
#include <string.h>

// Small, bounded journal of the last closure applied to each player's session.
// Persist alongside the operational roster, never as part of an optional cache.
struct PaymentReceipt {
    int spielerId;
    uint64_t token;
};

static inline uint64_t payment_receipt_token(const char *date, const char *id)
{
    uint64_t value = UINT64_C(14695981039346656037);
    for (const char *p = date; p && *p; ++p)
        value = (value ^ (uint8_t)*p) * UINT64_C(1099511628211);
    value = (value ^ 0xffu) * UINT64_C(1099511628211);
    for (const char *p = id; p && *p; ++p)
        value = (value ^ (uint8_t)*p) * UINT64_C(1099511628211);
    return value;
}