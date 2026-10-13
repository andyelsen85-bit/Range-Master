#!/usr/bin/env python3
"""Run actual gateway network save/load/reset functions with Arduino/NVS shims."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("gateway_fixture", Path(__file__).with_name("test_nonce.py"))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)

STUBS = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
struct String : std::string {
    using std::string::string;
    String(const std::string &s) : std::string(s) {}
    void toCharArray(char *out, size_t size) const {
        if (!size) return;
        std::snprintf(out, size, "%s", c_str());
    }
};
struct IPAddress {
    uint8_t bytes[4]{};
    IPAddress() = default;
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : bytes{a,b,c,d} {}
    bool fromString(const char *s) {
        unsigned a,b,c,d; char tail;
        if (std::sscanf(s, "%u.%u.%u.%u%c", &a,&b,&c,&d,&tail) != 4 ||
            a>255 || b>255 || c>255 || d>255) return false;
        bytes[0]=a; bytes[1]=b; bytes[2]=c; bytes[3]=d; return true;
    }
    uint8_t operator[](size_t i) const { return bytes[i]; }
    bool operator==(const IPAddress &o) const { return std::memcmp(bytes,o.bytes,4)==0; }
};
struct Preferences {
    std::map<std::string,std::vector<uint8_t>> values;
    bool failWrite=false, failRemove=false;
    bool isKey(const char *key) const { return values.count(key); }
    bool remove(const char *key) { if (failRemove) return false; return values.erase(key)>0; }
    size_t getBytesLength(const char *key) { return isKey(key) ? values[key].size() : 0; }
    size_t getBytes(const char *key, void *out, size_t n) {
        if (!isKey(key) || values[key].size()!=n) return 0;
        std::memcpy(out, values[key].data(), n); return n;
    }
    size_t putBytes(const char *key, const void *input, size_t n) {
        if (failWrite) return 0;
        const auto *p=static_cast<const uint8_t *>(input);
        values[key]=std::vector<uint8_t>(p,p+n); return n;
    }
    bool getBool(const char *key, bool fallback) {
        return isKey(key) ? values[key][0]!=0 : fallback;
    }
    String getString(const char *key, const char *fallback) {
        if (!isKey(key)) return String(fallback);
        return String(std::string(values[key].begin(),values[key].end()));
    }
    void legacy(const char *key, const char *value) {
        values[key]=std::vector<uint8_t>(value,value+std::strlen(value));
    }
} preferences;
struct WebServer {
    std::map<std::string,String> args;
    bool hasArg(const char *key) const { return args.count(key); }
    String arg(const char *key) const {
        const auto it=args.find(key); return it==args.end() ? String("") : it->second;
    }
} portal;
constexpr int WFM_LABEL_AFTER=1;
struct WiFiManagerParameter {
    String value;
    WiFiManagerParameter(const char *, const char *, const char *v, int, const char *, int=0) : value(v) {}
    void setValue(const char *v, int) { value=String(v); }
    const char *getValue() const { return value.c_str(); }
};
struct WiFiManager {
protected:
    int _staShowStaticFields=0, _staShowDns=0;
public:
    WebServer *server=&portal;
    String savedSSID;
    IPAddress address, gateway;
    unsigned resetCalls=0;
    void addParameter(WiFiManagerParameter *) {}
    String getWiFiSSID(bool) { return savedSSID; }
    void setSTAStaticIPConfig(IPAddress a, IPAddress g, IPAddress, IPAddress) { address=a; gateway=g; }
    void resetSettings() { savedSSID=String(""); ++resetCalls; }
    bool nativeFieldsHidden() const { return _staShowStaticFields==-1 && _staShowDns==-1; }
};
struct WiFiStub {
    IPAddress address;
    unsigned dhcpCalls=0;
    bool config(IPAddress a, IPAddress, IPAddress) { address=a; ++dhcpCalls; return true; }
} WiFi;
struct SerialStub {
    void println(const char *) {}
    template<class... Args> void printf(const char *, Args...) {}
} Serial;
static bool networkSettingsSaved=false;
static String lastResult;
static void renderStatus() {}
"""

MAIN = r"""
static void save(const char *ssid, bool fixed, const char *ip="", const char *gw="") {
    portal.args.clear();
    portal.args["s"]=String(ssid);
    if (fixed) portal.args["tm_static"]=String("1");
    staticAddressParam.setValue(ip,15);
    staticGatewayParam.setValue(gw,15);
    saveNetworkSettings();
}
int main() {
    wifiManager.savedSSID=String("OldVenue");
    preferences.values["net_static"]={1};
    preferences.legacy("net_ip","192.168.1.10");
    preferences.legacy("net_gw","192.168.1.1");
    preferences.values["counter"]={91};
    preferences.values["remote_counter"]={17};
    loadNetworkSettings(); applyNetworkSettings(); configureNetworkPortal();
    assert(staticAddressEnabled && wifiManager.address==IPAddress(192,168,1,10));
    assert(wifiManager.nativeFieldsHidden());
    puts("PASS: legacy settings retained; only one set of static-IP fields");

    save("NewVenue",true,"10.20.30.40","10.20.30.1");
    assert(networkSettingsSaved && staticAddressEnabled);
    assert(wifiManager.address==IPAddress(10,20,30,40));
    wifiManager.savedSSID=String("NewVenue");
    staticAddressEnabled=false; staticAddressText[0]=0; staticGatewayText[0]=0;
    loadNetworkSettings(); applyNetworkSettings();
    assert(staticAddressEnabled && wifiManager.address==IPAddress(10,20,30,40));
    puts("PASS: new static address applies before connection and survives reboot");

    save("DhcpVenue",false,"10.20.30.40","10.20.30.1");
    assert(networkSettingsSaved && !staticAddressEnabled);
    assert(wifiManager.address==IPAddress(0,0,0,0) && WiFi.dhcpCalls>0);
    assert(staticAddressText[0]==0 && staticGatewayText[0]==0);
    wifiManager.savedSSID=String("DhcpVenue");
    loadNetworkSettings(); applyNetworkSettings();
    assert(!staticAddressEnabled && wifiManager.address==IPAddress(0,0,0,0));
    puts("PASS: DHCP clears live static address and survives reboot without stale fields");

    save("StaticVenue",true,"172.16.4.8","172.16.4.1");
    wifiManager.savedSSID=String("DifferentVenue");
    loadNetworkSettings(); applyNetworkSettings();
    assert(!staticAddressEnabled && wifiManager.address==IPAddress(0,0,0,0));
    puts("PASS: static address is never reused for a different SSID");

    const auto before=preferences.values["net_cfg"];
    save("BadVenue",true,"10.0.0.8","10.1.0.1");
    assert(!networkSettingsSaved && preferences.values["net_cfg"]==before);
    preferences.failWrite=true;
    save("StaticVenue",true,"172.16.4.9","172.16.4.1");
    assert(!networkSettingsSaved && preferences.values["net_cfg"]==before);
    preferences.failWrite=false;
    puts("PASS: invalid addresses and failed NVS commit do not replace the saved record");

    wifiManager.savedSSID=String("StaticVenue");
    loadNetworkSettings();
    assert(staticAddressEnabled);
    assert(resetNetworkSettings());
    loadNetworkSettings(); applyNetworkSettings(); configureNetworkPortal();
    assert(!staticAddressEnabled && staticAddressText[0]==0 && staticGatewayText[0]==0);
    assert(!preferences.isKey("net_cfg") && !preferences.isKey("net_static"));
    assert(!preferences.isKey("net_ip") && !preferences.isKey("net_gw"));
    assert(preferences.values["counter"]==std::vector<uint8_t>{91});
    assert(preferences.values["remote_counter"]==std::vector<uint8_t>{17});
    assert(wifiManager.resetCalls==1);
    puts("PASS: WiFi reset clears address settings but preserves radio/security counters");

    preferences.values["net_cfg"]={99};
    loadNetworkSettings(); applyNetworkSettings();
    assert(!staticAddressEnabled);
    preferences.failRemove=true;
    assert(!resetNetworkSettings());
    puts("PASS: corrupt record uses DHCP; reset reports storage failure");
}
"""

if __name__ == "__main__":
    source = (ROOT / "TrapMasterGateway.ino").read_text()
    setup = fixture.function(source, "void setup()")
    assert setup.index("resetNetworkSettings()") < setup.index("loadNetworkSettings()")
    assert setup.index("loadNetworkSettings()") < setup.index("applyNetworkSettings()")
    assert setup.index("setSaveParamsCallback(saveNetworkSettings)") < setup.index("wifiManager.autoConnect")
    manager = source[source.index("class GatewayWiFiManager"):source.index("GatewayWiFiManager wifiManager;")]
    globals_ = source[source.index("static const IPAddress STATIC_SUBNET"):source.index("static bool parseStaticNetwork")]
    functions = "\n".join(fixture.function(source, name) for name in (
        "static bool parseStaticNetwork", "static void loadNetworkSettings",
        "static void configureNetworkPortal", "static void saveNetworkSettings()",
        "static void applyNetworkSettings()\n{",
    ))
    with tempfile.TemporaryDirectory(prefix="tm-network-test-") as directory:
        path = Path(directory)
        (path / "test.cpp").write_text(STUBS + manager + "\nGatewayWiFiManager wifiManager;\n" + globals_ + functions + MAIN)
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(path / "test.cpp"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)
