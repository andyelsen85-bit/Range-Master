#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Owned exclusively by the gateway HTTP/radio loop. No allocation or NVS writes.
namespace tm_gateway {
constexpr uint32_t NONCE_TTL_MS = 10000;
constexpr uint32_t RESULT_TTL_MS = 30000;
constexpr uint32_t NONCE_INTERVAL_MS = 20;
constexpr size_t MAX_OUTSTANDING = 8;
constexpr size_t NONCE_SLOTS = 32;
constexpr size_t RESPONSE_BYTES = 384;

enum class State : uint8_t { Empty, Issued, Running, Complete };
enum class Admission : uint8_t { Start, Cached, Busy, Conflict, Unauthorized };
struct Entry {
    State state = State::Empty;
    char nonce[33] = {};
    uint32_t timestamp = 0;
    bool pair = false;
    char first = 0, second = 0;
    uint32_t delay = 0;
    int status = 0;
    char response[RESPONSE_BYTES] = {};
};

class NonceRegistry {
    Entry entries[NONCE_SLOTS] = {};
    bool issued = false;
    uint32_t lastIssue = 0;
public:
    void expire(uint32_t now) {
        for (auto &entry : entries) {
            const uint32_t age = now - entry.timestamp; // millis() wrap safe
            if ((entry.state == State::Issued && age >= NONCE_TTL_MS) ||
                (entry.state == State::Complete && age >= RESULT_TTL_MS))
                entry = {};
            // Never expire/evict an in-progress command.
        }
    }
    Entry *find(const char *nonce) {
        for (auto &entry : entries)
            if (entry.state != State::Empty && strcmp(entry.nonce, nonce) == 0)
                return &entry;
        return nullptr;
    }
    bool issue(const char *nonce, uint32_t now) {
        expire(now);
        if (issued && now - lastIssue < NONCE_INTERVAL_MS) return false;
        size_t outstanding = 0;
        Entry *free = nullptr;
        for (auto &entry : entries) {
            if (entry.state == State::Issued) ++outstanding;
            if (entry.state == State::Empty && !free) free = &entry;
        }
        if (!free || outstanding >= MAX_OUTSTANDING || find(nonce)) return false;
        free->state = State::Issued;
        snprintf(free->nonce, sizeof(free->nonce), "%s", nonce);
        free->timestamp = now;
        issued = true;
        lastIssue = now;
        return true;
    }
    void finish(Entry *entry, int status, const char *response, uint32_t now) {
        entry->status = status;
        snprintf(entry->response, sizeof(entry->response), "%s", response);
        entry->timestamp = now;
        entry->state = State::Complete;
    }
    Admission admit(const char *nonce, bool pair, char first, char second,
                    uint32_t delay, bool busy, uint32_t now, Entry **out) {
        expire(now);
        *out = find(nonce);
        if (!*out) return Admission::Unauthorized;
        Entry &entry = **out;
        if (entry.state != State::Issued) {
            if (entry.pair != pair || entry.first != first ||
                entry.second != second || entry.delay != delay)
                return Admission::Conflict;
            return entry.state == State::Complete ? Admission::Cached : Admission::Busy;
        }
        entry.pair = pair;
        entry.first = first;
        entry.second = second;
        entry.delay = delay;
        entry.state = State::Running; // consume BEFORE any possible radio action
        if (busy) {
            // A rejected concurrent command must not turn into a late shot on retry.
            finish(&entry, 409, "{\"ok\":false,\"error\":\"busy\"}", now);
            return Admission::Cached;
        }
        return Admission::Start;
    }
};
}