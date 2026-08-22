#pragma once
// Shared terminal-to-gateway request authentication.
//
// The secret is never sent over HTTP. Each request authenticates the target
// machine and a strictly increasing terminal sequence with HMAC-SHA-256.

#include <stddef.h>
#include <stdint.h>
#include <mbedtls/md.h>

namespace tm_auth {

constexpr size_t MAC_LEN = 32;
constexpr size_t MAC_HEX_LEN = MAC_LEN * 2;

inline void write_u32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

inline bool make_request_mac(const uint8_t *key, size_t key_len,
                             uint8_t machine, uint32_t sequence,
                             uint8_t out[MAC_LEN])
{
    if (!key || key_len < 16 || !out || machine < 'A' || machine > 'H' ||
        sequence == 0) {
        return false;
    }
    const uint8_t payload[8] = {
        'T', 'M', 0x01, machine,
        (uint8_t)sequence, (uint8_t)(sequence >> 8),
        (uint8_t)(sequence >> 16), (uint8_t)(sequence >> 24),
    };
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info && mbedtls_md_hmac(info, key, key_len, payload, sizeof(payload), out) == 0;
}

// Authenticates a non-actuating terminal-to-gateway health check. This uses a
// separate domain string from FIRE commands, so it cannot be replayed as a
// machine command or consume a command sequence.
inline bool make_health_mac(const uint8_t *key, size_t key_len,
                            uint8_t out[MAC_LEN])
{
    if (!key || key_len < 16 || !out) return false;
    const uint8_t payload[] = { 'T', 'M', 0x01, 'C', 'H', 'E', 'C', 'K' };
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info && mbedtls_md_hmac(info, key, key_len, payload, sizeof(payload), out) == 0;
}

inline char hex_digit(uint8_t value)
{
    return (value < 10) ? (char)('0' + value) : (char)('a' + value - 10);
}

inline void mac_to_hex(const uint8_t mac[MAC_LEN], char out[MAC_HEX_LEN + 1])
{
    for (size_t i = 0; i < MAC_LEN; ++i) {
        out[i * 2] = hex_digit((uint8_t)(mac[i] >> 4));
        out[i * 2 + 1] = hex_digit((uint8_t)(mac[i] & 0x0F));
    }
    out[MAC_HEX_LEN] = '\0';
}

inline int hex_value(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

inline bool mac_matches_hex(const uint8_t mac[MAC_LEN], const char *hex)
{
    if (!mac || !hex) return false;
    size_t length = 0;
    while (length <= MAC_HEX_LEN && hex[length] != '\0') ++length;
    if (length != MAC_HEX_LEN) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < MAC_LEN; ++i) {
        int high = hex_value(hex[i * 2]);
        int low = hex_value(hex[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        diff |= (uint8_t)(((high << 4) | low) ^ mac[i]);
    }
    return diff == 0;
}

} // namespace tm_auth