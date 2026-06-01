#!/usr/bin/env python3
"""Generate oui.h: embedded OUI vendor table + SD loader."""

import urllib.request
import sys

NMAP_URL = "https://svn.nmap.org/nmap/nmap-mac-prefixes"

# ~300 most common BLE/WiFi-relevant vendors
KEYWORDS = [
    "Apple", "Samsung", "Google", "Huawei", "Xiaomi", "OnePlus", "Oppo",
    "Sony", "LG", "Nokia", "Ericsson", "Motorola", "Lenovo",
    "Intel", "Qualcomm", "Broadcom", "MediaTek", "Realtek", "Espressif",
    "Nordic", "Texas Instr", "Microchip", "STMicro", "NXP", "Cypress",
    "Cisco", "TP-LINK", "Netgear", "D-Link", "ASUSTek", "Linksys", "Belkin",
    "Aruba", "Ubiquiti", "MikroTik", "Zyxel", "Ruckus",
    "Dell", "HP ", "Hewlett-Packard", "Microsoft", "Raspberry Pi",
    "Garmin", "Fitbit", "Bose", "JBL", "Sennheiser", "Jabra", "Plantronics",
    "Panasonic", "Philips", "Hikvision",
    "Amazon", "Sonos", "Roku",
    "Canon", "Epson", "Brother",
    "Nintendo", "Logitech", "Anker",
    "Wyze", "Ring", "Arlo", "Nest Labs", "Honeywell",
    "Universal Electronics", "Liteon", "Foxconn", "Wistron",
    "Pegatron", "Compal", "Quanta", "Inventec",
    "Murata", "Taiyo Yuden", "TDK", "Kyocera",
    "Silicon Labs", "Renesas", "Infineon", "Dialog",
    "Marvell", "Atheros", "Ralink", "Zyxel",
    "Toshiba", "Samsung", "SK hynix", "Western Digital",
    "Seagate", "TP-Link", "Netgear",
]

MAX_ENTRIES = 350

def main():
    print("[OUI] Downloading nmap mac-prefixes...", file=sys.stderr)
    req = urllib.request.Request(NMAP_URL, headers={"User-Agent": "curl/8.0"})
    resp = urllib.request.urlopen(req, timeout=30)
    raw = resp.read().decode("utf-8", errors="replace")

    entries = []
    seen = set()
    for line in raw.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(None, 1)
        if len(parts) < 2:
            continue
        oui_hex, org = parts[0].strip(), parts[1].strip()
        if not any(kw.lower() in org.lower() for kw in KEYWORDS):
            continue
        key = int(oui_hex, 16)
        if key in seen:
            continue
        seen.add(key)
        entries.append((key, org))
        if len(entries) >= MAX_ENTRIES:
            break

    entries.sort(key=lambda x: x[0])

    vendor_map = {}
    vendor_strings = []
    for _, org in entries:
        if org not in vendor_map:
            vendor_map[org] = len(vendor_strings)
            vendor_strings.append(org)

    print(f"[OUI] {len(entries)} entries, {len(vendor_strings)} vendors", file=sys.stderr)

    lines = []
    lines.append("#ifndef OUI_H")
    lines.append("#define OUI_H")
    lines.append("")
    lines.append("#include <Arduino.h>")
    lines.append("#include <pgmspace.h>")
    lines.append("#include <SD.h>")
    lines.append("#include <FS.h>")
    lines.append("")
    lines.append("#define OUI_SD_PATH \"/oui.txt\"")
    lines.append("")
    lines.append("#define MAX_SD_OUIS 2000")
    lines.append("")
    lines.append("struct OUIRecord {")
    lines.append("    uint32_t oui;")
    lines.append("    uint16_t vendorIdx;")
    lines.append("};")
    lines.append("")

    # Embedded table — ensure unique vendor strings
    lines.append(f"static const char ouiVendorTable[{len(vendor_strings)}][22] PROGMEM = {{")
    for vs in vendor_strings:
        t = vs[:20] if len(vs) > 20 else vs
        lines.append(f'    "{t}",')
    lines.append("};")
    lines.append("")

    lines.append(f"static const OUIRecord ouiTable[{len(entries)}] PROGMEM = {{")
    for key, org in entries:
        idx = vendor_map[org]
        lines.append(f"    {{0x{key:06X}, {idx}}},")
    lines.append("};")
    lines.append("")
    lines.append("// Runtime SD-loaded table")
    lines.append("static OUIRecord sdTable[MAX_SD_OUIS];")
    lines.append("static int sdTableCount = 0;")
    lines.append("static char sdVendorBuf[MAX_SD_OUIS][22];")
    lines.append("static int sdVendorCount = 0;")
    lines.append("")

    lines.append("""
static uint8_t hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

static int loadOuiFromSD() {
    if (!SD.exists(OUI_SD_PATH)) return 0;
    File f = SD.open(OUI_SD_PATH);
    if (!f) return 0;
    sdTableCount = 0;
    sdVendorCount = 0;
    int lineLen = 0;
    char line[128];
    while (f.available() && sdTableCount < MAX_SD_OUIS) {
        line[lineLen] = f.read();
        if (line[lineLen] == '\\n' || lineLen >= 126) {
            line[lineLen] = '\\0';
            lineLen = 0;
            // skip comments / empty
            if (line[0] == '#' || line[0] == '\\0') continue;
            char *space = strchr(line, ' ');
            if (!space) continue;
            *space = '\\0';
            char *org = space + 1;
            uint32_t oui = strtoul(line, NULL, 16);
            if (oui == 0 && line[0] != '0') continue;
            // dedup vendor string
            int vIdx = -1;
            for (int i = 0; i < sdVendorCount; i++) {
                if (strcmp(sdVendorBuf[i], org) == 0) { vIdx = i; break; }
            }
            if (vIdx < 0 && sdVendorCount < MAX_SD_OUIS) {
                strncpy(sdVendorBuf[sdVendorCount], org, 21);
                sdVendorBuf[sdVendorCount][21] = '\\0';
                vIdx = sdVendorCount++;
            }
            if (vIdx >= 0) {
                sdTable[sdTableCount].oui = oui;
                sdTable[sdTableCount].vendorIdx = vIdx;
                sdTableCount++;
            }
        } else {
            lineLen++;
        }
    }
    f.close();
    // sort SD table by OUI
    for (int i = 1; i < sdTableCount; i++) {
        OUIRecord tmp = sdTable[i];
        int j = i - 1;
        while (j >= 0 && sdTable[j].oui > tmp.oui) {
            sdTable[j + 1] = sdTable[j];
            j--;
        }
        sdTable[j + 1] = tmp;
    }
    return sdTableCount;
}

static const char* lookupVendor(const char* mac) {
    uint32_t oui = 0;
    oui |= (uint32_t)hexval(mac[0]) << 20;
    oui |= (uint32_t)hexval(mac[1]) << 16;
    oui |= (uint32_t)hexval(mac[3]) << 12;
    oui |= (uint32_t)hexval(mac[4]) << 8;
    oui |= (uint32_t)hexval(mac[6]) << 4;
    oui |= (uint32_t)hexval(mac[7]);

    // Search embedded table first
    int lo = 0, hi = """ + str(len(entries)) + """ - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t val;
        memcpy_P(&val, &ouiTable[mid].oui, sizeof(val));
        if (val < oui) lo = mid + 1;
        else if (val > oui) hi = mid - 1;
        else {
            uint16_t idx;
            memcpy_P(&idx, &ouiTable[mid].vendorIdx, sizeof(idx));
            return ouiVendorTable[idx];
        }
    }

    // Fallback to SD table
    if (sdTableCount > 0) {
        lo = 0; hi = sdTableCount - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (sdTable[mid].oui < oui) lo = mid + 1;
            else if (sdTable[mid].oui > oui) hi = mid - 1;
            else return sdVendorBuf[sdTable[mid].vendorIdx];
        }
    }

    return nullptr;
}
""")

    lines.append("#endif")

    out = "\n".join(lines)
    with open("oui.h", "w") as f:
        f.write(out)
    print(f"[OUI] Generated oui.h ({len(out)} bytes)", file=sys.stderr)

if __name__ == "__main__":
    main()
