#include "storage_admin.h"
#include "nvs.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/md.h"
#include <cstring>
#include <atomic>
namespace {
struct Pin {uint32_t magic;uint8_t salt[16],hash[32];uint32_t failures;};
constexpr uint32_t MAGIC=0x5350494e;
std::atomic_flag busy=ATOMIC_FLAG_INIT;
int64_t locked_until;
bool boot_checked;
int read(Pin *out) {
    nvs_handle_t n;if(nvs_open("tm_admin",NVS_READWRITE,&n)!=ESP_OK)return -1;
    size_t bytes=sizeof(*out);esp_err_t err=nvs_get_blob(n,"storage_pin",out,&bytes);nvs_close(n);
    if(err==ESP_ERR_NVS_NOT_FOUND)return 0;
    return err==ESP_OK&&bytes==sizeof(*out)&&out->magic==MAGIC?1:-1;
}
bool save(const Pin &pin) {
    nvs_handle_t n;if(nvs_open("tm_admin",NVS_READWRITE,&n)!=ESP_OK)return false;
    bool ok=nvs_set_blob(n,"storage_pin",&pin,sizeof(pin))==ESP_OK&&nvs_commit(n)==ESP_OK;
    nvs_close(n);return ok;
}
bool digest(const char *pin,const uint8_t salt[16],uint8_t hash[32]) {
    size_t length=pin?strnlen(pin,17):0;
    if(length<4||length>16||strspn(pin,"0123456789")!=length)return false;
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
        (const unsigned char*)pin,length,salt,16,120000,32,hash)==0;
}
}
bool storage_admin_configured(){Pin pin{};return read(&pin)!=0;}
bool storage_admin_verify(const char *value) {
    if(busy.test_and_set())return false;
    struct Release {~Release(){busy.clear();}} release;
    Pin pin{};if(read(&pin)!=1)return false;
    int64_t now=esp_timer_get_time();
    if(!boot_checked){boot_checked=true;if(pin.failures>=5)locked_until=now+30000000;}
    if(now<locked_until)return false;
    uint8_t hash[32]{};bool valid=digest(value,pin.salt,hash);uint8_t difference=0;
    for(unsigned i=0;i<sizeof(hash);i++)difference|=hash[i]^pin.hash[i];
    memset(hash,0,sizeof(hash));
    if(valid&&!difference){pin.failures=0;return save(pin);}
    if(pin.failures<1000000)++pin.failures;
    if(pin.failures>=5)locked_until=now+30000000;
    (void)save(pin);return false;
}
bool storage_admin_set(const char *old_pin,const char *new_pin) {
    Pin previous{};int state=read(&previous);
    if(state<0||(state==1&&!storage_admin_verify(old_pin)))return false;
    if(busy.test_and_set())return false;
    struct Release {~Release(){busy.clear();}} release;
    Pin pin{};pin.magic=MAGIC;esp_fill_random(pin.salt,sizeof(pin.salt));
    bool ok=digest(new_pin,pin.salt,pin.hash)&&save(pin);
    memset(&pin,0,sizeof(pin));return ok;
}
