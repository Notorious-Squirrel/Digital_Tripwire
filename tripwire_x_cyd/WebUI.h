#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>

extern WebServer server;
extern DNSServer dnsServer;
extern bool scanningPaused;

void startWebUI();
void stopWebUI();
void handleWebUI();
