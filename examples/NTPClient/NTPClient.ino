// Example of an NTP time client.
//
// Copyright 2011 Dirk-Willem van Gulik <dirkx@webweaving.org> all rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <Arduino.h>
#include <EtherCard.h>

// start of user servicable parts -------------

// ethernet interface mac address - unique on local lan.
//
static byte mymac[] = { 0x00,0x40,0x01,0x02,0x03,0x09 };

// Server to consult - with a local fallback if
// that fails. Set either to 0/NULL to ignore.
//
char ntpFqdnServer[]  PROGMEM = "pool.ntp.org";
uint8_t localNtpServer[4] = { 192,168,1,67 };

#define OFFSET (3600) // seconds of localtime v.s. UTC

#define REQUEST_TIMEOUT (60*1000UL)   // milliseconds - how often to check with the NTP server.
#define RETRY_TIMEOUT (10*1000UL)     // milliseconds - retry delay if we get no answer soon enough (also the timeout).
#define DNS_TIMEOUT (5*1000UL)        // milliseconds - retry for a DNS looup (excluding DNS its own timeout).

// end of user servicable parts -------------

// Change the source port each time - and wrap around
// after 256 of them (as a short is just too large).
//
#define SRCPORT (1024 + (++lastNtpSrcPort))
byte lastNtpSrcPort = 1024;

// As per RFC868 and newer. We're making use of the fact that
// NTP rolls/slides with things like leap seconds.
#define NTPEPOCH2UNIX (2208988800UL)

unsigned long lastTry = 0;
unsigned long timeout = 0;
uint8_t dnsFail = 0;

long firstDelta = 0;

byte Ethernet::buffer[700];

void setup () {
  // NanodeMAC mac( mymac );

  Serial.begin(57600);
  Serial.println("\n[NTP Client] Build: " __DATE__ "/" __TIME__);

  if (ether.begin(sizeof Ethernet::buffer, mymac) == 0) 
    Serial.println( "Failed to access Ethernet controller");

  if (!ether.dhcpSetup())
    Serial.println("DHCP failed");

  ether.printIp("My IP: ", ether.myip);
  ether.printIp("GW IP: ", ether.gwip);
  ether.printIp("DNS IP: ", ether.dnsip);
  
  // if (p=getDhcpOption(42,ether)) {
  //    Serial.println("Using DHCP(42) provided NTP server.");
  //    memcpy(p,localNtpServer,4);
  //    ntpFqdnServer = NULL;
  // };
  lastTry = 0; // start timing out right away
}


void loop () {
  if (ether.dhcpExpired() && !ether.dhcpSetup()) {
    Serial.println("DHCP failed");
    return;
  };

  // We're relying on C and the unsigned int behaviour for this
  // to work. See http://www.thetaeng.com/TimerWrap.htm or any
  // good book on embedded coding.
  //
  if ((millis() - lastTry) > timeout) {
    lastTry = millis();

    if (dnsFail < 3 && ntpFqdnServer) {
      timeout = DNS_TIMEOUT * (1 + dnsFail);
      if (!ether.dnsLookup(ntpFqdnServer)) {
        dnsFail++;
        Serial.println("DNS lookup of NTP server failed");
        return;
      }
      dnsFail = 0;
    };
    
    if (dnsFail && localNtpServer) {
      Serial.println("Falling back to local NTP server");
      memcpy(ether.hisip,localNtpServer,4);
      dnsFail = 0;
    };
    
    if (dnsFail) {
      Serial.println("Hmm - no NTP ip or FQDN to try. giving up\n");
      return;
    };
    
    ether.printIp(">>> ntp request to ", ether.hisip);
    ether.ntpRequest (ether.hisip,SRCPORT);
    timeout = RETRY_TIMEOUT;
    return;
  }

  word len = ether.packetReceive();
  if (ether.packetLoop(len)) {
    // not sure if packetLoop ever confirms it is doneish - I think it just gleefully hides that.
    return;
  };
  
  // Anything (left) to process ?
  if (!len)
    return;

  // something raw left to do - lets see if it is NTP.
  uint32_t secsSince1900;
  if (ether.ntpProcessAnswer(&secsSince1900, lastNtpSrcPort)) {

    unsigned long epoch = secsSince1900 - NTPEPOCH2UNIX + OFFSET; 
    long delta = epoch * 1000L - millis();

    uint8_t h = (epoch  % 86400L) / 3600;
    uint8_t m = (epoch  % 3600) / 60;
    uint8_t s = epoch % 60;

    if (firstDelta == 0)
      firstDelta = delta;
    
    Serial.print("<<< reply ");
    
    if (h < 10) Serial.print("0");
    Serial.print(h);
    Serial.print(":");

    if (m < 10) Serial.print("0");
    Serial.print(m);
    Serial.print(":");

    if (s < 10) Serial.print("0");
    Serial.print(s);
    Serial.print(" local time -- ");
    
    Serial.print("Clock delta: ");    
    Serial.print(delta-firstDelta);
    Serial.println(" mSeconds");

    // and schedule our next attempt.    
    timeout = REQUEST_TIMEOUT;
    return;
  } // NTP reply processing.
  
  // potentially check for other packet types - and handle them.
  // ...
  return;
}

