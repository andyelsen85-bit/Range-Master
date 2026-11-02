#pragma once
// Portable policy; the HTTP task owns Auth. Radio/counter access stays in loop().
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/version.h>

namespace tm_phone {
constexpr uint32_t IDLE_MS=600000, NO_STATION_MS=120000;
constexpr unsigned ROUNDS=100000;
inline const char *shutdownReason(uint32_t now,uint32_t lastSuccess,uint32_t lastStation) {
    if(now-lastSuccess>=IDLE_MS) return "10 min inactivity";
    if(now-lastStation>=NO_STATION_MS) return "2 min no station";
    return nullptr;
}
inline bool pin(const char *p) {
    if(!p||strlen(p)!=4) return false;
    for(unsigned i=0;i<4;++i) if(p[i]<'0'||p[i]>'9') return false;
    return true;
}
inline bool equal(const void *a,const void *b,size_t n) {
    const auto *x=(const uint8_t*)a,*y=(const uint8_t*)b;
    unsigned diff=0;for(size_t i=0;i<n;++i) diff|=x[i]^y[i];return diff==0;
}
struct Credential { uint32_t version=1;uint8_t salt[16]{},hash[32]{}; };
inline bool derive(const char *p,const uint8_t salt[16],uint8_t hash[32]) {
    if(!pin(p)) return false;
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
        (const unsigned char*)p,4,salt,16,ROUNDS,32,hash)==0;
#else
    mbedtls_md_context_t ctx;mbedtls_md_init(&ctx);
    int rc=mbedtls_md_setup(&ctx,mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),1);
    if(!rc) rc=mbedtls_pkcs5_pbkdf2_hmac(&ctx,(const unsigned char*)p,4,
        salt,16,ROUNDS,32,hash);
    mbedtls_md_free(&ctx);return rc==0;
#endif
}
inline uint32_t backoff(unsigned failures) {
    if(failures<3) return 0;
    unsigned shift=failures-3;
    return shift>=5?900000:30000u<<shift;
}
// All persistence callbacks must report success. Corrupt/unreadable storage
// disables access instead of silently returning to unprovisioned setup.
template<class Store,class Random> class Auth {
    Store &store;Random &random;
public:
    Credential credential{};
    uint32_t failures=0,blockedAt=0,lastAction=0;
    bool provisioned=false,healthy=false,session=false;
    char token[33]{},csrf[33]{};
    Auth(Store &s,Random &r):store(s),random(r) {}
    void randomHex(char *out) {
        static const char h[]="0123456789abcdef";
        uint8_t bytes[16];random.fill(bytes,16);
        for(unsigned i=0;i<16;++i) {out[i*2]=h[bytes[i]>>4];out[i*2+1]=h[bytes[i]&15];}
        out[32]=0;
    }
    bool begin(uint32_t now) {
        healthy=store.load(credential,provisioned,failures);
        blockedAt=now;session=false;return healthy;
    }
    void revoke() {session=false;memset(token,0,sizeof(token));memset(csrf,0,sizeof(csrf));}
    uint32_t wait(uint32_t now) const {
        const uint32_t duration=backoff(failures),elapsed=now-blockedAt;
        return elapsed<duration?duration-elapsed:0;
    }
    bool valid(const char *cookie,uint32_t now) {
        if(session&&now-lastAction>=IDLE_MS) revoke();
        return healthy&&session&&cookie&&strlen(cookie)==32&&equal(cookie,token,32);
    }
    bool attempt(const char *p,uint32_t now) {
        if(!healthy||!provisioned||wait(now)) return false;
        uint8_t hash[32];
        const bool match=derive(p,credential.salt,hash)&&equal(hash,credential.hash,32);
        memset(hash,0,sizeof(hash));
        if(!match) {
            if(failures<UINT32_MAX) ++failures;
            blockedAt=now;
            healthy=store.saveFailures(failures);
            return false;
        }
        healthy=store.saveFailures(0);if(!healthy) {revoke();return false;}
        failures=0;return true;
    }
    bool set(const char *first,const char *second) {
        if(!healthy||!pin(first)||!pin(second)||!equal(first,second,4)) return false;
        Credential next;random.fill(next.salt,sizeof(next.salt));
        if(!derive(first,next.salt,next.hash)) return false;
        if(!store.saveCredential(next)) {healthy=false;revoke();return false;}
        credential=next;provisioned=true;revoke();return true;
    }
    void login(uint32_t now) {
        revoke();randomHex(token);randomHex(csrf);session=true;lastAction=now;
    }
};
} // namespace tm_phone
