#pragma once
// One atomic identity/key-fingerprint record. Legacy sender/counter keys and
// unrelated NVS namespaces are never erased. Fingerprints are not key material.
namespace tm_remote_identity {
struct Binding {uint32_t version=1;uint8_t sender=0;uint8_t reserved[3]{};uint8_t fingerprint[32]{};};
enum Result { READY, MIGRATED, ID_LOCKED, KEY_NOT_ROTATED, STORAGE_ERROR, INVALID_BINDING };
template<class Store>
Result bind(Store &store,uint8_t sender,uint8_t allowedFrom=0) {
    if(!tm_protocol::valid_remote(sender)) return INVALID_BINDING;
    Binding old{};
    const size_t length=store.getBytesLength("identity_v2");
    if(!length&&store.isKey("identity_v2")) return INVALID_BINDING;
    if(length) {
        if(length!=sizeof(old)||store.getBytes("identity_v2",&old,sizeof(old))!=sizeof(old)||
           old.version!=1||!tm_protocol::valid_remote(old.sender)) return INVALID_BINDING;
    } else {
        old.sender=store.getUChar("sender",0);
        if(old.sender&&!tm_protocol::valid_remote(old.sender)) return INVALID_BINDING;
        if(!old.sender&&store.getULong("counter",0)) return INVALID_BINDING;
        if(!tm_protocol::key_fingerprint(tm_protocol::SHARED_KEY,old.fingerprint)) return STORAGE_ERROR;
    }
    Binding next{};next.sender=sender;
    if(!tm_protocol::key_fingerprint(tm_protocol::REMOTE_KEY,next.fingerprint)) return STORAGE_ERROR;
    const bool changed=old.sender&&old.sender!=sender;
    if(changed) {
        if(allowedFrom!=old.sender) return ID_LOCKED;
        if(!tm_protocol::separate_remote_key()||!memcmp(old.fingerprint,next.fingerprint,32))
            return KEY_NOT_ROTATED;
    }
    if(!length||memcmp(&old,&next,sizeof(next))) {
        if(store.putBytes("identity_v2",&next,sizeof(next))!=sizeof(next)) return STORAGE_ERROR;
    }
    return changed?MIGRATED:READY;
}
}
