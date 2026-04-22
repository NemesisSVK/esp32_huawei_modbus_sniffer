#ifndef IPWHITELISTMANAGER_H
#define IPWHITELISTMANAGER_H

#include <Arduino.h>
#include <vector>
#include <IPAddress.h>

/**
 * IPWhitelistManager — IP-based access control for the web interface.
 * Range format: "192.168.1.100-15"  = IPs 192.168.1.100 through 192.168.1.115
 * Single IP:    "192.168.1.50"
 */
class IPWhitelistManager {
public:
    IPWhitelistManager();
    ~IPWhitelistManager();

    bool setEnabled(bool enabled);
    bool addIPRange(const String& range);
    bool isEnabled() const;
    const std::vector<String>& getIPRanges() const;

    bool isIPWhitelisted(const String& clientIP) const;
    bool isIPWhitelisted(const IPAddress& clientIP) const;

    static bool isValidIPv4(const String& ip);
    static bool isValidIPRange(const String& range);
    static bool isIPInRange(const String& ip, const String& range);
    static bool parseIPv4(const String& ip, uint8_t octets[4]);
    static bool parseIPRange(const String& range, uint8_t base[4], uint8_t& suffix);
    static bool compareIPs(const uint8_t ip1[4], const uint8_t ip2[4]);

private:
    bool enabled;
    std::vector<String> ipRanges;

    static bool checkIPInRangeOctets(const uint8_t ip[4], const uint8_t base[4], uint8_t suffix);
    bool isIPInRangeInternal(const uint8_t clientIP[4], const String& range) const;

    static const size_t MAX_IP_RANGES = 50;
    static const char* const RANGE_SEPARATOR;
};

#endif // IPWHITELISTMANAGER_H
