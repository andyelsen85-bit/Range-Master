#pragma once
// TrapMaster encrypted LoRa protocol, shared by gateway and relay sketches.
//
// Frame = 12-byte nonce + 7-byte encrypted body + 16-byte GCM authentication tag.
// Production builds require a unique key. The bench key is available only with
// TM_ALLOW_INSECURE_BENCH_KEY and must never be used with a real trap.

#include <stdint.h>
#include <string.h>
#include <mbedtls/gcm.h>

namespace tm_protocol {

constexpr uint8_t VERSION = 0x01;
constexpr uint8_t CMD_FIRE = 0x01;
constexpr uint8_t CMD_ACK  = 0x02;
constexpr size_t  NONCE_LEN = 12;
constexpr size_t  BODY_LEN  = 7;
constexpr size_t  TAG_LEN   = 16;
constexpr size_t  FRAME_LEN = NONCE_LEN + BODY_LEN + TAG_LEN;

#if defined(TM_PROTOCOL_KEY_BYTES)
// Define TM_PROTOCOL_KEY_BYTES as exactly 16 comma-separated byte values in
// both sketches (or their shared Arduino build configuration).
static constexpr uint8_t CONFIGURED_KEY[] = TM_PROTOCOL_KEY_BYTES;
static_assert(sizeof(CONFIGURED_KEY) == 16,
              "TM_PROTOCOL_KEY_BYTES must contain exactly 16 bytes");
static constexpr uint8_t SHARED_KEY[16] = TM_PROTOCOL_KEY_BYTES;
#elif defined(TM_ALLOW_INSECURE_BENCH_KEY)
// Deliberately-identifiable bench key. This branch is only for a radio bench
// test with every relay disconnected from its physical trap.
static constexpr uint8_t SHARED_KEY[16] = {
    0x54, 0x4D, 0x2D, 0x44, 0x45, 0x56, 0x2D, 0x4B,
    0x45, 0x59, 0x2D, 0x43, 0x48, 0x41, 0x4E, 0x47
};
#else
#error "Define TM_PROTOCOL_KEY_BYTES to a unique 16-byte AES key before compiling"
#endif

struct DecodedPacket {
    uint8_t version;
    uint8_t machine;   // ASCII 'A' ... 'H'
    uint8_t command;
    uint32_t counter;
};

inline void write_u32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

inline uint32_t read_u32_le(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

inline bool valid_machine(uint8_t machine)
{
    return machine >= 'A' && machine <= 'H';
}

inline void make_nonce(uint32_t counter, uint8_t command,
                       uint8_t nonce[NONCE_LEN])
{
    // The gateway's persisted counter plus command direction makes this nonce
    // unique per key: a relay ACK may echo a FIRE counter, but never its nonce.
    memset(nonce, 0, NONCE_LEN);
    nonce[0] = 'T'; nonce[1] = 'M'; nonce[2] = '0'; nonce[3] = '1';
    write_u32_le(&nonce[4], counter);
    nonce[8] = command;
}

inline bool encrypt(uint8_t machine, uint8_t command, uint32_t counter,
                    uint8_t frame[FRAME_LEN])
{
    if (!valid_machine(machine) || counter == 0) return false;

    uint8_t plain[BODY_LEN] = { VERSION, machine, command, 0, 0, 0, 0 };
    write_u32_le(&plain[3], counter);
    make_nonce(counter, command, frame);

    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES,
                                SHARED_KEY, sizeof(SHARED_KEY) * 8);
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, BODY_LEN,
                                       frame, NONCE_LEN, nullptr, 0,
                                       plain, frame + NONCE_LEN, TAG_LEN,
                                       frame + NONCE_LEN + BODY_LEN);
    }
    mbedtls_gcm_free(&ctx);
    return rc == 0;
}

inline bool decrypt(const uint8_t *frame, size_t frame_len, DecodedPacket *out)
{
    if (!frame || !out || frame_len != FRAME_LEN) return false;

    uint8_t plain[BODY_LEN] = {};
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES,
                                SHARED_KEY, sizeof(SHARED_KEY) * 8);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(&ctx, BODY_LEN, frame, NONCE_LEN,
                                      nullptr, 0, frame + NONCE_LEN + BODY_LEN,
                                      TAG_LEN, frame + NONCE_LEN, plain);
    }
    mbedtls_gcm_free(&ctx);
    if (rc != 0) return false;

    out->version = plain[0];
    out->machine = plain[1];
    out->command = plain[2];
    out->counter = read_u32_le(&plain[3]);

    uint8_t expected_nonce[NONCE_LEN];
    make_nonce(out->counter, out->command, expected_nonce);
    return out->version == VERSION && out->counter != 0 &&
           valid_machine(out->machine) &&
           memcmp(frame, expected_nonce, NONCE_LEN) == 0;
}

} // namespace tm_protocol