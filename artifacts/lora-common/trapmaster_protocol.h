#pragma once
// TrapMaster encrypted LoRa protocol, shared by gateway and relay sketches.
//
// Frame = 12-byte nonce + 7-byte encrypted body + 16-byte GCM authentication tag.
// Production builds require a unique key. The bench key is available only with
// TM_ALLOW_INSECURE_BENCH_KEY and must never be used with a real trap.

#include <stdint.h>
#include <string.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>

namespace tm_protocol {

constexpr uint8_t VERSION = 0x01;
constexpr uint8_t CMD_FIRE = 0x01;
constexpr uint8_t CMD_ACK  = 0x02;
constexpr uint8_t CMD_BTN_BASE = 0x10;
constexpr uint8_t CMD_BTN_ACK = 0x20;
// Phone-only addressed machine tests. Legacy FIRE/ACK/BTN frames stay unchanged.
constexpr uint8_t CMD_TEST_BASE = 0x30;
constexpr size_t  NONCE_LEN = 12;
constexpr size_t  BODY_LEN  = 7;
constexpr size_t  TAG_LEN   = 16;
constexpr size_t  FRAME_LEN = NONCE_LEN + BODY_LEN + TAG_LEN;
constexpr size_t TEST_BODY_LEN = BODY_LEN + 6;
constexpr size_t TEST_FRAME_LEN = NONCE_LEN + TEST_BODY_LEN + TAG_LEN;

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

#if defined(TM_REMOTE_KEY_BYTES)
static constexpr uint8_t REMOTE_KEY[] = TM_REMOTE_KEY_BYTES;
static_assert(sizeof(REMOTE_KEY)==16,"TM_REMOTE_KEY_BYTES must contain 16 bytes");
#else
static constexpr const uint8_t *REMOTE_KEY=SHARED_KEY;
#endif
constexpr bool separate_remote_key() {
    for(unsigned i=0;i<16;++i) if(REMOTE_KEY[i]!=SHARED_KEY[i]) return true;
    return false;
}
#if defined(TM_REMOTE_KEY_BYTES)
static_assert(separate_remote_key(),"Remote key must differ from the existing relay key");
#endif
inline bool key_fingerprint(const uint8_t *key,uint8_t out[32]) {
    const auto *info=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info&&mbedtls_md(info,key,16,out)==0;
}

struct DecodedPacket {
    uint8_t version;
    uint8_t machine;   // ASCII 'A' ... 'H'
    uint8_t command;
    uint32_t counter;
    uint8_t target[6]{}; // P4 factory MAC; only present in addressed test frames
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

inline bool valid_remote(uint8_t sender) { return sender >= '1' && sender <= '8'; }
inline bool test_command(uint8_t command) {
    return command >= CMD_TEST_BASE && command < CMD_TEST_BASE + 8;
}
inline bool button_command(uint8_t command) {
    return command > CMD_BTN_BASE && command <= CMD_BTN_BASE + 6;
}
inline bool valid_address(uint8_t address, uint8_t command) {
    return valid_machine(address) ||
           (valid_remote(address) && (button_command(command) || test_command(command) ||
                                    command == CMD_BTN_ACK));
}

inline void make_nonce(uint32_t counter, uint8_t command,
                       uint8_t nonce[NONCE_LEN], uint8_t sender = 0)
{
    // The gateway's persisted counter plus command direction makes this nonce
    // unique per key: a relay ACK may echo a FIRE counter, but never its nonce.
    memset(nonce, 0, NONCE_LEN);
    nonce[0] = 'T'; nonce[1] = 'M'; nonce[2] = '0'; nonce[3] = '1';
    write_u32_le(&nonce[4], counter);
    nonce[8] = command;
    nonce[9] = sender; // zero for legacy relay packets: byte-for-byte compatible
}

inline bool encrypt(uint8_t machine, uint8_t command, uint32_t counter,
                    uint8_t frame[FRAME_LEN])
{
    if (!valid_address(machine, command) || test_command(command) || counter == 0) return false;

    uint8_t plain[BODY_LEN] = { VERSION, machine, command, 0, 0, 0, 0 };
    write_u32_le(&plain[3], counter);
    make_nonce(counter, command, frame, valid_remote(machine) ? machine : 0);

    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES,
                                valid_remote(machine)?REMOTE_KEY:SHARED_KEY, 128);
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
    if (!frame || !out || (frame_len != FRAME_LEN && frame_len != TEST_FRAME_LEN)) return false;

    const size_t bodyLen=frame_len-NONCE_LEN-TAG_LEN;
    // Nonce carries the sender domain. Never try the old relay key as a
    // fallback for a failed remote packet after remote-only key rotation.
    const bool remoteDomain=valid_remote(frame[9]);
    uint8_t plain[TEST_BODY_LEN] = {};
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES,
                                remoteDomain?REMOTE_KEY:SHARED_KEY, 128);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(&ctx, bodyLen, frame, NONCE_LEN,
                                      nullptr, 0, frame + NONCE_LEN + bodyLen,
                                      TAG_LEN, frame + NONCE_LEN, plain);
    }
    mbedtls_gcm_free(&ctx);
    if (rc != 0) return false;

    out->version = plain[0];
    out->machine = plain[1];
    out->command = plain[2];
    out->counter = read_u32_le(&plain[3]);
    memset(out->target,0,sizeof(out->target));
    if(bodyLen==TEST_BODY_LEN) memcpy(out->target,plain+BODY_LEN,6);

    uint8_t expected_nonce[NONCE_LEN];
    make_nonce(out->counter, out->command, expected_nonce,
               valid_remote(out->machine) ? out->machine : 0);
    return out->version == VERSION && out->counter != 0 &&
           valid_remote(out->machine)==remoteDomain &&
           (test_command(out->command)==(bodyLen==TEST_BODY_LEN)) &&
           valid_address(out->machine, out->command) &&
           memcmp(frame, expected_nonce, NONCE_LEN) == 0;
}

inline bool encrypt_test(uint8_t sender,uint8_t machine,uint32_t counter,
                         const uint8_t target[6],uint8_t frame[TEST_FRAME_LEN]) {
    if(!valid_remote(sender)||!valid_machine(machine)||!counter||!target) return false;
    uint8_t any=0;for(unsigned i=0;i<6;++i) any|=target[i];
    if(!any) return false;
    const uint8_t command=CMD_TEST_BASE+machine-'A';
    uint8_t plain[TEST_BODY_LEN]={VERSION,sender,command,0,0,0,0};
    write_u32_le(plain+3,counter);memcpy(plain+BODY_LEN,target,6);
    make_nonce(counter,command,frame,sender);
    mbedtls_gcm_context ctx;mbedtls_gcm_init(&ctx);
    int rc=mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,REMOTE_KEY,128);
    if(!rc) rc=mbedtls_gcm_crypt_and_tag(&ctx,MBEDTLS_GCM_ENCRYPT,TEST_BODY_LEN,
        frame,NONCE_LEN,nullptr,0,plain,frame+NONCE_LEN,TAG_LEN,frame+NONCE_LEN+TEST_BODY_LEN);
    mbedtls_gcm_free(&ctx);return rc==0;
}

} // namespace tm_protocol