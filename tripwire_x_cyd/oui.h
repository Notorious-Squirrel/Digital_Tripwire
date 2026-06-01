#ifndef OUI_H
#define OUI_H

#include <Arduino.h>
#include <pgmspace.h>
#include <SD.h>
#include <FS.h>

#define OUI_SD_PATH "/oui.txt"

#define MAX_SD_OUIS 2000

struct OUIRecord {
    uint32_t oui;
    uint16_t vendorIdx;
};

static const char ouiVendorTable[139][22] PROGMEM = {
    "Cisco Systems",
    "Toshiba",
    "Seiko Epson",
    "Canon",
    "Sony Tektronix",
    "Dell EMC",
    "Western Digital",
    "Samsung Electronics",
    "Telematica Sistems I",
    "Sony",
    "Foxconn",
    "Precision Electronic",
    "Ericsson Group",
    "Canon Finetech",
    "Ericsson",
    "Compal Electronics",
    "Tilgin AB",
    "Philips CFT",
    "Samsung Electro Mech",
    "Intel",
    "Totsu Engineering",
    "NDC Infared Engineer",
    "Maschoff Design Engi",
    "Nokia Danmark A/S",
    "Seakr Engineering",
    "Infineon AG",
    "Omega Engineering",
    "Prüftechnik Conditio",
    "Taiyo Yuden",
    "Atheros Communicatio",
    "Plantronics",
    "Apple",
    "Allied Advanced Manu",
    "Microsoft",
    "ipDialog",
    "Sony Interactive Ent",
    "Intelligent Telecomm",
    "Sonos",
    "The Linksys Group",
    "Motorola Solutions",
    "Medialogic",
    "Microchip Technology",
    "Seagate Technology",
    "Hitachi Information ",
    "Japan Control Engine",
    "Northstar Engineerin",
    "Philips",
    "Garmin International",
    "D-Link Systems",
    "Asahi-Engineering",
    "Broadcom Technologie",
    "LG Innotek",
    "Toshiba Teli",
    "DigiPower Manufactur",
    "Notebook Development",
    "Mesco Engineering Gm",
    "Honeywell, (Korea)",
    "Dell",
    "Artistic Licence Eng",
    "Elgar Electronics",
    "Tri-M Engineering / ",
    "Dancontrol Engineeri",
    "Boris Manufacturing",
    "Leviton Manufacturin",
    "Eolring",
    "Sanrad Intelligence ",
    "Koei Engineering",
    "Quanta Network Syste",
    "ADI Engineering",
    "Philips Consumer Com",
    "Musashi Engineering",
    "Motion Control Engin",
    "SonoSite",
    "Samsung Techwin",
    "Inventec Appliance",
    "Intelnet",
    "Netgear",
    "Philips Medical Syst",
    "Dialogue Technology",
    "Intelligent Platform",
    "Cronyx Engineering",
    "Nintendo",
    "Philips Patient Moni",
    "MediaTek",
    "Honeywell Video Syst",
    "Motorola",
    "Wistron",
    "TP-Link Technologies",
    "Broadcom",
    "LS(LG) Industrial Sy",
    "Audio Engineering So",
    "Teralink Communicati",
    "Align Engineering",
    "Digital Monitoring P",
    "Nokia NET Product Op",
    "Berkeley Camera Engi",
    "Cisco-Linksys",
    "Mackie Engineering S",
    "ASUSTek Computer",
    "Bose",
    "Mentor Engineering",
    "Salland Engineering ",
    "Premier Technolgies",
    "Intelligent Computer",
    "Hgst a Western Digit",
    "JAI Manufacturing",
    "cybernet manufacturi",
    "Kyoto Electronics Ma",
    "Audio BU - Logitech",
    "Roku",
    "Engineering & Securi",
    "D-Link",
    "Samsung Heavy Indust",
    "Cosmic Engineering",
    "Samsung Thales",
    "Youngbo Engineering",
    "Thomson Telecom Belg",
    "Murata Manufacturing",
    "B&B Electronics Manu",
    "Motorola Korea",
    "Custom Engineering",
    "Synchronic Engineeri",
    "Panasonic Europe",
    "Midas Engineering",
    "Canon Korea",
    "Dialog",
    "Nokia Siemens Networ",
    "Hitachi Software Eng",
    "Tollgrade Communicat",
    "Nokia Multimedia Ter",
    "Applied Intelligent ",
    "Siemens NV (Belgium)",
    "Honeywell Cmss",
    "Belkin",
    "Intellambda Systems",
    "Monitoring Technolog",
    "Vtech Engineering Ca",
    "Manufacturing Techno",
    "Hangzhou Sunyard Sys",
};

static const OUIRecord ouiTable[350] PROGMEM = {
    {0x00000C, 0},
    {0x000039, 1},
    {0x000048, 2},
    {0x000085, 3},
    {0x000095, 4},
    {0x000097, 5},
    {0x0000C0, 6},
    {0x0000F0, 7},
    {0x00012A, 8},
    {0x000142, 0},
    {0x000143, 0},
    {0x000144, 5},
    {0x00014A, 9},
    {0x000163, 0},
    {0x000164, 0},
    {0x00016C, 10},
    {0x000196, 0},
    {0x000197, 0},
    {0x0001B3, 11},
    {0x0001C7, 0},
    {0x0001C9, 0},
    {0x0001EC, 12},
    {0x000216, 0},
    {0x000217, 0},
    {0x000220, 13},
    {0x00023B, 14},
    {0x00023D, 0},
    {0x00023F, 15},
    {0x00024A, 0},
    {0x00024B, 0},
    {0x000261, 16},
    {0x00026C, 17},
    {0x000278, 18},
    {0x00027D, 0},
    {0x00027E, 0},
    {0x0002B3, 19},
    {0x0002B9, 0},
    {0x0002BA, 0},
    {0x0002BE, 20},
    {0x0002E2, 21},
    {0x0002EC, 22},
    {0x0002EE, 23},
    {0x0002F8, 24},
    {0x0002FC, 0},
    {0x0002FD, 0},
    {0x000319, 25},
    {0x000331, 0},
    {0x000332, 0},
    {0x000334, 26},
    {0x000347, 19},
    {0x00035F, 27},
    {0x00036B, 0},
    {0x00036C, 0},
    {0x00037A, 28},
    {0x00037F, 29},
    {0x000389, 30},
    {0x000393, 31},
    {0x00039F, 0},
    {0x0003A0, 0},
    {0x0003AE, 32},
    {0x0003E3, 0},
    {0x0003E4, 0},
    {0x0003FD, 0},
    {0x0003FE, 0},
    {0x0003FF, 33},
    {0x00041C, 34},
    {0x00041F, 35},
    {0x000423, 19},
    {0x000427, 0},
    {0x000428, 0},
    {0x00043A, 36},
    {0x00043C, 37},
    {0x00044D, 0},
    {0x00044E, 0},
    {0x00045A, 38},
    {0x00046D, 0},
    {0x00046E, 0},
    {0x00047D, 39},
    {0x000482, 40},
    {0x00049A, 0},
    {0x00049B, 0},
    {0x0004A3, 41},
    {0x0004C0, 0},
    {0x0004C1, 0},
    {0x0004CF, 42},
    {0x0004D5, 43},
    {0x0004DD, 0},
    {0x0004DE, 0},
    {0x0004FD, 44},
    {0x000500, 0},
    {0x000501, 0},
    {0x000502, 31},
    {0x000531, 0},
    {0x000532, 0},
    {0x000534, 45},
    {0x00054E, 46},
    {0x00054F, 47},
    {0x00055D, 48},
    {0x00055E, 0},
    {0x00055F, 0},
    {0x000573, 0},
    {0x000574, 0},
    {0x00059A, 0},
    {0x00059B, 0},
    {0x0005B3, 49},
    {0x0005B5, 50},
    {0x0005C9, 51},
    {0x0005DC, 0},
    {0x0005DD, 0},
    {0x000600, 52},
    {0x000618, 53},
    {0x00061B, 54},
    {0x000625, 38},
    {0x000628, 0},
    {0x00062A, 0},
    {0x000632, 55},
    {0x00064A, 56},
    {0x000652, 0},
    {0x000653, 0},
    {0x00065B, 57},
    {0x00067C, 0},
    {0x0006A6, 58},
    {0x0006C1, 0},
    {0x0006D0, 59},
    {0x0006D6, 0},
    {0x0006D7, 0},
    {0x0006F6, 0},
    {0x00070D, 0},
    {0x00070E, 0},
    {0x00071E, 60},
    {0x000733, 61},
    {0x00074F, 0},
    {0x000750, 0},
    {0x000759, 62},
    {0x00077D, 0},
    {0x000784, 0},
    {0x000785, 0},
    {0x0007A6, 63},
    {0x0007AB, 7},
    {0x0007AC, 64},
    {0x0007B3, 0},
    {0x0007B4, 0},
    {0x0007E9, 19},
    {0x0007EB, 0},
    {0x0007EC, 0},
    {0x00080D, 1},
    {0x00081A, 65},
    {0x000820, 0},
    {0x000821, 0},
    {0x000828, 66},
    {0x00082F, 0},
    {0x000830, 0},
    {0x000831, 0},
    {0x000832, 0},
    {0x000874, 57},
    {0x00087C, 0},
    {0x00087D, 0},
    {0x00088C, 67},
    {0x0008A2, 68},
    {0x0008A3, 0},
    {0x0008A4, 0},
    {0x0008C2, 0},
    {0x0008C6, 69},
    {0x0008D0, 70},
    {0x0008E2, 0},
    {0x0008E3, 0},
    {0x0008EA, 71},
    {0x0008FB, 72},
    {0x000911, 0},
    {0x000912, 0},
    {0x000918, 73},
    {0x000937, 74},
    {0x000943, 0},
    {0x000944, 0},
    {0x000958, 75},
    {0x00095B, 76},
    {0x00095C, 77},
    {0x00095D, 78},
    {0x00097B, 0},
    {0x00097C, 0},
    {0x000991, 79},
    {0x000994, 80},
    {0x0009B6, 0},
    {0x0009B7, 0},
    {0x0009BF, 81},
    {0x0009E8, 0},
    {0x0009E9, 0},
    {0x0009FB, 82},
    {0x000A00, 83},
    {0x000A13, 84},
    {0x000A27, 31},
    {0x000A28, 85},
    {0x000A41, 0},
    {0x000A42, 0},
    {0x000A8A, 0},
    {0x000A8B, 0},
    {0x000A95, 31},
    {0x000AB7, 0},
    {0x000AB8, 0},
    {0x000AD9, 9},
    {0x000AE4, 86},
    {0x000AEB, 87},
    {0x000AF3, 0},
    {0x000AF4, 0},
    {0x000AF7, 88},
    {0x000B29, 89},
    {0x000B45, 0},
    {0x000B46, 0},
    {0x000B5E, 90},
    {0x000B5F, 0},
    {0x000B60, 0},
    {0x000B66, 91},
    {0x000B7F, 92},
    {0x000B85, 0},
    {0x000B94, 93},
    {0x000BBE, 0},
    {0x000BBF, 0},
    {0x000BDB, 57},
    {0x000BE1, 94},
    {0x000BFC, 0},
    {0x000BFD, 0},
    {0x000BFF, 95},
    {0x000C30, 0},
    {0x000C31, 0},
    {0x000C41, 96},
    {0x000C43, 83},
    {0x000C50, 42},
    {0x000C57, 97},
    {0x000C6E, 98},
    {0x000C85, 0},
    {0x000C86, 0},
    {0x000C8A, 99},
    {0x000C8E, 100},
    {0x000CB1, 101},
    {0x000CB5, 102},
    {0x000CC7, 103},
    {0x000CCA, 104},
    {0x000CCE, 0},
    {0x000CCF, 0},
    {0x000CDF, 105},
    {0x000CE7, 83},
    {0x000CF1, 19},
    {0x000D05, 106},
    {0x000D1B, 107},
    {0x000D28, 0},
    {0x000D29, 0},
    {0x000D3A, 33},
    {0x000D44, 108},
    {0x000D4B, 109},
    {0x000D56, 57},
    {0x000D65, 0},
    {0x000D66, 0},
    {0x000D67, 14},
    {0x000D78, 110},
    {0x000D88, 111},
    {0x000D93, 31},
    {0x000DAE, 112},
    {0x000DB6, 88},
    {0x000DBC, 0},
    {0x000DBD, 0},
    {0x000DC7, 113},
    {0x000DE5, 114},
    {0x000DE6, 115},
    {0x000DEC, 0},
    {0x000DED, 0},
    {0x000E07, 9},
    {0x000E08, 96},
    {0x000E0C, 19},
    {0x000E35, 19},
    {0x000E38, 0},
    {0x000E39, 0},
    {0x000E50, 116},
    {0x000E58, 37},
    {0x000E6D, 117},
    {0x000E7B, 1},
    {0x000E83, 0},
    {0x000E84, 0},
    {0x000EA6, 98},
    {0x000EBE, 118},
    {0x000EC7, 119},
    {0x000ED6, 0},
    {0x000ED7, 0},
    {0x000EE2, 120},
    {0x000EED, 23},
    {0x000F0C, 121},
    {0x000F12, 122},
    {0x000F1F, 57},
    {0x000F23, 0},
    {0x000F24, 0},
    {0x000F34, 0},
    {0x000F35, 0},
    {0x000F3D, 111},
    {0x000F66, 96},
    {0x000F6D, 123},
    {0x000F8F, 0},
    {0x000F90, 0},
    {0x000FA0, 124},
    {0x000FAF, 125},
    {0x000FB0, 15},
    {0x000FB5, 76},
    {0x000FBB, 126},
    {0x000FDE, 9},
    {0x000FF7, 0},
    {0x000FF8, 0},
    {0x001007, 0},
    {0x00100B, 0},
    {0x00100D, 0},
    {0x001011, 0},
    {0x001014, 0},
    {0x001018, 88},
    {0x00101F, 0},
    {0x001029, 0},
    {0x00102D, 127},
    {0x00102F, 0},
    {0x00103F, 128},
    {0x001054, 0},
    {0x001067, 14},
    {0x001079, 0},
    {0x00107B, 0},
    {0x0010A6, 0},
    {0x0010B3, 129},
    {0x0010E6, 130},
    {0x0010F6, 0},
    {0x0010FA, 31},
    {0x0010FF, 0},
    {0x001106, 131},
    {0x001111, 19},
    {0x001112, 132},
    {0x001120, 0},
    {0x001121, 0},
    {0x001124, 31},
    {0x00112F, 98},
    {0x001143, 57},
    {0x001150, 133},
    {0x00115C, 0},
    {0x00115D, 0},
    {0x001175, 19},
    {0x001176, 134},
    {0x001192, 0},
    {0x001193, 0},
    {0x001195, 111},
    {0x001197, 135},
    {0x00119F, 23},
    {0x0011A0, 136},
    {0x0011A2, 137},
    {0x0011BB, 0},
    {0x0011BC, 0},
    {0x0011C6, 42},
    {0x0011D5, 138},
    {0x0011D8, 98},
};

// Runtime SD-loaded table
static OUIRecord sdTable[MAX_SD_OUIS];
static int sdTableCount = 0;
static char sdVendorBuf[MAX_SD_OUIS][22];
static int sdVendorCount = 0;


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
        if (line[lineLen] == '\n' || lineLen >= 126) {
            line[lineLen] = '\0';
            lineLen = 0;
            // skip comments / empty
            if (line[0] == '#' || line[0] == '\0') continue;
            char *space = strchr(line, ' ');
            if (!space) continue;
            *space = '\0';
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
                sdVendorBuf[sdVendorCount][21] = '\0';
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
    int lo = 0, hi = 350 - 1;
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

#endif