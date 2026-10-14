#pragma once
// Loop-owned. Reserve sequence ranges durably at boot/wrap; never change the
// FIRE/remote replay counters or delay their persist-before-TX/ACK ordering.
struct GatewayTrace {
    uint32_t seq=0,received=0,verified=0,saved=0,tx=0,done=0,ack=0,answer=0;
    int rssi=0,status=0;
    uint32_t nvsDuration=0;
    char machine=0;
};
static GatewayTrace gatewayTraces[50],activeTrace;
static uint32_t traceNext=0,traceEnd=0;
static unsigned traceCount=0;
static bool traceReserve() {
    const uint32_t previous=preferences.getULong("trace_hi",0);
    if(previous>UINT32_MAX-4096)return false;
    if(preferences.putULong("trace_hi",previous+4096)!=sizeof(uint32_t))return false;
    traceNext=previous;traceEnd=previous+4096;return true;
}
static void traceStart(char machine,uint32_t received) {
    activeTrace={};
    if(traceNext==traceEnd&&!traceReserve())return;
    activeTrace.seq=++traceNext;activeTrace.received=received;
    activeTrace.verified=millis();activeTrace.machine=machine;activeTrace.rssi=WiFi.RSSI();
}
static void traceFinish(int status) {
    if(!activeTrace.seq)return;
    activeTrace.answer=millis();activeTrace.status=status;
    gatewayTraces[(activeTrace.seq-1)%50]=activeTrace;
    if(traceCount<50)++traceCount;
    activeTrace={};
}
static void handleGatewayTrace() {
    if(!health_request_is_authentic()) {server.send(401,"application/json","{}");return;}
    uint32_t after=0;
    if(!buttonQueryNumber("after",after)) {server.send(400,"application/json","{}");return;}
    JsonDocument doc;
    doc["gatewayId"]=WiFi.macAddress();doc["next"]=traceNext;doc["uptimeMs"]=millis();
    auto entries=doc["entries"].to<JsonArray>();
    // Bounded chronological scan; a reserved range after reboot can have gaps.
    const uint32_t oldest=traceNext>50?traceNext-49:1;
    for(uint32_t id=oldest;id&&id<=traceNext;++id) {
        const auto &t=gatewayTraces[(id-1)%50];
        if(t.seq!=id||id<=after)continue;
        auto row=entries.add<JsonObject>();
        row["seq"]=id;row["machine"]=String(t.machine);
        row["durationMs"]=t.answer-t.received;row["rssi"]=t.rssi;row["status"]=t.status;
        row["authMs"]=t.verified-t.received;
        row["nvsMs"]=t.saved?t.saved-t.received:0;
        row["nvsDurationMs"]=t.nvsDuration;
        row["txStartMs"]=t.tx?t.tx-t.received:0;
        row["txDoneMs"]=t.done?t.done-t.received:0;
        row["ackMs"]=t.ack?t.ack-t.received:0;
        row["answerMs"]=t.answer-t.received;
    }
    String body;serializeJson(doc,body);
    server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",body);
}
