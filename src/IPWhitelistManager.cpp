#include "IPWhitelistManager.h"
#include <algorithm>

const char* const IPWhitelistManager::RANGE_SEPARATOR = "-";

IPWhitelistManager::IPWhitelistManager() : enabled(false) {}
IPWhitelistManager::~IPWhitelistManager() {}

bool IPWhitelistManager::setEnabled(bool enabled) {
    this->enabled = enabled;
    return true;
}

bool IPWhitelistManager::addIPRange(const String& range) {
    if (!isValidIPRange(range)) return false;
    if (ipRanges.size() >= MAX_IP_RANGES) return false;
    if (std::any_of(ipRanges.begin(), ipRanges.end(),
        [&](const String& e){ return e == range; })) return true;
    ipRanges.push_back(range);
    return true;
}

bool IPWhitelistManager::isEnabled() const { return enabled; }
const std::vector<String>& IPWhitelistManager::getIPRanges() const { return ipRanges; }

bool IPWhitelistManager::isIPWhitelisted(const String& clientIP) const {
    if (!enabled || ipRanges.empty()) return true;
    uint8_t o[4];
    if (!parseIPv4(clientIP, o)) return false;
    return isIPWhitelisted(IPAddress(o[0], o[1], o[2], o[3]));
}

bool IPWhitelistManager::isIPWhitelisted(const IPAddress& clientIP) const {
    if (!enabled || ipRanges.empty()) return true;
    uint8_t o[4] = { clientIP[0], clientIP[1], clientIP[2], clientIP[3] };
    return std::any_of(ipRanges.begin(), ipRanges.end(),
        [&](const String& r){ return isIPInRangeInternal(o, r); });
}

bool IPWhitelistManager::isValidIPv4(const String& ip) {
    uint8_t o[4]; return parseIPv4(ip, o);
}

bool IPWhitelistManager::isValidIPRange(const String& range) {
    if (range.length() == 0) return false;
    if (range.indexOf(RANGE_SEPARATOR) == -1) return isValidIPv4(range);
    int dp = range.indexOf(RANGE_SEPARATOR);
    if (dp <= 0 || dp >= (int)range.length()-1) return false;
    uint8_t b[4];
    if (!parseIPv4(range.substring(0, dp), b)) return false;
    int s = range.substring(dp+1).toInt();
    if (s < 1 || s > 255) return false;
    if (b[3] + s > 255) return false;
    return true;
}

bool IPWhitelistManager::isIPInRange(const String& ip, const String& range) {
    uint8_t i[4], b[4]; uint8_t s;
    if (!parseIPv4(ip,i) || !parseIPRange(range,b,s)) return false;
    return checkIPInRangeOctets(i,b,s);
}

bool IPWhitelistManager::parseIPv4(const String& ip, uint8_t octets[4]) {
    int cnt=0; String cur="";
    for (size_t i=0; i<ip.length(); i++) {
        char c=ip[i];
        if (c=='.') {
            if (!cur.length()) return false;
            int v=cur.toInt(); if (v<0||v>255) return false;
            octets[cnt++]=(uint8_t)v; cur="";
            if (cnt>3) return false;
        } else if (c>='0'&&c<='9') {
            cur+=c; if (cur.length()>3) return false;
        } else return false;
    }
    if (!cur.length()) return false;
    int v=cur.toInt(); if (v<0||v>255) return false;
    octets[cnt++]=(uint8_t)v;
    return cnt==4;
}

bool IPWhitelistManager::parseIPRange(const String& range, uint8_t base[4], uint8_t& suffix) {
    int dp=range.indexOf(RANGE_SEPARATOR);
    if (dp==-1) { suffix=0; return parseIPv4(range,base); }
    if (!parseIPv4(range.substring(0,dp),base)) return false;
    int s=range.substring(dp+1).toInt();
    if (s<1||s>255) return false;
    suffix=(uint8_t)s; return true;
}

bool IPWhitelistManager::compareIPs(const uint8_t a[4], const uint8_t b[4]) {
    return a[0]==b[0]&&a[1]==b[1]&&a[2]==b[2]&&a[3]==b[3];
}

bool IPWhitelistManager::checkIPInRangeOctets(const uint8_t ip[4],
                                               const uint8_t base[4], uint8_t suffix) {
    if (ip[0]!=base[0]||ip[1]!=base[1]||ip[2]!=base[2]) return false;
    return ip[3]>=base[3] && ip[3]<=(base[3]+suffix);
}

bool IPWhitelistManager::isIPInRangeInternal(const uint8_t ip[4], const String& range) const {
    uint8_t b[4]; uint8_t s;
    if (!parseIPRange(range,b,s)) return false;
    return checkIPInRangeOctets(ip,b,s);
}
