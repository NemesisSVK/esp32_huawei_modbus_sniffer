#include "web_ui.h"
#include "config.h"
#include "BuildConfig.h"
#include "CodeDate.h"
#include "huawei_decoder.h"
#include "mqtt_publisher.h"
#include "LiveValueStore.h"
#include "UnifiedLogger.h"
#include "DebugLogBuffer.h"
#include "RawFrameStreamer.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <ESP.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include "PsramLimits.h"

// ============================================================
// Global frame counter (incremented by sniffer task)
// ============================================================
volatile uint32_t g_frames_decoded = 0;

// ============================================================
// Module pointers
// ============================================================
static ConfigManager*   s_cfg     = nullptr;
static MQTTManager*     s_mqtt    = nullptr;
static OTAManager*      s_ota     = nullptr;
static bool             s_ap_mode = false;
static AsyncWebServer*  s_server  = nullptr;
static uint32_t         s_boot_ms = 0;

// Deferred restart Ã¢â‚¬â€ lets the HTTP response flush before the device reboots
static bool          s_restart_pending = false;
static unsigned long s_restart_at_ms   = 0;
static constexpr unsigned long kRestartGraceMs = 250;

// IP whitelist
static IPWhitelistManager s_wl;

// ============================================================
// Group state -- s_seen is runtime-only (resets on boot).
// Enabled/tier state lives in Settings::Publish (config.json).
// ============================================================
static bool s_seen[GRP_COUNT] = {};

bool group_is_enabled(RegGroup g) {
    if (g >= GRP_COUNT || !s_cfg) return true;   // default enabled
    return s_cfg->getSettings().publish.group_enabled[g];
}

bool group_is_seen(RegGroup g) { return g < GRP_COUNT && s_seen[g]; }

void group_mark_seen(RegGroup g) {
    if (g < GRP_COUNT) s_seen[g] = true;
}

void group_set_enabled(RegGroup, bool) {
    // No-op -- group enable/disable is managed via the settings page
    // (POST /api/config -> ConfigManager::updateSettingsFromJson).
}


// ============================================================
// Security helpers
static bool check_auth(AsyncWebServerRequest* req) {
    if (!s_cfg) return true;
    const Settings& st = s_cfg->getSettings();

    // IP whitelist check
    if (s_wl.isEnabled()) {
        IPAddress client = req->client()->remoteIP();
        if (!s_wl.isIPWhitelisted(client)) {
            req->send(403, "text/plain", "Forbidden");
            return false;
        }
    }

    // Basic auth check
    if (st.security.auth_enabled && st.security.username.length() > 0) {
        if (!req->authenticate(st.security.username.c_str(),
                               st.security.password.c_str())) {
            req->requestAuthentication("Sniffer");
            return false;
        }
    }
    return true;
}

// ============================================================
// Helpers
// ============================================================

/** Escape user-controlled strings before embedding in HTML attributes or text. */
static String htmlEscape(const String& s) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < (size_t)s.length(); i++) {
        char c = s.charAt(i);
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;         break;
        }
    }
    return out;
}

/**
 * Schedule a reboot after kRestartGraceMs milliseconds.
 * Using a deferred restart (processed in web_ui_loop) ensures the HTTP
 * response is fully flushed before ESP.restart() fires Ã¢â‚¬â€ avoids the
 * browser seeing a connection-reset error on Save.
 */
static void scheduleRestart() {
    s_restart_pending = true;
    s_restart_at_ms   = millis() + kRestartGraceMs;
}

static void processPendingRestart() {
    if (!s_restart_pending) return;
    if ((long)(millis() - s_restart_at_ms) >= 0) {
        s_restart_pending = false;
        ESP.restart();
    }
}

// ============================================================
// Shared CSS / design tokens (dark theme matching Temp Manager)
// ============================================================
static const char* CSS = R"(
body{background:#1e1e1e;color:#fff;font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;margin:0;padding:60px 16px 24px}
h1{color:#2196f3;margin-bottom:24px}
h2{color:#2196f3;font-size:1.3em;margin:12px 0 8px;border-bottom:2px solid #2196f3;padding-bottom:4px}
a{color:#64b5f6;text-decoration:none}
.card{background:#2d2d2d;border:1px solid #424242;border-radius:8px;padding:16px;margin:10px 0}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:14px;margin-bottom:24px}
.form-row{display:flex;flex-direction:column;margin-bottom:10px}
.form-row label{font-weight:bold;margin-bottom:4px;font-size:.88em;color:#b0b0b0}
.form-row input,.form-row select{background:#333;color:#fff;border:1px solid #424242;padding:8px 10px;border-radius:4px;font-size:.9em}
.form-row input:focus,.form-row select:focus{outline:none;border-color:#2196f3}
.form-help{font-size:.78em;color:#888;margin-top:2px}
.settings-section{margin-top:14px;padding-top:10px;border-top:1px solid #3a3a3a}
.settings-section:first-of-type{margin-top:0;padding-top:0;border-top:none}
.settings-subtitle{color:#90caf9;font-size:1.05em;margin:0 0 8px;font-weight:700}
.btn{display:inline-block;padding:10px 20px;font-size:1em;font-weight:bold;border:none;border-radius:4px;cursor:pointer;text-decoration:none;transition:all .2s}
.btn-primary{background:linear-gradient(135deg,#2196f3,#1976d2);color:#fff;box-shadow:0 4px 8px rgba(33,150,243,.35)}
.btn-primary:hover{background:linear-gradient(135deg,#42a5f5,#2196f3);transform:translateY(-1px)}
.btn-danger{background:linear-gradient(135deg,#f44336,#d32f2f);color:#fff}
.btn-danger:hover{background:linear-gradient(135deg,#ef5350,#f44336);transform:translateY(-1px)}
.btn-sm{padding:5px 12px;font-size:.85em}
.badge{display:inline-block;padding:2px 8px;border-radius:10px;font-size:.75em;font-weight:bold}
.badge-ok{background:#1b5e20;color:#a5d6a7}
.badge-warn{background:#e65100;color:#ffcc80}
.badge-off{background:#37474f;color:#90a4ae}
.badge-new{background:#0d47a1;color:#bbdefb}
.status-bar{background:#252525;border:1px solid #333;border-radius:8px;padding:10px 16px;margin-bottom:16px;display:flex;flex-wrap:wrap;gap:14px;font-size:.82em;color:#b0b0b0}
.status-bar span b{color:#fff}
.group-card{background:#2d2d2d;border:1px solid #424242;border-radius:8px;padding:14px}
.group-card h3{margin:0 0 6px;color:#64b5f6;font-size:1em}
.group-card p{margin:0 0 10px;color:#999;font-size:.82em}
.toggle{position:relative;display:inline-block;width:44px;height:24px}
.toggle input{opacity:0;width:0;height:0}
.slider{position:absolute;cursor:pointer;inset:0;background:#555;border-radius:24px;transition:.3s}
.slider:before{content:'';position:absolute;width:18px;height:18px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.3s}


input:checked+.slider{background:#2196f3}
input:checked+.slider:before{transform:translateX(20px)}
.nav-bar{background:#2d2d2d;border-bottom:1px solid #424242;position:fixed;top:0;left:0;right:0;z-index:1000;display:flex;gap:8px;padding:8px 16px}
.nav-btn{padding:8px 16px;font-size:.95em;font-weight:bold;border-radius:4px;color:#fff;background:linear-gradient(135deg,#555,#444);text-decoration:none;transition:all .2s}
.nav-btn:hover{background:linear-gradient(135deg,#666,#555);transform:translateY(-1px)}
.nav-btn.active{background:linear-gradient(135deg,#2196f3,#1976d2);box-shadow:0 0 10px rgba(33,150,243,.6)}
@media(max-width:600px){.grid{grid-template-columns:1fr}.status-bar{gap:8px}}
.stat-row{display:flex;justify-content:space-between;align-items:flex-start;padding:5px 0;border-bottom:1px solid #333;gap:12px}
.stat-row:last-child{border-bottom:none}
.stat-label{color:#b0b0b0;flex:1;margin-right:8px;word-break:break-word}
.stat-right{text-align:right;flex-shrink:0}
.stat-value{font-weight:600}
.stat-meta{display:flex;gap:4px;font-size:.7em;color:#666;margin-top:2px;flex-wrap:wrap;justify-content:flex-end}
.stat-meta span{background:#252525;border-radius:3px;padding:1px 5px;color:#888;white-space:nowrap}
)";

// ============================================================
// PsPage Ã¢â‚¬â€ PSRAM-backed page builder
// Keeps HTML construction (15-25 KB) out of SRAM to prevent
// heap fragmentation during web serving.
// ============================================================
struct PsPage {
    char*  _buf = nullptr;
    size_t _len = 0;
    size_t _cap = 0;

    explicit PsPage(size_t reserve_hint = 24576) {
        _buf = (char*)heap_caps_malloc(reserve_hint,
                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (_buf) { _cap = reserve_hint; _buf[0] = '\0'; }
    }
    ~PsPage() { if (_buf) { heap_caps_free(_buf); _buf = nullptr; } }

    // Not copyable, but movable
    PsPage(const PsPage&)            = delete;
    PsPage& operator=(const PsPage&) = delete;

    PsPage(PsPage&& o) noexcept
        : _buf(o._buf), _len(o._len), _cap(o._cap)
    { o._buf = nullptr; o._len = 0; o._cap = 0; }

    PsPage& operator=(PsPage&& o) noexcept {
        if (this != &o) {
            if (_buf) heap_caps_free(_buf);
            _buf = o._buf; _len = o._len; _cap = o._cap;
            o._buf = nullptr; o._len = 0; o._cap = 0;
        }
        return *this;
    }

    PsPage& operator+=(PsPage&& o) { return append(o.c_str()); }

    PsPage& append(const char* s) {
        if (!_buf || !s) return *this;
        size_t n = strlen(s);
        if (_len + n + 1 > _cap) {
            size_t newcap = (_len + n + 1) * 2;
            char* nb = (char*)heap_caps_realloc(_buf, newcap,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!nb) return *this;
            _buf = nb; _cap = newcap;
        }
        memcpy(_buf + _len, s, n);
        _len += n;
        _buf[_len] = '\0';
        return *this;
    }
    PsPage& append(const String& s) { return append(s.c_str()); }

    PsPage& operator+=(const char*   s) { return append(s); }
    PsPage& operator+=(const String& s) { return append(s.c_str()); }

    bool        ok()     const { return _buf != nullptr; }
    size_t      length() const { return _len; }
    const char* c_str()  const { return _buf ? _buf : ""; }
};

static void ps_page_send(AsyncWebServerRequest* req, PsPage& page) {
    req->send(200, "text/html; charset=UTF-8", page.c_str());
}

// LED state string helpers Ã¢â‚¬â€ mirror the logic in loop() so the web UI
// can show what the physical LED currently means without needing a pointer.
static const char* led_state_str() {
    if (esp_get_free_heap_size() < psram_limits::kHeapCriticalThresholdBytes) return "magenta";
    if (!WiFi.isConnected())                                                 return "red";
    if (!(s_mqtt && s_mqtt->isConnected()))                                  return "yellow";
    if (g_frames_decoded == 0)                                               return "cyan";
    return "green";
}
static const char* led_meaning_str() {
    if (esp_get_free_heap_size() < psram_limits::kHeapCriticalThresholdBytes) return "Heap critical";
    if (!WiFi.isConnected())                                                  return "WiFi disconnected";
    if (!(s_mqtt && s_mqtt->isConnected()))                                   return "MQTT disconnected";
    if (g_frames_decoded == 0)                                                return "No Modbus frames yet";
    return "Fully operational";
}

static PsPage nav_html_ps(const char* current) {
    PsPage h;
    h += "<div class='nav-bar'>";
    auto btn = [&](const char* href, const char* label, const char* id) {
        h += "<a href='"; h += href;
        h += "' class='nav-btn";
        if (strcmp(current, id) == 0) h += " active";
        h += "'>"; h += label; h += "</a>";
    };
    btn("/",           "&#x2600; Home",        "home");
    btn("/priority",   "&#x26A1; Priority",    "priority");
    btn("/live",       "&#x1F4BB; Live Modbus","live");
    btn("/monitoring", "&#x1F4CA; Monitoring", "monitoring");
    btn("/settings",   "&#x2699; Settings",    "settings");
    btn("/logs",       "&#x1F4CB; Logs",       "logs");
    btn("/ota",        "&#x1F4E1; OTA",        "ota");
    h += "</div>";
    return h;
}

// ============================================================
// Page: Home "/"
// ============================================================
static void handle_root(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    PsPage page;
    if (!page.ok()) { req->send(503, "text/plain", "Out of PSRAM"); return; }
    page += "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<title>Huawei Sniffer</title><style>"; page += CSS; page += "</style>";
    page += "</head><body>";
    page += nav_html_ps("home");
    page += "<h1>&#x2600; Solar Live Data</h1>";
    page += "<div class='status-bar' id='sb'><span>Connecting...</span></div>";
    // Live decoded register values Ã¢â‚¬â€ polled from /api/values every 3 s
    page += "<div id='values-root'>";
    page += "<div class='card' style='color:#888;text-align:center;padding:32px'>";
    page += "Waiting for Modbus data &mdash; connect RS&#x2011;485 and ensure the inverter is on.";
    page += "</div></div>";
page += R"(<script>
const STATUS_POLL_MS = 4000;
const VALUES_POLL_MS = 3000;
function loadStatus(){
  fetch('/api/status').then(r=>r.json()).then(d=>{
    const valuesEvery = (VALUES_POLL_MS/1000).toFixed(0)+'s';
    const statusEvery = (STATUS_POLL_MS/1000).toFixed(0)+'s';
    document.getElementById('sb').innerHTML=
      "<span><b>Frames</b> "+d.frames+"</span>"+
      "<span><b>Refresh</b> values "+valuesEvery+" / status "+statusEvery+"</span>"+
      "<span title='Source tags'><b>Tags</b> DOC=official map, REV=reverse-engineered, NEW=unconfirmed decode, UNK=unknown, S#=slave</span>"+
      "<span title='Source families'><b>Sources</b> FC03/FC04=standard reads, H41-33/H41-X=proprietary Huawei</span>";
  }).catch(()=>{});}
function fmtAge(s){
  if(typeof s!=='number'||s<0) return null;
  if(s<60) return s+'s ago';
  const m=Math.floor(s/60),r=s%60;
  return r?m+'m '+r+'s ago':m+'m ago';
}
function ageColor(s){
  if(typeof s!=='number') return '#888';
  if(s<=90)  return '#4caf50';
  if(s<=300) return '#ffb300';
  return '#f44336';
}
function srcBadge(src,icon){
  const t=(src||'UNK');
  const ic=(icon||'UNK');
  const cls=(ic==='REV')?'badge-warn':(ic==='DOC'?'badge-ok':'badge-off');
  return "<span class='badge "+cls+"' title='Decode source'>"+ic+" "+t+"</span>";
}
function valTagBadge(vtag){
  if(!vtag) return '';
  return "<span class='badge badge-new' title='Reverse-engineered, awaiting confirmation'>"+vtag+"</span>";
}
function regBadge(reg, regEnd){
  if(typeof reg!=='number') return '';
  if(typeof regEnd==='number' && regEnd>=reg){
    return "<span class='badge badge-off' title='Source register range'>R"+reg+"-"+regEnd+"</span>";
  }
  return "<span class='badge badge-off' title='Source register'>R"+reg+"</span>";
}
function fmtInterval(ms){
  if(typeof ms!=='number'||ms<=0) return null;
  if(ms<1000) return ms+'ms';
  const s=ms/1000;
  return (s<10? s.toFixed(1):Math.round(s))+'s';
}
function loadValues(){
  fetch('/api/values').then(r=>r.json()).then(data=>{
    const keys=Object.keys(data);
    const root=document.getElementById('values-root');
    if(!keys.length){root.innerHTML="<div class='card' style='color:#888;text-align:center;padding:32px'>Waiting for Modbus data...</div>";return;}
    const byGrp={};
    keys.forEach(k=>{const e=data[k],g=e.group||'other';if(!byGrp[g])byGrp[g]=[];
      const parsedName=k.includes('/')?k.split('/').slice(1).join('/'):k;
      const name=e.name||parsedName;
      byGrp[g].push({name,src:e.src,src_icon:e.src_icon,vtag:e.vtag,reg:e.reg,reg_end:e.reg_end,reg_words:e.reg_words,slave:e.slave,v:e.v,u:e.u,age_s:e.age_s,min_ms:e.min_ms,avg_ms:e.avg_ms,max_ms:e.max_ms,min_s:e.min_s,avg_s:e.avg_s,max_s:e.max_s});});
    let html='<div class="grid">';
    Object.keys(byGrp).sort().forEach(grp=>{
      html+='<div class="card"><h2>'+grp+'</h2>';
      byGrp[grp].sort((a,b)=>(a.name+'|'+(a.src||'')+'|'+(a.slave||0)).localeCompare(b.name+'|'+(b.src||'')+'|'+(b.slave||0))).forEach(e=>{
        const val=(typeof e.v==='number')?e.v.toLocaleString(void 0,{maximumFractionDigits:2}):e.v;
        const unit=e.u?'<span class="muted" style="font-size:.8em;margin-left:4px">'+e.u+'</span>':'';
        const src=srcBadge(e.src,e.src_icon);
        const vtag=valTagBadge(e.vtag);
        const rbg=regBadge(e.reg,e.reg_end);
        const slv=(typeof e.slave==='number')?('<span class="badge badge-off" title="Modbus slave">S'+e.slave+'</span>'):'';
        const ageTxt=fmtAge(e.age_s);
        const agePart=ageTxt?'<span title="Last decoded" style="color:'+ageColor(e.age_s)+'">&#x23F1; '+ageTxt+'</span>':'';
        const minMs=(typeof e.min_ms==='number'&&e.min_ms>0)?e.min_ms:((typeof e.min_s==='number'&&e.min_s>0)?e.min_s*1000:null);
        const avgMs=(typeof e.avg_ms==='number'&&e.avg_ms>0)?e.avg_ms:((typeof e.avg_s==='number'&&e.avg_s>0)?e.avg_s*1000:null);
        const maxMs=(typeof e.max_ms==='number'&&e.max_ms>0)?e.max_ms:((typeof e.max_s==='number'&&e.max_s>0)?e.max_s*1000:null);
        const intPart=(minMs&&avgMs&&maxMs)
          ?'<span title="Min interval">&#8595;'+fmtInterval(minMs)+'</span><span title="1h avg">&#8776;'+fmtInterval(avgMs)+'</span><span title="Max interval">&#8593;'+fmtInterval(maxMs)+'</span>'
          :'';
        const meta=(agePart||intPart)?'<div class="stat-meta">'+agePart+intPart+'</div>':'';
        html+='<div class="stat-row"><span class="stat-label">'+src+' '+vtag+' '+rbg+' '+slv+' '+e.name+'</span><div class="stat-right"><span class="stat-value">'+val+unit+'</span>'+meta+'</div></div>';});
      html+='</div>';});
    html+='</div>';root.innerHTML=html;
  }).catch(()=>{});}
loadStatus();loadValues();
setInterval(loadStatus,STATUS_POLL_MS);setInterval(loadValues,VALUES_POLL_MS);
</script>)";
    page += "</body></html>";
    ps_page_send(req, page);
}

// ============================================================
// Page: Priority Monitor "/priority"
// Fast monitor for registers routed into manual priority group.
// ============================================================
static void handle_priority_page(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    PsPage page;
    if (!page.ok()) { req->send(503, "text/plain", "Out of PSRAM"); return; }
    page += "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<title>Priority Monitor</title><style>"; page += CSS; page += "</style>";
    page += "</head><body>";
    page += nav_html_ps("priority");
    page += "<h1>&#x26A1; Priority Monitor</h1>";
    page += "<div class='status-bar' id='psb'><span>Connecting...</span></div>";
    page += "<div id='priority-root'>";
    page += "<div class='card' style='color:#888;text-align:center;padding:32px'>";
    page += "Waiting for manual priority group values...";
    page += "</div></div>";
page += R"(<script>
const STATUS_POLL_MS = 3000;
const VALUES_POLL_MS = 1000;
let priorityEntryCount = 0;
function fmtAge(s){
  if(typeof s!=='number'||s<0) return null;
  if(s<60) return s+'s ago';
  const m=Math.floor(s/60),r=s%60;
  return r?m+'m '+r+'s ago':m+'m ago';
}
function ageColor(s){
  if(typeof s!=='number') return '#888';
  if(s<=30)  return '#4caf50';
  if(s<=120) return '#ffb300';
  return '#f44336';
}
function fmtInterval(ms){
  if(typeof ms!=='number'||ms<=0) return null;
  if(ms<1000) return ms+'ms';
  const s=ms/1000;
  return (s<10? s.toFixed(1):Math.round(s))+'s';
}
function srcBadge(src,icon){
  const t=(src||'UNK');
  const ic=(icon||'UNK');
  const cls=(ic==='REV')?'badge-warn':(ic==='DOC'?'badge-ok':'badge-off');
  return "<span class='badge "+cls+"' title='Decode source'>"+ic+" "+t+"</span>";
}
function regBadge(reg, regEnd){
  if(typeof reg!=='number') return '';
  if(typeof regEnd==='number'&&regEnd>=reg){
    return "<span class='badge badge-off' title='Source register range'>R"+reg+"-"+regEnd+"</span>";
  }
  return "<span class='badge badge-off' title='Source register'>R"+reg+"</span>";
}
function loadStatus(entryCount){
  fetch('/api/status').then(r=>r.json()).then(d=>{
    document.getElementById('psb').innerHTML=
      "<span><b>Frames</b> "+d.frames+"</span>"+
      "<span><b>Priority Entries</b> "+entryCount+"</span>"+
      "<span><b>Refresh</b> values "+(VALUES_POLL_MS/1000).toFixed(0)+"s / status "+(STATUS_POLL_MS/1000).toFixed(0)+"s</span>"+
      "<span><b>Scope</b> manual priority group only (MQTT topic: priority)</span>";
  }).catch(()=>{});
}
function loadValues(){
  fetch('/api/priority_values').then(r=>r.json()).then(data=>{
    const keys=Object.keys(data||{});
    const root=document.getElementById('priority-root');
    priorityEntryCount = keys.length;
    if(!keys.length){
      root.innerHTML="<div class='card' style='color:#888;text-align:center;padding:32px'>No values in manual priority group yet. Add register names in Settings -> Manual Priority Group and wait for matching traffic.</div>";
      return;
    }
    const rows=[];
    keys.forEach(k=>{
      const e=data[k]||{};
      const parsedName=k.includes('/')?k.split('/').slice(1).join('/'):k;
      rows.push({
        name:e.name||parsedName, src:e.src, src_icon:e.src_icon,
        reg:e.reg, reg_end:e.reg_end, slave:e.slave,
        v:e.v, u:e.u, age_s:e.age_s, min_ms:e.min_ms, avg_ms:e.avg_ms, max_ms:e.max_ms,
        min_s:e.min_s, avg_s:e.avg_s, max_s:e.max_s
      });
    });
    rows.sort((a,b)=>(a.name+'|'+(a.src||'')+'|'+(a.slave||0)).localeCompare(b.name+'|'+(b.src||'')+'|'+(b.slave||0)));
    let html="<div class='card'><h2>Manual Priority Registers</h2>";
    rows.forEach(e=>{
      const val=(typeof e.v==='number')?e.v.toLocaleString(void 0,{maximumFractionDigits:2}):e.v;
      const unit=e.u?'<span class="muted" style="font-size:.8em;margin-left:4px">'+e.u+'</span>':'';
      const ageTxt=fmtAge(e.age_s);
      const agePart=ageTxt?'<span title="Last decoded" style="color:'+ageColor(e.age_s)+'">&#x23F1; '+ageTxt+'</span>':'';
      const minMs=(typeof e.min_ms==='number'&&e.min_ms>0)?e.min_ms:((typeof e.min_s==='number'&&e.min_s>0)?e.min_s*1000:null);
      const avgMs=(typeof e.avg_ms==='number'&&e.avg_ms>0)?e.avg_ms:((typeof e.avg_s==='number'&&e.avg_s>0)?e.avg_s*1000:null);
      const maxMs=(typeof e.max_ms==='number'&&e.max_ms>0)?e.max_ms:((typeof e.max_s==='number'&&e.max_s>0)?e.max_s*1000:null);
      const intPart=(minMs&&avgMs&&maxMs)?'<span title="Min interval">&#8595;'+fmtInterval(minMs)+'</span><span title="Avg interval">&#8776;'+fmtInterval(avgMs)+'</span><span title="Max interval">&#8593;'+fmtInterval(maxMs)+'</span>':'';
      const meta=(agePart||intPart)?'<div class="stat-meta">'+agePart+intPart+'</div>':'';
      const slv=(typeof e.slave==='number')?('<span class="badge badge-off" title="Modbus slave">S'+e.slave+'</span>'):'';
      html+='<div class="stat-row"><span class="stat-label">'+srcBadge(e.src,e.src_icon)+' '+regBadge(e.reg,e.reg_end)+' '+slv+' '+e.name+'</span><div class="stat-right"><span class="stat-value">'+val+unit+'</span>'+meta+'</div></div>';
    });
    html+="</div>";
    root.innerHTML=html;
  }).catch(()=>{});
}
function tick(){
  loadValues();
}
let iv = null;
function startPolling(){
  if(iv!==null) return;
  tick();
  iv = setInterval(tick, VALUES_POLL_MS);
}
function stopPolling(){
  if(iv===null) return;
  clearInterval(iv);
  iv = null;
}
document.addEventListener('visibilitychange',()=>{
  if(document.visibilityState==='visible'){ startPolling(); }
  else { stopPolling(); }
});
loadStatus(0);
startPolling();
setInterval(()=>loadStatus(priorityEntryCount), STATUS_POLL_MS);
</script>)";
    page += "</body></html>";
    ps_page_send(req, page);
}

// ============================================================
// Page: Live Modbus "/live"
// In-place state view: rows are keyed by source+slave+register and updated
// when new samples arrive (no rolling log behavior).
// ============================================================
static void handle_live_page(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    PsPage page;
    if (!page.ok()) { req->send(503, "text/plain", "Out of PSRAM"); return; }
    page += "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<title>Live Modbus</title><style>"; page += CSS;
    page += ".live-grid{display:grid;grid-template-columns:repeat(4,minmax(260px,1fr));gap:12px}";
    page += ".live-col{background:#2d2d2d;border:1px solid #424242;border-radius:8px;padding:12px}";
    page += ".live-col h2{margin:0 0 8px;font-size:1.05em}";
    page += ".live-empty{color:#888;font-size:.85em;padding:10px 4px}";
    page += "@media(max-width:1400px){.live-grid{grid-template-columns:repeat(2,minmax(260px,1fr));}}";
    page += "@media(max-width:760px){.live-grid{grid-template-columns:1fr;}}";
    page += "</style></head><body>";
    page += nav_html_ps("live");
    page += "<h1>&#x1F4BB; Live Modbus State</h1>";
    page += "<div class='status-bar' id='lsb'><span>Connecting...</span></div>";
    page += "<div id='live-root' class='live-grid'></div>";
    page += R"(<script>
const POLL_MS=3000;
let sinceSeq=0;
const rows=new Map();
const COLS=[
  {key:'inverter',label:'Inverter'},
  {key:'battery',label:'Battery'},
  {key:'meter',label:'Meter'},
  {key:'other',label:'Other'}
];
function fmtAge(s){
  if(typeof s!=='number'||s<0) return null;
  if(s<60) return s+'s ago';
  const m=Math.floor(s/60),r=s%60;
  return r?m+'m '+r+'s ago':m+'m ago';
}
function ageColor(s){
  if(typeof s!=='number') return '#888';
  if(s<=90) return '#4caf50';
  if(s<=300) return '#ffb300';
  return '#f44336';
}
function fmtInterval(ms){
  if(typeof ms!=='number'||ms<=0) return null;
  if(ms<1000) return ms+'ms';
  const s=ms/1000;
  return (s<10?s.toFixed(1):Math.round(s))+'s';
}
function srcBadge(src,icon){
  const t=(src||'UNK');
  const ic=(icon||'UNK');
  const cls=(ic==='REV')?'badge-warn':(ic==='DOC'?'badge-ok':'badge-off');
  return "<span class='badge "+cls+"' title='Decode source'>"+ic+" "+t+"</span>";
}
function vtagBadge(vtag){
  if(!vtag) return '';
  const title=(vtag==='UNK')?'Unknown register candidate':'Reverse-engineered, awaiting confirmation';
  return "<span class='badge badge-new' title='"+title+"'>"+vtag+"</span>";
}
function regBadge(reg,regEnd){
  if(typeof reg!=='number') return '';
  if(typeof regEnd==='number'&&regEnd>=reg){
    return "<span class='badge badge-off' title='Source register range'>R"+reg+"-"+regEnd+"</span>";
  }
  return "<span class='badge badge-off' title='Source register'>R"+reg+"</span>";
}
function slaveBadge(slave){
  if(typeof slave!=='number') return '';
  return "<span class='badge badge-off' title='Modbus slave'>S"+slave+"</span>";
}
function cmpRow(a,b){
  if((a.known?1:0)!==(b.known?1:0)) return (b.known?1:0)-(a.known?1:0);
  const ra=(typeof a.reg==='number')?a.reg:999999;
  const rb=(typeof b.reg==='number')?b.reg:999999;
  if(ra!==rb) return ra-rb;
  const sa=(a.src||'')+'|'+(a.slave||0)+'|'+(a.name||'');
  const sb=(b.src||'')+'|'+(b.slave||0)+'|'+(b.name||'');
  return sa.localeCompare(sb);
}
function valText(e){
  if(typeof e.v==='number'){
    const s=e.v.toLocaleString(void 0,{maximumFractionDigits:2});
    return e.u ? s+" <span class='muted' style='font-size:.8em;margin-left:4px'>"+e.u+"</span>" : s;
  }
  if(typeof e.v_str==='string') return e.v_str;
  return '—';
}
function rowHtml(e){
  const ageTxt=fmtAge(e.age_s);
  const agePart=ageTxt?'<span title="Last decoded" style="color:'+ageColor(e.age_s)+'">&#x23F1; '+ageTxt+'</span>':'';
  const minMs=(typeof e.min_ms==='number'&&e.min_ms>0)?e.min_ms:null;
  const avgMs=(typeof e.avg_ms==='number'&&e.avg_ms>0)?e.avg_ms:null;
  const maxMs=(typeof e.max_ms==='number'&&e.max_ms>0)?e.max_ms:null;
  const intPart=(minMs&&avgMs&&maxMs)?'<span title="Min interval">&#8595;'+fmtInterval(minMs)+'</span><span title="Avg interval">&#8776;'+fmtInterval(avgMs)+'</span><span title="Max interval">&#8593;'+fmtInterval(maxMs)+'</span>':'';
  const meta=(agePart||intPart)?'<div class="stat-meta">'+agePart+intPart+'</div>':'';
  return "<div class='stat-row'><span class='stat-label'>"+
      srcBadge(e.src,e.src_icon)+' '+vtagBadge(e.vtag)+' '+regBadge(e.reg,e.reg_end)+' '+slaveBadge(e.slave)+' '+(e.name||'unknown')+
      "</span><div class='stat-right'><span class='stat-value'>"+valText(e)+"</span>"+meta+"</div></div>";
}
function render(){
  const grouped={inverter:[],battery:[],meter:[],other:[]};
  rows.forEach((v)=>{const g=(v.group&&grouped[v.group])?v.group:'other';grouped[g].push(v);});
  let html='';
  COLS.forEach(c=>{
    const arr=grouped[c.key].sort(cmpRow);
    html+="<div class='live-col'><h2>"+c.label+"</h2>";
    if(!arr.length){
      html+="<div class='live-empty'>No data yet.</div>";
    }else{
      arr.forEach(e=>{html+=rowHtml(e);});
    }
    html+="</div>";
  });
  document.getElementById('live-root').innerHTML=html;
  document.getElementById('lsb').innerHTML=
    "<span><b>Entries</b> "+rows.size+"</span>"+
    "<span><b>Refresh</b> "+(POLL_MS/1000).toFixed(0)+"s (delta updates)</span>"+
    "<span><b>Tags</b> DOC/REV source, NEW provisional, UNK unknown candidate</span>";
}
function poll(){
  fetch('/api/live_values?since='+sinceSeq).then(r=>r.json()).then(d=>{
    const items=Array.isArray(d.items)?d.items:[];
    items.forEach(it=>{if(it&&it.id) rows.set(it.id,it);});
    if(typeof d.latest_seq==='number') sinceSeq=d.latest_seq;
    render();
  }).catch(()=>{});
}
poll();
setInterval(poll,POLL_MS);
</script>)";
    page += "</body></html>";
    ps_page_send(req, page);
}


// ============================================================
// Page: Settings "/settings"
// ============================================================
static void handle_settings(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    if (!s_cfg) { req->send(500, "text/plain", "ConfigManager not set"); return; }
    const Settings& st = s_cfg->getSettings();

    PsPage page;
    if (!page.ok()) { req->send(503, "text/plain", "Out of PSRAM"); return; }
    page += "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<title>Settings \u2014 Huawei Sniffer</title><style>"; page += CSS; page += "</style>";
    page += "</head><body>";
    page += nav_html_ps("settings");
    page += "<h1>&#9881; Settings</h1>";
    page += "<form id='sf'>";
    page += "<div class='grid'>";

    auto row = [&](const char* label, const char* name, const String& val, const char* type="text", const char* help="") -> String {
        String h = "<div class='form-row'>";
        h += "<label>"; h += label; h += "</label>";
        h += "<input type='"; h += type; h += "' name='"; h += name; h += "' id='"; h += name;
        h += "' value='"; h += (strcmp(type, "password") == 0 ? "" : htmlEscape(val)); h += "'>";
        if (help && *help) { h += "<div class='form-help'>"; h += help; h += "</div>"; }
        h += "</div>";
        return h;
    };

    // Network card (WiFi + Network + Security)
    page += "<div class='card'><h2>&#x1F310; Network</h2>";
    page += "<div class='settings-section'><h3 class='settings-subtitle'>&#x1F4F6; WiFi</h3>";
    page += row("SSID",     "wifi_ssid",     st.wifi.ssid);
    page += row("Password", "wifi_password", "", "password", "Leave blank to keep current");
    page += "</div>";
    page += "<div class='settings-section'><h3 class='settings-subtitle'>&#x1F4BB; Hostname &amp; mDNS</h3>";
    page += row("Hostname (mDNS)", "network_hostname", st.network.hostname, "text", "Access at hostname.local");
    page += "<div class='form-row'><label>mDNS</label><select name='network_mdns_enabled'>";
    page += String("<option value='true'") + (st.network.mdns_enabled ? " selected" : "") + ">Enabled</option>";
    page += String("<option value='false'") + (!st.network.mdns_enabled ? " selected" : "") + ">Disabled</option>";
    page += "</select></div>";
    page += "</div>";
    page += "<div class='settings-section'><h3 class='settings-subtitle'>&#x1F512; Security</h3>";
    page += "<div class='form-row'><label>Auth</label><select name='security_auth_enabled'>";
    page += String("<option value='false'") + (!st.security.auth_enabled ? " selected" : "") + ">Disabled</option>";
    page += String("<option value='true'")  + (st.security.auth_enabled  ? " selected" : "") + ">Enabled</option>";
    page += "</select></div>";
    page += row("Username",    "security_username", st.security.username);
    page += row("Password",    "security_password", "", "password", "Leave blank to keep current");
    page += "<div class='form-row'><label>IP Whitelist</label><select name='security_wl_enabled'>";
    page += String("<option value='false'") + (!st.security.ip_whitelist_enabled ? " selected" : "") + ">Disabled</option>";
    page += String("<option value='true'")  + (st.security.ip_whitelist_enabled  ? " selected" : "") + ">Enabled</option>";
    page += "</select></div>";
    String ranges;
    for (size_t i = 0; i < st.security.ip_ranges.size(); i++) {
        if (i) ranges += "\n";
        ranges += st.security.ip_ranges[i];
    }
    page += "<div class='form-row'><label>IP Ranges</label><textarea name='security_ip_ranges' rows='4' style='background:#333;color:#fff;border:1px solid #424242;border-radius:4px;padding:8px;resize:vertical;font-family:monospace;font-size:.85em'>";
    page += ranges;
    page += "</textarea><div class='form-help'>One per line. Format: 192.168.1.100 or 192.168.1.100-20 (range of 20 IPs)</div></div>";
    page += "</div>";
    page += "</div>";

    // MQTT + Device Info card
    page += "<div class='card'><h2>&#x1F4E1; MQTT</h2>";
    page += row("Server",   "mqtt_server",   st.mqtt.server);
    page += row("Port",     "mqtt_port",     String(st.mqtt.port), "number");
    page += row("Username", "mqtt_user",     st.mqtt.user);
    page += row("Password", "mqtt_password", "", "password", "Leave blank to keep current");
    page += row("Client ID","mqtt_client_id",st.mqtt.client_id);
    page += row("Base Topic","mqtt_base_topic",st.mqtt.base_topic);

    page += "<div class='form-row'><label>Format</label>"
            "<span class='form-help' style='display:inline-block'>"
            "JSON per group &mdash; publish timing set via Publish Tiers.</span></div>";
    page += "<div class='settings-section'><h3 class='settings-subtitle'>&#x1F4F1; Device Info</h3>";
    page += row("Device Name",  "device_name", st.device_info.name,         "text", "Shown in HA device registry");
    page += row("Manufacturer", "device_mfr",  st.device_info.manufacturer);
    page += row("Model",        "device_model", st.device_info.model);
    page += "</div>";
    page += "</div>";

    // Publish Configuration
    page += "<div class='card'><h2>&#x1F4E1; Publish Configuration</h2>";
    page += "<h3 style='color:#b0b0b0;font-size:.95em;margin:8px 0 6px'>Tier Intervals</h3>";
    page += row("HIGH interval (s)",   "tier_high_s",   String(st.publish.tier_interval_s[TIER_HIGH]),   "number", "Fast-update groups (e.g. meter)");
    page += row("MEDIUM interval (s)", "tier_medium_s", String(st.publish.tier_interval_s[TIER_MEDIUM]), "number", "Standard groups (e.g. inverter, battery)");
    page += row("LOW interval (s)",    "tier_low_s",    String(st.publish.tier_interval_s[TIER_LOW]),    "number", "Slow/static groups (e.g. info, config)");
    page += "<h3 style='color:#b0b0b0;font-size:.95em;margin:14px 0 6px'>Per-Group Configuration</h3>";
    page += "<div class='form-help' style='margin-bottom:8px'>Only groups with active decode mappings are shown.</div>";
    page += "<div style='overflow-x:auto'>";
    page += "<table style='width:100%;border-collapse:collapse;font-size:.88em'>";
    page += "<tr style='border-bottom:2px solid #2196f3'>"
            "<th style='text-align:left;padding:8px;color:#b0b0b0'>Group</th>"
            "<th style='text-align:left;padding:8px;color:#b0b0b0'>Tier</th>"
            "<th style='text-align:center;padding:8px;color:#b0b0b0'>Enabled</th></tr>";

    auto render_group_row = [&](RegGroup g) {
        const int gi = (int)g;
        uint8_t tier = st.publish.group_tier[gi];
        bool    en   = st.publish.group_enabled[gi];
        char tr_buf[700];
        snprintf(tr_buf, sizeof(tr_buf),
            "<tr style='border-bottom:1px solid #424242'>"
            "<td style='padding:8px'>%s<div class='form-help'>%s</div></td>"
            "<td style='padding:8px'>"
            "<select name='grp_tier_%s' style='background:#333;color:#fff;border:1px solid #424242;padding:4px 8px;border-radius:4px;font-size:.9em'>"
            "<option value='high'%s>HIGH</option>"
            "<option value='medium'%s>MEDIUM</option>"
            "<option value='low'%s>LOW</option>"
            "</select></td>"
            "<td style='padding:8px;text-align:center'>"
            "<label class='toggle'><input type='checkbox' name='grp_en_%s'%s>"
            "<span class='slider'></span></label></td></tr>",
            GROUP_INFO[gi].label, GROUP_INFO[gi].key,
            GROUP_INFO[gi].key,
            (tier == TIER_HIGH   ? " selected" : ""),
            (tier == TIER_MEDIUM ? " selected" : ""),
            (tier == TIER_LOW    ? " selected" : ""),
            GROUP_INFO[gi].key,
            (en ? " checked" : "")
        );
        page += tr_buf;
    };

    // Settings order: show operationally important groups first.
    static const RegGroup kSettingsPriority[] = {
        GRP_INVERTER_AC,
        GRP_METER,
        GRP_PV_STRINGS,
        GRP_INVERTER_ENERGY,
        GRP_INVERTER_STATUS,
        GRP_BATTERY
    };

    bool rendered[GRP_COUNT] = {};
    for (RegGroup g : kSettingsPriority) {
        if (g >= GRP_COUNT) continue;
        if (!huawei_decoder_group_has_registers(g)) continue;
        render_group_row(g);
        rendered[g] = true;
    }

    // Fallback for any additional supported groups not listed above.
    for (int gi = 0; gi < (int)GRP_COUNT; gi++) {
        if (rendered[gi]) continue;
        if (!huawei_decoder_group_has_registers((RegGroup)gi)) continue;
        render_group_row((RegGroup)gi);
    }
    page += "</table></div>";

    // Manual priority group (custom register list, dedicated topic/tier).
    String manualRegs;
    for (size_t i = 0; i < st.publish.manual_group.registers.size(); i++) {
        if (i) manualRegs += "\n";
        manualRegs += st.publish.manual_group.registers[i];
    }
    page += "<h3 style='color:#b0b0b0;font-size:.95em;margin:14px 0 6px'>Manual Priority Group</h3>";
    page += "<div class='form-row'><label>Enabled</label><select name='manual_group_enabled'>";
    page += String("<option value='false'") + (!st.publish.manual_group.enabled ? " selected" : "") + ">Disabled</option>";
    page += String("<option value='true'") + (st.publish.manual_group.enabled ? " selected" : "") + ">Enabled</option>";
    page += "</select><div class='form-help'>Route selected registers into dedicated MQTT topic <code>priority</code>.</div></div>";
    page += "<div class='form-row'><label>Tier</label><select name='manual_group_tier'>";
    page += String("<option value='high'") + (st.publish.manual_group.tier == TIER_HIGH ? " selected" : "") + ">HIGH</option>";
    page += String("<option value='medium'") + (st.publish.manual_group.tier == TIER_MEDIUM ? " selected" : "") + ">MEDIUM</option>";
    page += String("<option value='low'") + (st.publish.manual_group.tier == TIER_LOW ? " selected" : "") + ">LOW</option>";
    page += "</select></div>";
    page += "<div class='form-row'><label>Add Register</label>"
            "<div style='display:flex;gap:8px;align-items:center'>"
            "<input id='manual_reg_pick' list='known_register_names' placeholder='Select source + register' style='flex:1'>"
            "<datalist id='known_register_names'></datalist>"
            "<button type='button' class='btn btn-sm' style='padding:6px 10px' onclick='addManualRegister()'>Add</button>"
            "</div>"
            "<div id='manual_reg_hint' class='form-help'>Loading known register selectors (observed sources)...</div></div>";
    page += "<div class='form-row'><label>Registers</label><textarea name='manual_group_registers' id='manual_group_registers' rows='6' style='background:#333;color:#fff;border:1px solid #424242;border-radius:4px;padding:8px;resize:vertical;font-family:monospace;font-size:.85em'>";
    page += htmlEscape(manualRegs);
    page += "</textarea><div class='form-help'>One selector per line. Use <code>register</code> for all sources, or <code>source:register</code> for exact source (<code>fc03</code>, <code>fc04</code>, <code>h41_33</code>, <code>h41_x</code>).</div></div>";

    page += "</div>";

    // RS485 / Modbus + Pins card
    page += "<div class='card'><h2>&#x1F50C; RS485 / Modbus</h2>";
    page += "<div class='settings-section'><h3 class='settings-subtitle'>&#x1F4E1; Modbus</h3>";
    page += row("Baud Rate",     "rs485_baud",  String(st.rs485.baud_rate),        "number");
    page += row("Slave Address", "rs485_slave", String(st.rs485.meter_slave_addr), "number");
    page += "</div>";
    page += "<div class='settings-section'><h3 class='settings-subtitle'>&#x1F50C; Pins</h3>";
    page += row("RS485 RX",    "pin_rs485_rx",    String(st.pins.rs485_rx));
    page += row("RS485 TX",    "pin_rs485_tx",    String(st.pins.rs485_tx));
    page += row("RS485 DE/RE", "pin_rs485_de_re", String(st.pins.rs485_de_re));
    page += "</div>";
    page += "</div>";

    // Debug card
    page += "<div class='card'><h2>&#x1F41E; Debug</h2>";
    page += "<div class='form-row'><label>Serial Logging</label><select name='debug_logging'>";
    page += String("<option value='true'")  + (st.debug.logging_enabled  ? " selected" : "") + ">Enabled</option>";
    page += String("<option value='false'") + (!st.debug.logging_enabled ? " selected" : "") + ">Disabled</option>";
    page += "</select><div class='form-help'>Realtime log stream to /logs page</div></div>";
    page += "<div class='form-row'><label>Sensor Refresh Metrics</label><select name='debug_sensor_refresh_metrics'>";
    page += String("<option value='false'") + (!st.debug.sensor_refresh_metrics ? " selected" : "") + ">Disabled</option>";
    page += String("<option value='true'")  + (st.debug.sensor_refresh_metrics  ? " selected" : "") + ">Enabled</option>";
    page += "</select><div class='form-help'>Show min/avg/max update intervals on Home page</div></div>";
    page += "<div class='form-row'><label>Raw Capture</label><select name='debug_raw_frame_dump'>";
    page += String("<option value='false'") + (!st.debug.raw_frame_dump ? " selected" : "") + ">Disabled</option>";
    page += String("<option value='true'")  + (st.debug.raw_frame_dump  ? " selected" : "") + ">Enabled</option>";
    page += "</select><div class='form-help'>Capture raw frames selected by profile (serial mirror and/or raw stream)</div></div>";
    page += "<div class='form-row'><label>Capture Profile</label><select name='debug_raw_capture_profile'>";
    page += String("<option value='unknown_h41'") + (st.debug.raw_capture_profile == "unknown_h41" ? " selected" : "") + ">Unknown H41 (default)</option>";
    page += String("<option value='all_frames'") + (st.debug.raw_capture_profile == "all_frames" ? " selected" : "") + ">All Frames (no filter)</option>";
    page += "</select><div class='form-help'>unknown_h41: only unknown FC41 frames. all_frames: capture every frame type (REQ/RSP/EXC/UNKNOWN).</div></div>";
    page += "<h3 style='color:#b0b0b0;font-size:.95em;margin:14px 0 6px'>Raw Stream Export</h3>";
    page += "<div class='form-row'><label>Enable Stream</label><select name='raw_stream_enabled'>";
    page += String("<option value='false'") + (!st.raw_stream.enabled ? " selected" : "") + ">Disabled</option>";
    page += String("<option value='true'")  + (st.raw_stream.enabled  ? " selected" : "") + ">Enabled</option>";
    page += "</select><div class='form-help'>Stream captured raw records to TCP collector</div></div>";
    page += row("Collector Host", "raw_stream_host", st.raw_stream.host, "text", "Collector listener IP or hostname");
    page += row("Collector Port", "raw_stream_port", String(st.raw_stream.port), "number");
    page += row("Queue Size (KB)", "raw_stream_queue_kb", String(st.raw_stream.queue_kb), "number", "PSRAM buffer for burst capture");
    page += row("Reconnect Delay (ms)", "raw_stream_reconnect_ms", String(st.raw_stream.reconnect_ms), "number");
    page += row("Connect Timeout (ms)", "raw_stream_connect_timeout_ms", String(st.raw_stream.connect_timeout_ms), "number");
    page += "<div class='form-row'><label>Serial Mirror</label><select name='raw_stream_serial_mirror'>";
    page += String("<option value='false'") + (!st.raw_stream.serial_mirror ? " selected" : "") + ">Stream only</option>";
    page += String("<option value='true'")  + (st.raw_stream.serial_mirror  ? " selected" : "") + ">Stream + verbose log</option>";
    page += "</select><div class='form-help'>When disabled, avoids serial/log-page overload during capture</div></div>";
    page += "</div>";

    page += "</div>"; // .grid

    // Export card
    page += "<div class='card' style='margin-top:16px'>";
    page += "<h2>&#x1F4E6; Config Backup</h2>";
    page += "<div style='display:flex;flex-wrap:wrap;gap:12px;align-items:center'>";
    page += "<button type='button' class='btn btn-primary btn-sm' onclick='exportConfig()'>&#x2B07; Export config.json</button>";
    page += "</div>";
    page += "<div class='form-help' style='margin-top:8px'>Export downloads the current on-device config as readable JSON.</div>";
    page += "</div>";

    // Buttons
    page += "<div style='text-align:center;margin-top:20px;display:flex;gap:12px;justify-content:center'>";
    page += "<button type='button' class='btn btn-primary' onclick='save()'>&#x1F4BE; Save &amp; Restart</button>";
    page += "<button type='button' class='btn btn-danger'  onclick='reboot()'>&#x1F501; Reboot</button>";
    page += "</div>";
    page += "</form>";

    // JS
page += R"(<script>
const SOURCE_TOKENS = ['fc03','fc04','h41_33','h41_x'];
const SOURCE_LABEL = {'fc03':'FC03','fc04':'FC04','h41_33':'H41-33','h41_x':'H41-X'};
const PICKER_CATALOG_SEPARATOR = '----- Catalog-Only (Known, Not Seen Yet) -----';
let KNOWN_REG_NAMES = new Set();
let DISPLAY_TO_SELECTOR = new Map();
function sourceTagToToken(src){
  const s = String(src||'').trim().toUpperCase();
  if(s === 'FC03') return 'fc03';
  if(s === 'FC04') return 'fc04';
  if(s === 'H41-33') return 'h41_33';
  if(s === 'H41-X') return 'h41_x';
  return '';
}
function normalizeSourceToken(v){
  return String(v||'').trim().toLowerCase().replace(/-/g,'_');
}
function isKnownSourceToken(v){
  return SOURCE_TOKENS.includes(normalizeSourceToken(v));
}
function selectorSource(v){
  const raw = String(v||'').trim();
  const idx = raw.indexOf(':');
  if(idx <= 0) return '';
  return normalizeSourceToken(raw.slice(0, idx));
}
function selectorRegister(v){
  const raw = String(v||'').trim();
  if(!raw) return '';
  const idx = raw.indexOf(':');
  if(idx <= 0) return raw;
  return raw.slice(idx + 1).trim();
}
function canonicalizeManualSelector(v){
  const raw = String(v||'').trim();
  if(!raw) return '';
  if(raw === PICKER_CATALOG_SEPARATOR) return '';
  if(DISPLAY_TO_SELECTOR.has(raw)) return DISPLAY_TO_SELECTOR.get(raw);
  const idx = raw.indexOf(':');
  if(idx <= 0) return raw;
  const src = normalizeSourceToken(raw.slice(0, idx));
  const reg = raw.slice(idx + 1).trim();
  if(!src || !reg) return raw;
  if(!isKnownSourceToken(src)) return raw;
  return `${src}:${reg}`;
}
function normalizeManualRegs(lines){
  const out = [];
  const seen = new Set();
  for(const raw of lines){
    const v = canonicalizeManualSelector(raw);
    if(!v) continue;
    if(seen.has(v)) continue;
    seen.add(v);
    out.push(v);
  }
  return out;
}
function resolveManualReg(v){
  return canonicalizeManualSelector(v);
}
function inferDefaultSources(regName){
  const n = String(regName||'').trim().toLowerCase();
  // When no runtime observation exists yet, prefer realistic defaults:
  // proprietary-prefixed names are typically H41-only; others are FC03-first.
  if(n.startsWith('h41_')) return ['h41_33'];
  return ['fc03'];
}
function collectObservedSources(){
  return fetch('/api/values')
    .then(r=>r.json())
    .then(data=>{
      const out = new Map();
      if(!data || typeof data !== 'object') return out;
      Object.keys(data).forEach(k=>{
        const e = data[k] || {};
        const name = String(e.name||'').trim();
        const srcTok = sourceTagToToken(e.src);
        if(!name || !srcTok) return;
        if(!out.has(name)) out.set(name, new Set());
        out.get(name).add(srcTok);
      });
      return out;
    })
    .catch(()=>new Map());
}
function addManualRegister(){
  const pick = document.getElementById('manual_reg_pick');
  const ta = document.getElementById('manual_group_registers');
  if(!pick || !ta) return;
  const v = resolveManualReg(pick.value);
  if(!v){ pick.value = ''; return; }
  const reg = selectorRegister(v);
  if(KNOWN_REG_NAMES.size > 0 && !KNOWN_REG_NAMES.has(reg)){
    alert("Unknown register name: " + reg);
    return;
  }
  const src = selectorSource(v);
  if(src && !isKnownSourceToken(src)){
    alert("Unknown source token in selector: " + src);
    return;
  }
  const regs = normalizeManualRegs(ta.value.split('\n'));
  if(!regs.includes(v)) regs.push(v);
  ta.value = regs.join('\n');
  pick.value = '';
}
function renderKnownRegisterPicker(arr, observedSourcesByName){
  KNOWN_REG_NAMES = new Set();
  DISPLAY_TO_SELECTOR = new Map();
  const dl = document.getElementById('known_register_names');
  if(dl) dl.innerHTML = '';
  const items = [];
  arr.forEach(it=>{
    if(it && typeof it === 'object' && !Array.isArray(it)){
      const name = String(it.name||'').trim();
      if(!name) return;
      const group = String(it.group||'other').trim() || 'other';
      items.push({name, group});
      return;
    }
    const name = String(it||'').trim();
    if(!name) return;
    items.push({name, group:'other'});
  });
  items.sort((a,b)=>(a.group+'|'+a.name).localeCompare(b.group+'|'+b.name));
  const runtimeOptions = [];
  const catalogOptions = [];
  items.forEach(it=>{
    KNOWN_REG_NAMES.add(it.name);
    const observedSet = observedSourcesByName ? observedSourcesByName.get(it.name) : null;
    const isObserved = !!(observedSet && observedSet.size > 0);
    let sources = isObserved
      ? SOURCE_TOKENS.filter(src=>observedSet.has(src))
      : inferDefaultSources(it.name);
    sources.forEach(src=>{
      const display = `${SOURCE_LABEL[src]} :: ${it.group} :: ${it.name}`;
      const selector = `${src}:${it.name}`;
      (isObserved ? runtimeOptions : catalogOptions).push({display, selector});
    });
  });
  let optionCount = 0;
  const appendOption = (entry)=>{
    DISPLAY_TO_SELECTOR.set(entry.display, entry.selector);
    if(!dl) return;
    const o = document.createElement('option');
    o.value = entry.display;
    o.label = entry.selector;
    dl.appendChild(o);
    optionCount++;
  };
  runtimeOptions.forEach(appendOption);
  if(dl && runtimeOptions.length && catalogOptions.length){
    const sep = document.createElement('option');
    sep.value = PICKER_CATALOG_SEPARATOR;
    sep.label = '';
    dl.appendChild(sep);
  }
  catalogOptions.forEach(appendOption);
  const hint = document.getElementById('manual_reg_hint');
  if(hint){
    hint.textContent = `Known registers: ${items.length}, selectors: ${optionCount} (${runtimeOptions.length} runtime, ${catalogOptions.length} catalog-default). Runtime selectors are listed first; after "${PICKER_CATALOG_SEPARATOR}" entries are catalog-only defaults. Picker adds exact source selectors; use plain register manually for all sources.`;
  }
}
function loadKnownRegisters(){
  fetch('/api/register_catalog')
    .then(r=>r.json())
    .then(arr=>{
      if(!Array.isArray(arr)) throw new Error('invalid register list');
      return collectObservedSources().then(obs=>renderKnownRegisterPicker(arr, obs));
    })
    .catch(()=>{
      const hint = document.getElementById('manual_reg_hint');
      if(hint) hint.textContent = 'Could not load known names. Save validation still runs on backend.';
    });
}
function collectSettings(){
  const f = document.getElementById('sf');
  const d = {wifi:{},mqtt:{},device_info:{},network:{},security:{ip_ranges:[]},rs485:{}};
  d.wifi.ssid     = f.wifi_ssid.value;
  d.wifi.password = f.wifi_password.value;
  d.network.hostname     = f.network_hostname.value;
  d.network.mdns_enabled = f.network_mdns_enabled.value === 'true';
  d.mqtt.server             = f.mqtt_server.value;
  d.mqtt.port               = parseInt(f.mqtt_port.value)||1883;
  d.mqtt.user               = f.mqtt_user.value;
  d.mqtt.password           = f.mqtt_password.value;
  d.mqtt.client_id          = f.mqtt_client_id.value;
  d.mqtt.base_topic         = f.mqtt_base_topic.value;
  d.device_info.name         = f.device_name.value;
  d.device_info.manufacturer = f.device_mfr.value;
  d.device_info.model        = f.device_model.value;
  d.rs485.baud_rate        = parseInt(f.rs485_baud.value)||9600;
  d.rs485.meter_slave_addr = parseInt(f.rs485_slave.value)||1;
  d.pins = {};
  d.pins.rs485_rx    = parseInt(f.pin_rs485_rx.value);
  d.pins.rs485_tx    = parseInt(f.pin_rs485_tx.value);
  d.pins.rs485_de_re = parseInt(f.pin_rs485_de_re.value);
  d.debug = { logging_enabled: f.debug_logging.value === 'true',
              sensor_refresh_metrics: f.debug_sensor_refresh_metrics.value === 'true',
              raw_frame_dump: f.debug_raw_frame_dump.value === 'true',
              raw_capture_profile: f.debug_raw_capture_profile.value };
  d.raw_stream = {
    enabled: f.raw_stream_enabled.value === 'true',
    host: f.raw_stream_host.value,
    port: parseInt(f.raw_stream_port.value)||9900,
    queue_kb: parseInt(f.raw_stream_queue_kb.value)||256,
    reconnect_ms: parseInt(f.raw_stream_reconnect_ms.value)||1000,
    connect_timeout_ms: parseInt(f.raw_stream_connect_timeout_ms.value)||1500,
    serial_mirror: f.raw_stream_serial_mirror.value === 'true'
  };
  d.security.auth_enabled         = f.security_auth_enabled.value === 'true';
  d.security.username             = f.security_username.value;
  d.security.password             = f.security_password.value;
  d.security.ip_whitelist_enabled = f.security_wl_enabled.value === 'true';
  d.security.ip_ranges = f.security_ip_ranges.value.split('\n').map(s=>s.trim()).filter(s=>s.length>0);
  // Publish configuration
  d.publish = {tiers:{},group_tiers:{},group_enabled:{}};
  d.publish.tiers.high   = {interval_s: parseInt(f.tier_high_s.value)||10};
  d.publish.tiers.medium = {interval_s: parseInt(f.tier_medium_s.value)||30};
  d.publish.tiers.low    = {interval_s: parseInt(f.tier_low_s.value)||60};
  document.querySelectorAll("[name^='grp_tier_']").forEach(sel=>{
    const k=sel.name.replace('grp_tier_','');
    d.publish.group_tiers[k]=sel.value;
  });
  document.querySelectorAll("[name^='grp_en_']").forEach(cb=>{
    const k=cb.name.replace('grp_en_','');
    d.publish.group_enabled[k]=cb.checked;
  });
  document.querySelectorAll("[name^='grp_tier_']").forEach(sel=>{
    const k=sel.name.replace('grp_tier_','');
    if(!(k in d.publish.group_enabled)) d.publish.group_enabled[k]=false;
  });
  const manualRegs = normalizeManualRegs(
    (f.manual_group_registers.value||'').split('\n').map(resolveManualReg)
  );
  f.manual_group_registers.value = manualRegs.join('\n');
  d.publish.manual_group = {
    enabled: f.manual_group_enabled.value === 'true',
    tier: f.manual_group_tier.value,
    registers: manualRegs
  };
  return d;
}
function save(){
  const f = document.getElementById('sf');
  const regs = normalizeManualRegs(
    (f.manual_group_registers.value||'').split('\n').map(resolveManualReg)
  );
  f.manual_group_registers.value = regs.join('\n');
  if(KNOWN_REG_NAMES.size > 0){
    const bad = regs.filter(sel=>{
      const src = selectorSource(sel);
      if(src && !isKnownSourceToken(src)) return true;
      const reg = selectorRegister(sel);
      return !KNOWN_REG_NAMES.has(reg);
    });
    if(bad.length){
      alert("Invalid manual-group selector(s):\n" + bad.join('\n'));
      return;
    }
  }
  if(!confirm('Save settings and restart?')) return;
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(collectSettings())})
  .then(r=>{if(r.ok) alert('Saved \u2014 rebooting\u2026'); else r.text().then(t=>alert('Error: '+t));});
}
function reboot(){
  if(!confirm('Reboot the device?')) return;
  fetch('/api/reboot',{method:'POST'});
}
function exportConfig(){
  fetch('/api/config/export')
    .then(r=>{
      if(!r.ok) return r.text().then(t=>{throw new Error(t || ('HTTP '+r.status));});
      return r.text();
    })
    .then(txt=>{
      let pretty = txt;
      try{
        pretty = JSON.stringify(JSON.parse(txt), null, 2) + '\r\n';
      }catch(_){
        // Keep original body when JSON parse fails unexpectedly.
      }
      const blob = new Blob([pretty],{type:'application/json;charset=utf-8'});
      const a=document.createElement('a');
      a.href=URL.createObjectURL(blob);
      a.download='config.json';
      a.click();
      URL.revokeObjectURL(a.href);
    }).catch(e=>alert('Export failed: '+e));
}
document.addEventListener('DOMContentLoaded', loadKnownRegisters);
</script>)";
    page += "</body></html>";
    ps_page_send(req, page);
}

// ============================================================
// Page: Monitoring "/monitoring"
// ============================================================
static void handle_monitoring(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    PsPage page;
    if (!page.ok()) { req->send(503, "text/plain", "Out of PSRAM"); return; }
    page += "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<title>Monitoring &#x2014; Sniffer</title><style>"; page += CSS;
    page += ".led{display:inline-block;width:11px;height:11px;border-radius:50%;margin-right:5px;vertical-align:middle}";
    page += ".status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:14px;margin-bottom:14px}";
    page += ".info-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:14px;margin-bottom:14px}";
    page += "</style></head><body>";
    page += nav_html_ps("monitoring");
    page += "<h1>&#x1F4CA; System Monitoring</h1>";
    page += "<div id='mon'><div class='card' style='color:#888'>Loading...</div></div>";
    page += R"(<script>
const LC={green:'#4caf50',cyan:'#00bcd4',yellow:'#ffb300',red:'#f44336',magenta:'#9c27b0',blue:'#2196f3',white:'#e0e0e0'};
const MONITORING_POLL_MS=15000;
function fmtUp(s){const h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sc=s%60;return h+'h '+m+'m '+sc+'s';}
function fmtB(b){if(typeof b!=='number'||!isFinite(b))return '-';return b>=1048576?(b/1048576).toFixed(1)+' MB':b>=1024?(b/1024).toFixed(0)+' KB':b+' B';}
function fmtN(n){return(typeof n==='number'&&isFinite(n))?n.toLocaleString():'-';}
function fmtP(part,total){if(typeof part!=='number'||typeof total!=='number'||!isFinite(part)||!isFinite(total)||total<=0)return '-';return((part/total)*100).toFixed(1)+'%';}
function fmtAgeS(s){return(typeof s==='number'&&isFinite(s))?fmtUp(s):'-';}
function sr(l,v){return '<div class="stat-row"><span class="stat-label">'+l+'</span><span class="stat-right"><span class="stat-value">'+v+'</span></span></div>';}
function stateBadge(ok,onLabel,offLabel){return ok?"<span class='badge badge-ok'>"+onLabel+"</span>":"<span class='badge badge-warn'>"+offLabel+"</span>";}
function badge(ok){return stateBadge(ok,'connected','offline');}
function renderMon(d){
  const led=d.led_state||'white',lc=LC[led]||'#9e9e9e';
  const ag=Array.isArray(d.mqtt_availability_groups)?d.mqtt_availability_groups:[];
  const offlineKeys=ag.filter(g=>g&&g.offline).map(g=>g.key).join(', ');
  const lfsFree=(typeof d.lfs_free==='number'&&isFinite(d.lfs_free))?d.lfs_free:((typeof d.lfs_total==='number'&&typeof d.lfs_used==='number')?Math.max(0,d.lfs_total-d.lfs_used):null);
  let html='<div class="status-grid">';
  html+='<div class="card"><h2>&#x1F4F6; Connectivity</h2>';
  html+=sr('Status',badge(d.wifi_ok))+sr('SSID',d.wifi_ssid||'-');
  html+=sr('IP',d.ip||'-')+sr('RSSI',(typeof d.rssi==='number'&&isFinite(d.rssi))?(d.rssi+' dBm'):'-');
  html+=sr('MAC','<span style="font-size:.85em">'+(d.mac||'-')+'</span>');
  html+='<div class="settings-section"><h3 class="settings-subtitle">&#x1F4E1; MQTT</h3></div>';
  html+=sr('MQTT',badge(d.mqtt_ok))+sr('Broker',(d.mqtt_broker||'-'));
  html+=sr('Client ID',(d.mqtt_client_id||'-'));
  html+='</div>';
  html+='<div class="card"><h2>&#x1F50E; MQTT Diagnostics</h2>';
  html+=sr('State Reason',d.mqtt_reason||'-');
  html+=sr('Current Session',d.mqtt_ok?fmtAgeS(d.mqtt_conn_duration_s):('disconnected for '+fmtAgeS(d.mqtt_disc_duration_s)));
  html+=sr('Reconnect Attempts',fmtN(d.mqtt_reconnect_attempts));
  html+=sr('Connect Events',fmtN(d.mqtt_connect_events));
  html+=sr('Disconnect Events',fmtN(d.mqtt_disconnect_events));
  html+=sr('Publish Attempts',fmtN(d.mqtt_publish_attempts));
  html+=sr('Publish Success',fmtN(d.mqtt_publish_success));
  html+=sr('Publish Fail (No Conn)',fmtN(d.mqtt_publish_fail_not_connected));
  html+=sr('Publish Fail (Transport)',fmtN(d.mqtt_publish_fail_transport));
  html+=sr('Last Publish OK',fmtAgeS(d.mqtt_last_publish_ok_age_s));
  html+=sr('Last Publish Fail',fmtAgeS(d.mqtt_last_publish_fail_age_s));
  html+=sr('Last Disconnect',fmtAgeS(d.mqtt_last_disconnect_age_s)+' (reason '+(d.mqtt_last_disconnect_reason!=null?d.mqtt_last_disconnect_reason:'-')+')');
  html+=sr('Avail Offline Groups',fmtN(d.mqtt_avail_groups_offline));
  html+=sr('Avail Offline Transitions',fmtN(d.mqtt_avail_offline_transitions_total));
  html+=sr('Avail Online Transitions',fmtN(d.mqtt_avail_online_transitions_total));
  html+=sr('Most Stale Group',(d.mqtt_avail_worst_group||'-')+' @ '+fmtAgeS(d.mqtt_avail_worst_age_s));
  html+=sr('Priority Group Age',fmtAgeS(d.mqtt_avail_priority_age_s)+(d.mqtt_avail_priority_offline?' (offline)':''));
  html+=sr('Offline Group Keys',offlineKeys||'-');
  html+='</div>';
  html+='<div class="card"><h2>&#x1F527; Sniffer</h2>';
  html+=sr('Frames Decoded',fmtN(d.frames_decoded))+sr('Groups Seen',fmtN(d.groups_seen));
  html+=sr('RS&#x2011;485 Baud',fmtN(d.rs485_baud))+sr('Slave Address',fmtN(d.rs485_slave));
  html+=sr('Raw Capture Profile',d.raw_capture_profile||'-');
  html+=sr('Raw Stream',d.raw_stream_enabled?(d.raw_stream_connected?'connected':'disconnected'):'disabled');
  html+=sr('Raw Stream Queue',(d.raw_stream_queued||0)+' / '+(d.raw_stream_capacity||0));
  html+=sr('Raw Stream Sent',(d.raw_stream_sent||0).toLocaleString());
  html+=sr('Raw Stream Dropped',(d.raw_stream_dropped||0).toLocaleString());
  html+=sr('Raw Stream Reconnects',(d.raw_stream_reconnects||0).toLocaleString());
  html+=sr('Raw Stream Failed Connects',(d.raw_stream_failed_connects||0).toLocaleString());
  html+='</div></div>';
  html+='<div class="info-grid">';
  html+='<div class="card"><h2>&#x1F9E0; Memory Usage</h2>';
  html+=sr('Heap Free',fmtB(d.heap_free))+sr('Heap Largest Block',fmtB(d.heap_largest));
  html+=sr('Heap Total',fmtB(d.heap_total))+sr('PSRAM Free',fmtB(d.psram_free));
  html+=sr('PSRAM Largest Block',fmtB(d.psram_largest))+sr('PSRAM Total',fmtB(d.psram_total));
  html+='</div>';
  html+='<div class="card"><h2>&#x1F4BB; System Details</h2>';
  html+=sr('Hostname',d.hostname?d.hostname+'.local':'-');
  html+=sr('Uptime',fmtUp(d.uptime_s||0))+sr('Code Date',d.code_date||'-');
  html+=sr('Chip Temp',(d.mcu_temp!=null)?d.mcu_temp.toFixed(1)+'&thinsp;&deg;C':'-');
  html+=sr('LED','<span class="led" style="background:'+lc+'"></span>'+(d.led_state||'-')+' &mdash; '+(d.led_meaning||'-'));
  html+='</div>';
  html+='<div class="card"><h2>LittleFS Information</h2>';
  html+=sr('Mount',stateBadge(d.lfs_ok,'mounted','unavailable'))+sr('Total',fmtB(d.lfs_total));
  html+=sr('Used',fmtB(d.lfs_used))+sr('Free',fmtB(lfsFree));
  html+=sr('Used Percent',fmtP(d.lfs_used,d.lfs_total));
  html+='</div></div>';
  document.getElementById('mon').innerHTML=html;
}
function pollMon(){fetch('/api/monitoring/cards?t='+Date.now(),{cache:'no-store'}).then(r=>r.json()).then(renderMon).catch(()=>{});}
pollMon();setInterval(pollMon,MONITORING_POLL_MS);
document.addEventListener('visibilitychange',()=>{if(document.visibilityState==='visible')pollMon();});
</script>)";
    page += "</body></html>";
    ps_page_send(req, page);
}

// ============================================================
// Page: Logs "/logs"
// ============================================================
static void handle_logs_page(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    PsPage page;
    if (!page.ok()) { req->send(503, "text/plain", "Out of PSRAM"); return; }
    page += "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<title>Logs &#x2014; Huawei Sniffer</title><style>"; page += CSS;
    page += ".logbox{background:#0d1117;border:1px solid #30363d;border-radius:8px;"
            "padding:14px;height:62vh;overflow-y:auto;"
            "font-family:'Courier New',Consolas,monospace;font-size:.8em;line-height:1.6}"
            ".le{white-space:pre-wrap;word-break:break-all;padding:1px 0}"
            ".lerr{color:#ff7b72}.lwarn{color:#e3b341}.lverb{color:#4d5566}.linfo{color:#79c0ff}"
            ".log-tb{display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin-bottom:12px}";
    page += "</style></head><body>";
    page += nav_html_ps("logs");
    page += "<h1>&#x1F4CB; Live Logs</h1>";
    page += "<div class='status-bar'>"
            "<span>Capture <span id='sbadge' class='badge badge-off'>Inactive</span></span>"
            "<span><b>Lines</b>&nbsp;<span id='lcnt'>0</span></span>"
            "<span><b>TTL</b>&nbsp;<span id='ttl'>&#x2014;</span></span>"
            "</div>";

    page += "<div class='log-tb'>"
            "<button id='btn-start' class='btn btn-primary btn-sm'>&#x25B6; Start</button>"
            "<button id='btn-stop'  class='btn btn-danger  btn-sm'>&#x23F9; Stop</button>"
            "<button id='btn-clear' class='btn btn-sm' style='background:#37474f'>&#x1F5D1; Clear</button>"
            "<button id='btn-dl'    class='btn btn-sm' style='background:#1b5e20' title='Save all captured lines as a text file'>&#x2B07; Download</button>"
            "<label style='display:flex;align-items:center;gap:6px;font-size:.85em;color:#b0b0b0'>"
            "<input type='checkbox' id='asc' checked>Auto-scroll</label>"
            "</div>";
    page += "<div class='logbox' id='logbox'>"
            "<div style='color:#4d5566;font-style:italic'>Press Start to begin capturing...</div>"
            "</div>";
    page += R"(<script>
let worker=null,nLines=0,allLines=[];
function cls(m){
  if(m.indexOf('[ERROR]')>=0)   return 'lerr';
  if(m.indexOf('[WARNING]')>=0) return 'lwarn';
  if(m.indexOf('[VERBOSE]')>=0) return 'lverb';
  return 'linfo';
}
function fmtMs(ms){
  return '[+'+Math.floor(ms/1000)+'.'+String(ms%1000).padStart(3,'0')+'s]';
}
function addLines(arr){
  const box=document.getElementById('logbox');
  const asc=document.getElementById('asc');
  const atBot=box.scrollTop+box.clientHeight>=box.scrollHeight-12;
  arr.forEach(l=>{
    const d=document.createElement('div');
    d.className='le '+cls(l.msg);
    const txt=(l.has_epoch?'':fmtMs(l.ts)+' ')+l.msg+(l.truncated?' [TRUNC]':'');
    d.textContent=txt;
    box.appendChild(d);
    allLines.push(txt);
    nLines++;
    document.getElementById('lcnt').textContent=nLines;
  });
  if(atBot&&asc&&asc.checked) box.scrollTop=box.scrollHeight;
}
function setSt(d){
  const a=d&&d.active;
  const b=document.getElementById('sbadge');
  b.textContent=a?'Active':'Inactive';
  b.className='badge '+(a?'badge-ok':'badge-off');
  document.getElementById('ttl').textContent=a?Math.ceil(d.remaining_ms/1000)+'s':'\u2014';
}
function mkWorker(){
  // Worker runs in a background thread - immune to Chrome tab throttling
  const src=[
    'let lastId=0,base="";',
    'function poll(){',
    '  fetch(base+"/api/logs?since="+lastId,{credentials:"same-origin"})',
    '  .then(r=>r.json()).then(d=>{',
    '    postMessage({t:"st",active:d.active,rem:d.remaining_ms||0});',
    '    if(d.dropped){lastId=0;postMessage({t:"drop"});}',
    '    if(d.lines&&d.lines.length){postMessage({t:"lines",lines:d.lines});}',
    '    if(d.latest_id>lastId)lastId=d.latest_id;',
    '    if(d.active){setTimeout(poll,1000);}',
    '    else{',
    '      postMessage({t:"inactive"});',
    '      setTimeout(function rc(){',
    '        fetch(base+"/api/logs/start",{method:"POST",credentials:"same-origin"})',
    '        .then(r=>r.json()).then(d2=>{',
    '          if(d2.ok){poll();}else{setTimeout(rc,5000);}',
    '        }).catch(()=>setTimeout(rc,5000));',
    '      },3000);',
    '    }',
    '  }).catch(()=>setTimeout(poll,2000));',
    '}',
    'self.onmessage=function(e){',
    '  if(e.data.cmd==="start"){base=e.data.base;poll();}',
    '  else if(e.data.cmd==="stop"){self.close();}',
    '};'
  ].join('\n');
  const b=new Blob([src],{type:'application/javascript'});
  const u=URL.createObjectURL(b);
  const w=new Worker(u);
  URL.revokeObjectURL(u);
  return w;
}
function attachWorker(w){
  worker=w;
  w.onmessage=function(e){
    const m=e.data;
    if(m.t==='st'){setSt({active:m.active,remaining_ms:m.rem});}
    else if(m.t==='drop'){
      const box=document.getElementById('logbox');
      const div=document.createElement('div');
      div.style.cssText='color:#ff7b72;font-weight:bold;border-top:1px solid #30363d;padding:4px 0;margin:4px 0';
      div.textContent='--- BUFFER OVERFLOW: some lines were dropped ---';
      box.appendChild(div);
    }
    else if(m.t==='lines'){addLines(m.lines);}
    else if(m.t==='inactive'){
      const b=document.getElementById('sbadge');
      b.textContent='Reconnecting\u2026';b.className='badge badge-off';
    }
  };
}
function startCapture(){
  nLines=0;allLines=[];
  document.getElementById('logbox').innerHTML='';
  document.getElementById('lcnt').textContent='0';
  if(worker){worker.terminate();worker=null;}
  fetch('/api/logs/start',{method:'POST'})
    .then(r=>r.json()).then(d=>{
      if(d.ok){
        attachWorker(mkWorker());
        worker.postMessage({cmd:'start',base:window.location.origin});
      } else if(d.busy){
        alert('Viewer active from another client ('+Math.ceil(d.remaining_ms/1000)+'s left)');
      } else {
        alert('Could not start \u2014 out of PSRAM?');
      }
    });
}
function stopCapture(){
  if(worker){worker.terminate();worker=null;}
  fetch('/api/logs/stop',{method:'POST'});
  setSt(null);
}
document.getElementById('btn-start').onclick=startCapture;
document.getElementById('btn-stop').onclick=stopCapture;
document.getElementById('btn-clear').onclick=()=>{
  document.getElementById('logbox').innerHTML='';
  nLines=0;allLines=[];document.getElementById('lcnt').textContent='0';
};
document.getElementById('btn-dl').onclick=()=>{
  if(!allLines.length){alert('No lines to download \u2014 start capture first.');return;}
  const blob=new Blob([allLines.join('\n')],{type:'text/plain'});
  const url=URL.createObjectURL(blob);
  const a=document.createElement('a');
  const ts=new Date().toISOString().replace(/[:.]/g,'-').slice(0,19);
  a.href=url;a.download='sniffer-log-'+ts+'.txt';a.click();
  setTimeout(()=>URL.revokeObjectURL(url),1000);
};
// On page load: if session already active, attach worker to resume polling
fetch('/api/logs?since=0').then(r=>r.json()).then(d=>{
  setSt(d);
  if(d.active&&d.lines&&d.lines.length)addLines(d.lines);
  if(d.active){
    attachWorker(mkWorker());
    worker.postMessage({cmd:'start',base:window.location.origin});
  }
}).catch(()=>{});
</script>)";
    page += "</body></html>";
    ps_page_send(req, page);
}

// ============================================================
// Page: OTA "/ota"
// ============================================================
static void handle_ota_page(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    PsPage page;
    if (!page.ok()) { req->send(503, "text/plain", "Out of PSRAM"); return; }
    page += "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<title>OTA &#x2014; Sniffer</title><style>"; page += CSS;
    page += ".si{display:flex;gap:6px;margin:4px 0}.si .lb{color:#b0b0b0}.si .vl{font-weight:600}";
    page += ".ota-row{display:flex;gap:10px;flex-wrap:wrap}.ota-s{flex:0 0 270px;min-width:240px}.ota-c{flex:1 1 auto}";
    page += "</style></head><body>";
    page += nav_html_ps("ota");
    page += "<h1>&#x1F4E1; OTA Updates</h1>";
    page += "<div class='card'>";
    page += "<button id='armBtn' class='btn btn-primary' type='button'>Arm OTA Window</button>";
    page += "<div id='actSt' class='muted' style='margin-top:10px'>Status: idle</div></div>";
    page += "<div class='ota-row'>";
    page += "<div class='card ota-s'>";
    page += "<b id='cfgHead' class='muted'>Loading...</b><br>";
    page += "<div id='cfgErr' class='err' style='display:none;margin-top:6px'></div>";
    page += "<div class='si'><span class='lb'>Port:</span><span id='oPort' class='vl'>-</span></div>";
    page += "<div class='si'><span class='lb'>Window:</span><span id='oWin' class='vl'>-</span></div>";
    page += "<div class='si'><span class='lb'>Armed:</span><span id='oArmed' class='vl'>-</span></div>";
    page += "<div class='si'><span class='lb'>Remaining:</span><span id='oRem' class='vl'>-</span></div>";
    page += "<div class='si'><span class='lb'>In progress:</span><span id='oInProg' class='vl'>-</span></div>";
    page += "<div class='si'><span class='lb'>Progress:</span><span id='oPct' class='vl'>-</span></div>";
    page += "<div class='si'><span class='lb'>Last error:</span><span id='oErr' class='vl'>-</span></div>";
    page += "<div id='ph' class='muted' style='margin-top:10px'>Live status: connecting...</div>";
    page += "</div>";
    page += "<div class='card ota-c'>";
    page += "<b>PlatformIO commands</b><br><br>";
    page += "<span class='muted'>Preferred (mDNS hostname):</span><br>";
    page += "<pre id='cmdHost'>loading...</pre>";
    page += "<span class='muted'>Fallback (direct IPv4):</span><br>";
    page += "<pre id='cmdIp'>loading...</pre>";
    page += "<span class='muted'>USB (full erase + flash):</span><br>";
    page += "<pre>pio run -t erase; pio run -t clean; pio run -t upload</pre>";
    page += "</div></div>";
    page += R"(<script>
const POLL_MS=3000;
let oFail=0;
const actEl=document.getElementById('actSt'),phEl=document.getElementById('ph');
function tx(id,v){const e=document.getElementById(id);if(e)e.textContent=v;}
function ni(v,f){const n=Number(v);return Number.isFinite(n)?String(Math.trunc(n)):f;}
function renderOta(d){
  const ok=!!d.config_valid;
  const hEl=document.getElementById('cfgHead'),eEl=document.getElementById('cfgErr');
  if(hEl){hEl.textContent=ok?'ota.json OK':'ota.json invalid';hEl.className=ok?'ok':'err';}
  if(eEl){if(ok){eEl.style.display='none';}else{eEl.style.display='block';eEl.textContent=d.config_error||'Unknown error';}}
  tx('oPort',ni(d.port,'-'));
  tx('oWin',ni(d.window_seconds,'-')==='--'?'-':ni(d.window_seconds,'-')+'s');
  tx('oArmed',d.armed===true?'yes':'no');
  tx('oRem',ni(d.remaining_s,'-')==='--'?'-':ni(d.remaining_s,'-')+'s');
  tx('oInProg',d.in_progress===true?'yes':'no');
  tx('oPct',ni(d.progress_pct,'-')==='--'?'-':ni(d.progress_pct,'-')+'%');
  tx('oErr',(d.last_error&&d.last_error.length)?d.last_error:'-');
  const host=d.host_target||'hostname.local',ip=d.ip_target||'0.0.0.0';
  tx('cmdHost','pio run -e esp32-s3-n16r8v-ota -t upload --upload-port '+host);
  tx('cmdIp',  'pio run -e esp32-s3-n16r8v-ota -t upload --upload-port '+ip);
  oFail=0; if(phEl) phEl.innerHTML='Live status: <b class="ok">connected</b>';
}
async function pollOta(){
  try{const r=await fetch('/api/ota_status?t='+Date.now(),{cache:'no-store'});
    renderOta(await r.json().catch(()=>{}));}
  catch(e){oFail++;if(phEl)phEl.innerHTML='Live status: <span class="'+(oFail<3?'warn':'err')+'">'+e.message+'</span>';}
}
document.getElementById('armBtn').addEventListener('click',async()=>{
  try{const r=await fetch('/api/ota_arm',{method:'POST'});const t=await r.text();
    actEl.textContent='Status: '+t;actEl.className=r.ok?'ok':'err';await pollOta();}
  catch(e){actEl.textContent='Status: '+e.message;actEl.className='err';}
});
setInterval(pollOta,POLL_MS);
document.addEventListener('visibilitychange',()=>{if(document.visibilityState==='visible')pollOta();});
pollOta();
</script>)";
    page += "</body></html>";
    ps_page_send(req, page);
}

// ============================================================
// API: GET /api/monitoring/cards Ã¢â‚¬â€ system health JSON
// ============================================================
static void handle_api_monitoring_cards(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    JsonDocument doc;
    const bool wifiOk = WiFi.isConnected();
    doc["wifi_ok"]      = wifiOk;
    doc["ip"]           = WiFi.localIP().toString();
    doc["mac"]          = WiFi.macAddress();
    doc["wifi_ssid"]    = WiFi.SSID();
    doc["hostname"]     = s_cfg ? s_cfg->getSettings().network.hostname : "unknown";
    doc["uptime_s"]     = (millis() - s_boot_ms) / 1000;
    doc["heap_free"]    = (uint32_t)ESP.getFreeHeap();
    doc["heap_total"]   = (uint32_t)ESP.getHeapSize();
    doc["heap_largest"] = (uint32_t)ESP.getMaxAllocHeap();
    doc["psram_free"]   = (uint32_t)ESP.getFreePsram();
    doc["psram_total"]  = (uint32_t)ESP.getPsramSize();
    doc["psram_largest"] = (uint32_t)ESP.getMaxAllocPsram();
    doc["rssi"]         = WiFi.RSSI();
    doc["mqtt_ok"]      = s_mqtt && s_mqtt->isConnected();
    doc["mqtt_reason"]  = s_mqtt ? s_mqtt->getStateReason() : "not initialised";
    doc["mqtt_conn_duration_s"] = s_mqtt ? (uint32_t)(s_mqtt->getConnectionDuration() / 1000UL) : 0;
    doc["mqtt_disc_duration_s"] = s_mqtt ? (uint32_t)(s_mqtt->getDisconnectionDuration() / 1000UL) : 0;
    doc["build_ts"]     = BUILD_TIMESTAMP;
    doc["code_date"]    = CODE_DATE;
    bool lfsOk = LittleFS.begin(false);
    uint32_t lfsTotal = 0;
    uint32_t lfsUsed = 0;
    if (lfsOk) {
        lfsTotal = (uint32_t)LittleFS.totalBytes();
        lfsUsed = (uint32_t)LittleFS.usedBytes();
    }
    doc["lfs_ok"] = lfsOk;
    doc["lfs_total"] = lfsTotal;
    doc["lfs_used"] = lfsUsed;
    doc["lfs_free"] = (lfsTotal >= lfsUsed) ? (lfsTotal - lfsUsed) : 0;
    if (s_cfg) {
        const Settings& st = s_cfg->getSettings();
        if (!wifiOk && st.wifi.ssid.length() > 0) doc["wifi_ssid"] = st.wifi.ssid;
        doc["mqtt_broker"]    = st.mqtt.server + ":" + String(st.mqtt.port);
        doc["mqtt_client_id"] = st.mqtt.client_id;
        doc["rs485_baud"]     = st.rs485.baud_rate;
        doc["rs485_slave"]    = st.rs485.meter_slave_addr;
        doc["raw_capture_profile"] = st.debug.raw_capture_profile;
    }
    doc["frames_decoded"] = (uint32_t)g_frames_decoded;
    int seen = 0;
    for (int i = 0; i < (int)GRP_COUNT; i++) if (s_seen[i]) seen++;
    doc["groups_seen"]  = seen;
    RawFrameStreamStats rs{};
    raw_frame_streamer_get_stats(&rs);
    doc["raw_stream_enabled"] = rs.enabled;
    doc["raw_stream_connected"] = rs.connected;
    doc["raw_stream_queued"] = rs.queued_frames;
    doc["raw_stream_capacity"] = rs.queue_capacity;
    doc["raw_stream_sent"] = rs.sent_frames;
    doc["raw_stream_dropped"] = rs.dropped_frames;
    doc["raw_stream_failed_connects"] = rs.failed_connects;
    doc["raw_stream_reconnects"] = rs.reconnect_count;

    MQTTManager::Diagnostics mqttDiag{};
    if (s_mqtt) s_mqtt->getDiagnostics(mqttDiag);
    doc["mqtt_reconnect_attempts"] = mqttDiag.reconnect_attempts;
    doc["mqtt_connect_events"] = mqttDiag.connect_events;
    doc["mqtt_disconnect_events"] = mqttDiag.disconnect_events;
    doc["mqtt_publish_attempts"] = mqttDiag.publish_attempts;
    doc["mqtt_publish_success"] = mqttDiag.publish_success;
    doc["mqtt_publish_fail_not_connected"] = mqttDiag.publish_fail_not_connected;
    doc["mqtt_publish_fail_transport"] = mqttDiag.publish_fail_transport;
    doc["mqtt_last_disconnect_reason"] = mqttDiag.last_disconnect_reason;
    if (mqttDiag.last_connect_ms > 0) {
        doc["mqtt_last_connect_age_s"] = (uint32_t)((millis() - mqttDiag.last_connect_ms) / 1000UL);
    }
    if (mqttDiag.last_disconnect_ms > 0) {
        doc["mqtt_last_disconnect_age_s"] = (uint32_t)((millis() - mqttDiag.last_disconnect_ms) / 1000UL);
    }
    if (mqttDiag.last_publish_ok_ms > 0) {
        doc["mqtt_last_publish_ok_age_s"] = (uint32_t)((millis() - mqttDiag.last_publish_ok_ms) / 1000UL);
    }
    if (mqttDiag.last_publish_fail_ms > 0) {
        doc["mqtt_last_publish_fail_age_s"] = (uint32_t)((millis() - mqttDiag.last_publish_fail_ms) / 1000UL);
    }

    MQTTAvailabilitySnapshot av{};
    mqtt_get_availability_snapshot(&av);
    uint32_t totalOnlineTransitions = 0;
    uint32_t totalOfflineTransitions = 0;
    uint32_t offlineGroups = 0;
    int worstGroup = -1;
    uint32_t worstAgeSec = 0;
    JsonArray groupArr = doc["mqtt_availability_groups"].to<JsonArray>();
    const uint32_t nowMs = millis();
    for (int g = 0; g < GRP_COUNT; g++) {
        if (s_cfg && !s_cfg->getSettings().publish.group_enabled[g]) continue;
        const MQTTAvailabilityGroupStats& gs = av.groups[g];
        totalOnlineTransitions += gs.online_transitions;
        totalOfflineTransitions += gs.offline_transitions;
        if (gs.offline) offlineGroups++;
        uint32_t ageSec = 0;
        bool hasAge = false;
        if (gs.last_seen_ms > 0) {
            hasAge = true;
            ageSec = (uint32_t)((nowMs - gs.last_seen_ms) / 1000UL);
            if (ageSec >= worstAgeSec) {
                worstAgeSec = ageSec;
                worstGroup = g;
            }
        }

        JsonObject o = groupArr.add<JsonObject>();
        o["key"] = GROUP_INFO[g].key;
        o["label"] = GROUP_INFO[g].label;
        o["offline"] = gs.offline;
        o["online_transitions"] = gs.online_transitions;
        o["offline_transitions"] = gs.offline_transitions;
        if (hasAge) o["age_s"] = ageSec;
        if (gs.last_online_ms > 0) o["last_online_age_s"] = (uint32_t)((nowMs - gs.last_online_ms) / 1000UL);
        if (gs.last_offline_ms > 0) o["last_offline_age_s"] = (uint32_t)((nowMs - gs.last_offline_ms) / 1000UL);
    }
    doc["mqtt_avail_online_transitions_total"] = totalOnlineTransitions;
    doc["mqtt_avail_offline_transitions_total"] = totalOfflineTransitions;
    doc["mqtt_avail_groups_offline"] = offlineGroups;
    if (worstGroup >= 0) {
        doc["mqtt_avail_worst_group"] = GROUP_INFO[worstGroup].key;
        doc["mqtt_avail_worst_age_s"] = worstAgeSec;
    }
    const MQTTAvailabilityGroupStats& pg = av.groups[(int)GRP_PRIORITY_MANUAL];
    if (pg.last_seen_ms > 0) {
        doc["mqtt_avail_priority_age_s"] = (uint32_t)((nowMs - pg.last_seen_ms) / 1000UL);
    }
    doc["mqtt_avail_priority_offline"] = pg.offline;

    const float mcuT = temperatureRead();
    if (!isnan(mcuT)) doc["mcu_temp"] = mcuT;
    doc["led_state"]    = led_state_str();
    doc["led_meaning"]  = led_meaning_str();
    if (s_cfg) {
        const Settings& st = s_cfg->getSettings();
        if (st.network.mdns_enabled && st.network.hostname.length() > 0)
            doc["host_target"] = st.network.hostname + ".local";
    }
    doc["ip_target"] = WiFi.localIP().toString();
    String body;
    serializeJson(doc, body);
    AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", body);
    resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    req->send(resp);
}

// ============================================================
// API: GET /api/status Ã¢â‚¬â€ JSON for dashboard polling
// ============================================================
static void handle_api_status(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    JsonDocument doc;
    doc["ip"]       = WiFi.localIP().toString();
    doc["hostname"] = s_cfg ? s_cfg->getSettings().network.hostname : "unknown";
    doc["uptime_s"] = (millis() - s_boot_ms) / 1000;
    doc["heap"]     = esp_get_free_heap_size();
    doc["rssi"]     = WiFi.RSSI();
    doc["build_ts"] = BUILD_TIMESTAMP;
    doc["code_date"] = CODE_DATE;
    doc["mqtt_ok"]      = s_mqtt ? s_mqtt->isConnected() : false;
    doc["mqtt_reason"]  = s_mqtt ? s_mqtt->getStateReason() : "not initialised";
    doc["frames"]       = (uint32_t)g_frames_decoded;
    JsonArray grps = doc["groups"].to<JsonArray>();
    for (int i = 0; i < (int)GRP_COUNT; i++) {
        if (!s_seen[i]) continue;
        JsonObject o = grps.add<JsonObject>();
        o["key"]         = GROUP_INFO[i].key;
        o["label"]       = GROUP_INFO[i].label;
        o["description"] = GROUP_INFO[i].description;
        o["enabled"]     = group_is_enabled((RegGroup)i);
    }
    String body;
    serializeJson(doc, body);
    req->send(200, "application/json", body);
}

// ============================================================
// API: GET /api/config Ã¢â‚¬â€ returns settings JSON (no passwords)
// ============================================================
static void handle_api_config_get(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    if (!s_cfg) { req->send(500, "text/plain", "Not ready"); return; }
    req->send(200, "application/json", s_cfg->getSettingsJson());
}

// ============================================================
// API: POST /api/config Ã¢â‚¬â€ save settings, then reboot
// ============================================================
static void handle_api_config_post(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
    if (!check_auth(req)) return;
    if (!s_cfg) { req->send(500, "text/plain", "Not ready"); return; }
    // Accumulate body chunks Ã¢â‚¬â€ ESPAsyncWebServer may call this once per TCP segment.
    // Using statics is safe: async server processes requests sequentially at app level.
    static String s_post_body;
    static size_t s_post_total;
    if (index == 0) {
        s_post_body  = "";
        s_post_total = total;
        s_post_body.reserve(total + 1);
    }
    s_post_body += String((char*)data, len);
    if (index + len < s_post_total) return;   // more chunks pending
    static constexpr size_t MAX_CONFIG_BODY = 8192;
    if (s_post_body.length() > MAX_CONFIG_BODY) {
        req->send(413, "text/plain", "Request body too large");
        return;
    }
    if (s_cfg->updateSettingsFromJson(s_post_body)) {
        req->send(200, "text/plain", "OK");
        scheduleRestart();  // deferred Ã¢â‚¬â€ lets response flush before reboot
    } else {
        String err = s_cfg->getLastError();
        if (err.length() == 0) err = "Save failed";
        req->send(400, "text/plain", err);
    }
}

// ============================================================
// API: POST /api/reboot
// ============================================================
static void handle_api_reboot(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    req->send(200, "text/plain", "Rebooting...");
    scheduleRestart();  // deferred Ã¢â‚¬â€ lets response flush before reboot
}

// ============================================================
// API: GET /api/values Ã¢â‚¬â€ last decoded register values
// ============================================================
static void handle_api_values(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    req->send(200, "application/json", mqtt_get_last_values_json());
}

// ============================================================
// API: GET /api/priority_values — cached values in manual priority group
// ============================================================
static void handle_api_priority_values(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    AsyncWebServerResponse* resp =
        req->beginResponse(200, "application/json", mqtt_get_priority_values_json());
    resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    req->send(resp);
}

// ============================================================
// API: GET /api/register_catalog — unique known register names + group metadata
// ============================================================
static void handle_api_register_catalog(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    AsyncWebServerResponse* resp =
        req->beginResponse(200, "application/json", huawei_decoder_get_known_register_catalog_json());
    resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    req->send(resp);
}

// ============================================================
// API: GET /api/live_values?since=<seq> — live modbus state delta
// ============================================================
static void handle_api_live_values(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    uint32_t since = 0;
    if (req->hasParam("since")) {
        since = (uint32_t)req->getParam("since")->value().toInt();
    }
    AsyncWebServerResponse* resp =
        req->beginResponse(200, "application/json", live_value_store_get_json(since));
    resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    req->send(resp);
}

// ============================================================
// API: POST /api/ota_arm Ã¢â‚¬â€ arm the OTA update window
// ============================================================
static void handle_api_ota_arm(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    if (!s_ota) { req->send(503, "text/plain", "OTA not available"); return; }
    String err;
    if (s_ota->armFromWeb(err)) {
        OtaStatusSnapshot s = s_ota->snapshot();
        req->send(200, "text/plain",
            String("OTA armed for ") + String(s.windowSeconds) + "s on port " + String(s.port));
    } else {
        req->send(400, "text/plain", "OTA arm failed: " + err);
    }
}

// ============================================================
// API: GET /api/ota_status Ã¢â‚¬â€ current OTA state
// ============================================================
static void handle_api_ota_status(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    JsonDocument doc;
    if (s_ota) {
        OtaStatusSnapshot s = s_ota->snapshot();
        doc["config_valid"]      = s.configValid;
        doc["config_error"]      = s.configError;
        doc["armed"]             = s.armed;
        doc["remaining_s"]       = (unsigned long)s.remainingSeconds;
        doc["in_progress"]       = s.inProgress;
        doc["progress_pct"]      = s.progressPercent;
        doc["command"]           = s.command;
        doc["last_error"]        = s.lastError;
        doc["port"]              = s.port;
    } else {
        doc["config_valid"] = false;
        doc["config_error"] = "OTA manager not ready";
    }
    String body;
    serializeJson(doc, body);
    req->send(200, "application/json", body);
}

// ============================================================
// API: GET /api/config/export Ã¢â‚¬â€ download pretty config.json
// ============================================================
static void handle_api_config_export(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    if (!s_cfg) { req->send(500, "text/plain", "ConfigManager not set"); return; }
    String body = s_cfg->getSettingsJsonPretty(false);
    if (body.length() == 0) { req->send(500, "text/plain", "Failed to build config export"); return; }
    AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", body);
    resp->addHeader("Content-Disposition", "attachment; filename=\"config.json\"");
    resp->addHeader("Cache-Control", "no-store");
    req->send(resp);
}

// ============================================================
// Favicon (inline SVG Ã¢â‚¬â€ solar panel icon in brand blue)
// ============================================================
static void handle_favicon(AsyncWebServerRequest* req) {
    static const char* svg =
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
        "<rect width='64' height='64' rx='12' fill='#1e1e1e'/>"
        "<rect x='8' y='22' width='48' height='28' rx='4' fill='#2196f3'/>"
        "<line x1='8' y1='36' x2='56' y2='36' stroke='#1565c0' stroke-width='2'/>"
        "<line x1='24' y1='22' x2='24' y2='50' stroke='#1565c0' stroke-width='2'/>"
        "<line x1='40' y1='22' x2='40' y2='50' stroke='#1565c0' stroke-width='2'/>"
        "<line x1='32' y1='8' x2='32' y2='18' stroke='#ffd54f' stroke-width='2'/>"
        "<line x1='20' y1='11' x2='24' y2='18' stroke='#ffd54f' stroke-width='2'/>"
        "<line x1='44' y1='11' x2='40' y2='18' stroke='#ffd54f' stroke-width='2'/>"
        "</svg>";
    AsyncWebServerResponse* resp = req->beginResponse(200, "image/svg+xml", svg);
    resp->addHeader("Cache-Control", "public, max-age=86400");
    req->send(resp);
}

// ============================================================
// Helpers: JSON-escape a raw buffer into a PsPage
// ============================================================
static void appendJsonEscaped(PsPage& out, const char* data, uint16_t len) {
    char chunk[66]; int ci = 0;
    for (uint16_t i = 0; i <= len; i++) {
        if (ci >= 62 || i == len) {
            if (ci > 0) { chunk[ci] = '\0'; out += chunk; ci = 0; }
            if (i == len) break;
        }
        const char c = data[i];
        if      (c == '"')  { chunk[ci++]='\\'; chunk[ci++]='"';  }
        else if (c == '\\') { chunk[ci++]='\\'; chunk[ci++]='\\'; }
        else if (c == '\n') { chunk[ci++]='\\'; chunk[ci++]='n';  }
        else if (c == '\r') {}
        else if (c >= 0x20) { chunk[ci++] = c; }
    }
}

// ============================================================
// API: GET /api/logs Ã¢â‚¬â€ poll for new log lines (also keeps TTL alive)
// ============================================================
static void handle_api_logs_get(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    uint32_t sinceId = 0;
    if (req->hasParam("since"))
        sinceId = (uint32_t)req->getParam("since")->value().toInt();

    IPAddress ip = req->client()->remoteIP();
    DebugLogBuffer::keepAlive(ip, 1800000UL);     // 30-min TTL: survives heavy browser throttling

    const bool     active  = DebugLogBuffer::isEnabled();
    const uint32_t remMs   = DebugLogBuffer::remainingMs();

    constexpr size_t kMaxLines = 50;
    DebugLogBuffer::LineOut lout[kMaxLines];
    bool     dropped  = false;
    size_t   n        = 0;
    uint32_t latestId = DebugLogBuffer::latestId();

    if (active)
        n = DebugLogBuffer::readSince(sinceId, kMaxLines, 8192, lout, dropped);
    if (n > 0) latestId = lout[n - 1].id;

    PsPage body(16384);
    char tmp[64];
    body += "{\"active\":";     body += active  ? "true" : "false";
    body += ",\"remaining_ms\":";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)remMs);   body += tmp;
    body += ",\"latest_id\":";
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)latestId); body += tmp;
    body += ",\"dropped\":";    body += dropped ? "true" : "false";
    body += ",\"lines\":[";

    for (size_t i = 0; i < n; i++) {
        if (i > 0) body += ",";
        snprintf(tmp, sizeof(tmp), "{\"id\":%lu,\"ts\":%lu,\"has_epoch\":",
                 (unsigned long)lout[i].id, (unsigned long)lout[i].ts);
        body += tmp;
        body += lout[i].hasEpoch ? "true" : "false";
        body += ",\"truncated\":";
        body += lout[i].truncated ? "true" : "false";
        body += ",\"msg\":\"";
        if (lout[i].msg && lout[i].msgLen > 0)
            appendJsonEscaped(body, lout[i].msg, lout[i].msgLen);
        body += "\"}";
    }
    body += "]}";
    req->send(200, "application/json", body.c_str());
}

// ============================================================
// API: POST /api/logs/start Ã¢â‚¬â€ arm capture session for this IP
// ============================================================
static void handle_api_logs_start(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    uint32_t remMs = 0;
    auto result = DebugLogBuffer::enableFor(req->client()->remoteIP(), 1800000UL, remMs); // 30-min TTL
    char buf[80];
    if (result == DebugLogBuffer::EnableResult::Ok) {
        snprintf(buf, sizeof(buf), "{\"ok\":true,\"remaining_ms\":%lu}", (unsigned long)remMs);
    } else if (result == DebugLogBuffer::EnableResult::Busy) {
        snprintf(buf, sizeof(buf), "{\"ok\":false,\"busy\":true,\"remaining_ms\":%lu}", (unsigned long)remMs);
    } else {
        snprintf(buf, sizeof(buf), "{\"ok\":false,\"busy\":false,\"remaining_ms\":0}");
    }
    req->send(200, "application/json", buf);
}

// ============================================================
// API: POST /api/logs/stop Ã¢â‚¬â€ release capture session
// ============================================================
static void handle_api_logs_stop(AsyncWebServerRequest* req) {
    if (!check_auth(req)) return;
    DebugLogBuffer::disableIfOwner(req->client()->remoteIP());
    req->send(200, "application/json", "{\"ok\":true}");
}

// ============================================================
// Public init
// ============================================================
void web_ui_init(ConfigManager* cfg, MQTTManager* mqtt, OTAManager* ota, bool ap_mode) {
    s_cfg     = cfg;
    s_mqtt    = mqtt;
    s_ota     = ota;
    s_ap_mode = ap_mode;
    s_boot_ms = millis();

    // Configure IP whitelist from settings
    if (cfg) {
        const Settings& st = cfg->getSettings();
        s_wl.setEnabled(st.security.ip_whitelist_enabled);
        for (const auto& r : st.security.ip_ranges) s_wl.addIPRange(r);
    }

    s_server = new AsyncWebServer(80);

    s_server->on("/",               HTTP_GET,  handle_root);
    s_server->on("/priority",        HTTP_GET,  handle_priority_page);
    s_server->on("/live",            HTTP_GET,  handle_live_page);
    s_server->on("/monitoring",      HTTP_GET,  handle_monitoring);
    s_server->on("/settings",        HTTP_GET,  handle_settings);
    s_server->on("/ota",             HTTP_GET,  handle_ota_page);
    s_server->on("/favicon.svg",     HTTP_GET,  handle_favicon);
    s_server->on("/favicon.ico",     HTTP_GET,  handle_favicon);

    s_server->on("/api/status",           HTTP_GET,  handle_api_status);
    s_server->on("/api/config",           HTTP_GET,  handle_api_config_get);
    s_server->on("/api/config/export",    HTTP_GET,  handle_api_config_export);
    s_server->on("/api/reboot",           HTTP_POST, handle_api_reboot);
    s_server->on("/api/values",           HTTP_GET,  handle_api_values);
    s_server->on("/api/priority_values",  HTTP_GET,  handle_api_priority_values);
    s_server->on("/api/register_catalog", HTTP_GET,  handle_api_register_catalog);
    s_server->on("/api/live_values",      HTTP_GET,  handle_api_live_values);
    s_server->on("/api/ota_arm",          HTTP_POST, handle_api_ota_arm);
    s_server->on("/api/ota_status",       HTTP_GET,  handle_api_ota_status);
    s_server->on("/api/monitoring/cards", HTTP_GET,  handle_api_monitoring_cards);

    // Body-bearing POST handlers
    s_server->on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* r){},
        nullptr,
        handle_api_config_post);

    s_server->on("/logs",           HTTP_GET,  handle_logs_page);
    s_server->on("/api/logs",       HTTP_GET,  handle_api_logs_get);
    s_server->on("/api/logs/start", HTTP_POST, handle_api_logs_start);
    s_server->on("/api/logs/stop",  HTTP_POST, handle_api_logs_stop);

    s_server->onNotFound([](AsyncWebServerRequest* r) {
        r->send(404, "text/html; charset=UTF-8",
            "<!DOCTYPE html><html><head><title>Not Found</title>"
            "<style>body{background:#1e1e1e;color:#fff;font-family:'Segoe UI',sans-serif;padding:60px 20px}"
            "h1{color:#f44336} a{color:#64b5f6}</style></head><body>"
            "<h1>404 Not Found</h1><p>The requested URL was not found.</p>"
            "<p><a href='/'>&#x2190; Home</a></p></body></html>");
    });

    s_server->begin();
    UnifiedLogger::info("[WEB] started Ã¢â‚¬â€ http://%s/\n",
                       cfg ? cfg->getSettings().network.hostname.c_str() : "?");
}

void web_ui_loop() {
    processPendingRestart();
    DebugLogBuffer::isEnabled(); // drives internal TTL expiry
}
