/*
  xdrv_57_1_tasmesh_support.ino - Mesh via ESP-Now support for Tasmota

  Copyright (C) 2021  Christian Baars and Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_TASMESH

#include <queue>
#include <t_bearssl_block.h>

#ifdef ESP32
#include <esp_now.h>
#include <esp_wifi.h>
#else
#include <espnow.h>
#endif //ESP32

/*********************************************************************************************\
 * constants
\*********************************************************************************************/

#define D_CMND_MESH "MESH"

#define MESH_PAYLOAD_SIZE 160      // Default 160 - with header of 20 bytes and 16 bytes tag, stays below 200 bytes, which is reported to work with ESP8266
// Espressif stated max payload is 250 bytes
#define MESH_TOPICSZ      64       // Max supported topic size
#define MESH_BUFFERS      20       // (20) Number buffers for reassembling split messages (on broker only!)
#define MESH_MAX_PACKETS  5        // (5) Max number of packets over which a message can be split,
// absolute maximum **OUTGOING** message length of node = (MESH_PAYLOAD_SIZE - lenth_of_node_topic) * MESH_MAX_PACKETS
// maximum **INCOMING** message length of node = MESH_PAYLOAD_SIZE - lenth_of_node_topic -> No incoming buffer on node!
#define MESH_REFRESH      50       // Number of ms

// The format of the vendor-specific action frame is as follows:
// ------------------------------------------------------------------------------------------------------------
// | MAC Header | Category Code | Organization Identifier | Random Values | Vendor Specific Content |   FCS   |
// ------------------------------------------------------------------------------------------------------------
//   24 bytes         1 byte              3 bytes               4 bytes             7~255 bytes        4 bytes
//
// The Vendor Specific Content contains vendor-specific fields as follows:
// -------------------------------------------------------------------------------
// | Element ID | Length | Organization Identifier | Type | Version |    Body    |
// -------------------------------------------------------------------------------
//     1 byte     1 byte            3 bytes         1 byte   1 byte   0~250 bytes

struct mesh_packet_t {
  uint8_t sender[6];               // MAC
  uint8_t receiver[6];             // MAC
  uint32_t counter:4;              // Rolling counter to identify a packet
  uint32_t type:6;                 // Command, Mqtt, ...
  uint32_t chunks:6;               // Number of chunks
  uint32_t chunk:6;                // Chunk number
  uint32_t chunkSize:8;            // Chunk size
  uint32_t TTL:2;                  // Time to live, counting down
  union {
    uint32_t senderTime;           // UTC-timestamp from every sender in the MESH
    uint32_t peerIndex;            // Only for resending in the MESH
  };
  uint8_t tag[16];                 // Tag for de/encryption
  uint8_t payload[MESH_PAYLOAD_SIZE];
} __attribute__((packed));

struct mesh_packet_header_t {      // ToDo: Maybe, we do not need this
  uint8_t sender[6];               // MAC
  uint8_t receiver[6];             // MAC
  uint32_t counter:4;              // Rolling counter to identify a packet
  uint32_t type:6;                 // Command, Mqtt, ...
  uint32_t chunks:6;               // Number of chunks
  uint32_t chunk:6;                // Chunk number
  uint32_t chunkSize:8;            // Chunk size
  uint32_t TTL:2;                  // Time to live, counting down
  union {
    uint32_t senderTime;           // UTC-timestamp from every sender in the MESH
    uint32_t peerIndex;            // Only for resending in the MESH
  };
  uint8_t tag[16];                 // Tag for de/encryption
} __attribute__((packed));

struct mesh_peer_t {
  uint8_t MAC[6];
  uint32_t lastMessageFromPeer;    // Time of last message from peer
#ifdef ESP32
  char topic[MESH_TOPICSZ];
#endif //ESP32
};

struct mesh_flags_t {
  uint8_t brokerNeedsTopic:1;
  uint8_t nodeGotTime:1;
  uint8_t nodeWantsTime:1;
  uint8_t nodeWantsTimeASAP:1;
};

#ifdef ESP32
struct mesh_packet_combined_t {
  mesh_packet_header_t header;
  uint32_t receivedChunks;         // Bitmask for up to 32 chunks
  char raw[MESH_PAYLOAD_SIZE * MESH_MAX_PACKETS];
  // size of each is the size of an individual payload multiplied by the
  // maximum number of packets over which we're willing to split a message
};
#endif

struct mesh_first_header_bytes {   // TODO: evaluate random 4-byte-value of pre-packet
  uint8_t raw[15];
} __attribute__((packed));;

struct {
  uint32_t lastMessageFromBroker;  // Time of last message from broker
  uint32_t lmfap;                  // Time of last message from any peer
  uint8_t broker[6] = { 0 };       // The MAC of the broker
  uint8_t key[32];                 // The encryption/decryption key (taken from the PASSWORD1)
  uint8_t role;                    // The role of the node, from enum MESH_Role
  uint8_t channel;                 // Wifi channel
  uint8_t interval;                // Interval between processing loops
  uint8_t currentTopicSize;        // Length of current MQTT topic
  mesh_flags_t flags;              // Flags for needed mesh actions
  mesh_packet_t sendPacket;        // Holder for the next outgoing mesh packet
  std::vector<mesh_peer_t> peers;  // List of all mesh peers
  std::queue<mesh_packet_t> packetToResend;  // Queue for the packets to resend
  std::queue<mesh_packet_t> packetToConsume;  // Queue for the packets to process
  // std::vector<mesh_packet_header_t> packetsAlreadySended;  // A reference to ensure packets aren't double-sent
  // std::vector<mesh_first_header_bytes> packetsAlreadyReceived;  // A reference to ensure packets aren't double-processed
#ifdef ESP32
  std::vector<mesh_packet_combined_t> multiPackets;  // Storage for partially assembled split packets
#endif
// #ifdef ESP32
//   std::vector<std::string> lastTeleMsgs;  // for web display of message history
// #endif //ESP32
} MESH;

/*********************************************************************************************\
 * Declarations for functions with custom types
\*********************************************************************************************/

void MESHsendPacket(mesh_packet_t *_packet);
bool MESHencryptPayload(mesh_packet_t *_packet, int _encrypt);  // 1 encryption, 0 decryption

/*********************************************************************************************\
 * enumerations
\*********************************************************************************************/

enum MESH_Commands {               // commands useable in console or rules
  CMND_MESH_BROKER,                // start broker on ESP32
  CMND_MESH_NODE,                  // start node and connect to broker based on MAC address
  CMND_MESH_PEER,                  // add node to peer list of a broker or node
  CMND_MESH_CHANNEL};              // set wifi channel on node (the broker gets it automatically from the AP)

enum MESH_Role {
  ROLE_NONE = 0,                   // not initialized
  ROLE_BROKER,                     // ESP32 will connect mesh to WLAN
  ROLE_NODE_FULL,                  // Node will listen and resend every message for MESH functionality
  ROLE_NODE_SMALL                  // Node will only talk to the broker
};

enum MESH_Packet_Type {            // Type of packet
  PACKET_TYPE_TIME = 0,            //
  PACKET_TYPE_PEERLIST,            // send all kown peers, broker is always 0
  PACKET_TYPE_COMMAND,             // not used yet
  PACKET_TYPE_REGISTER_NODE,       // register a node with encrypted broker-MAC, announce mqtt topic to ESP32-proxy - broker will send time ASAP
  PACKET_TYPE_REFRESH_NODE,        // refresh node infos with encrypted broker-MAC, announce mqtt topic to ESP32-proxy - broker will send time slightly delayed
  PACKET_TYPE_MQTT,                // send regular mqtt messages, single or multipackets
  PACKET_TYPE_WANTTOPIC            // the broker has no topic for this peer/node
};

/*********************************************************************************************\
 *
\*********************************************************************************************/

#ifdef ESP32

/**
 * @brief **ESP-32 ONLY**  Prepares a "sendPacket" object for a time packet and then broadcasts it over the mesh.
 */
void MESHsendTime(void) {          // Only from broker to nodes
  MESH.sendPacket.counter++;
  MESH.sendPacket.type = PACKET_TYPE_TIME;
  MESH.sendPacket.TTL = 1;
  // memcpy(MESH.sendPacket.receiver,MESH.peers[_peerNumber].MAC,6);
  MESH.sendPacket.senderTime = Rtc.utc_time;
  MESH.sendPacket.payload[0] = 0;
  // mesh_flags_t *_flags = (mesh_flags_t *)MESH.sendPacket.payload;
  // if(MESH.peers[_peerNumber].topic[0]==0){
  //   AddLog(LOG_LEVEL_INFO, PSTR("MSH: Broker wants topic from peer: %u"), _peerNumber);
  //   _flags->brokerNeedsTopic = 1;
  // }
  MESH.sendPacket.chunkSize = 0;
  MESH.sendPacket.chunks = 0;
  MESHsendPacket(&MESH.sendPacket);
}

/**
 * @brief **ESP-32 ONLY**  Prepares a "sendPacket" object for a demand for the MQTT topic of a peer (node) and broadcast it over the mesh.
 *
 * The packet type is `PACKET_TYPE_WANTTOPIC` and the payload is empty.  No time is sent in this packet?
 *
 * @param _peerNumber  The peer number (within the stored peer list) to demand the topic from.
 */
void MESHdemandTopic(uint32_t _peerNumber) {
  MESH.sendPacket.counter++;
  MESH.sendPacket.type = PACKET_TYPE_WANTTOPIC;
  MESH.sendPacket.TTL = 2;
  MESH.sendPacket.payload[0] = 0;
  MESH.sendPacket.chunkSize = 0;
  MESH.sendPacket.chunks = 0;
  memcpy(MESH.sendPacket.receiver,MESH.peers[_peerNumber].MAC,6);
  MESHsendPacket(&MESH.sendPacket);
}

#endif //ESP32

/**
 * @brief Prepares a sendPacket object with a list of all know peers
 *
 * The packet type is `PACKET_TYPE_PEERLIST` and the payload is a list of the MAC's of all peers.
 */
void MESHsendPeerList(void) {      // We send this list only to the peers, that can directly receive it
  MESH.sendPacket.counter++;
  MESH.sendPacket.type = PACKET_TYPE_PEERLIST;
  MESH.sendPacket.senderTime = Rtc.utc_time;
  uint32_t _idx = 0;
  for (auto &_peer : MESH.peers) {
    memcpy(MESH.sendPacket.payload + _idx, _peer.MAC, 6);
    _idx += 6;
  }
  if (0 == _idx) { return; }

  MESH.sendPacket.chunk = 0;
  MESH.sendPacket.chunks = 1;
  MESH.sendPacket.chunkSize = _idx;
  MESH.sendPacket.TTL = 1;
//  AddLogBuffer(LOG_LEVEL_INFO, MESH.sendPacket.payload, MESH.sendPacket.chunkSize);
  MESHsendPacket(&MESH.sendPacket);
}

/**
 * @brief Iterate over the peer list checking for the given MAC
 *
 * @param MAC The MAC address of a potentially known peer
 * @return *true*  If the peer is already on the peer list
 * @return *false*  If the peer is *NOT* on the peer list
 */
bool MESHcheckPeerList(const uint8_t *MAC) {
  bool success = false;
  for (auto &_peer : MESH.peers) {
    if (memcmp(_peer.MAC, MAC, 6) == 0) {
      _peer.lastMessageFromPeer = millis();
      return true;
    }
  }
  return false;
}

/**
 * @brief Count the number of peers on the know peer list
 *
 * @return *uint8_t*  The number of peers
 */
uint8_t MESHcountPeers(void) {
#ifdef ESP32
  esp_now_peer_num_t _peernum;
  esp_now_get_peer_num(&_peernum);
  uint8_t _num = _peernum.total_num;
#else
  uint8_t _num;
  uint8_t _numEnc; // we don't care
  esp_now_get_cnt_info(&_num, &_numEnc);
#endif
//  AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Peers %u"), _num);
  return _num;
}

/**
 * @brief Adds a new peer to the list of known peers
 *
 * @param _MAC The MAC address of the new peer
 * @return *int* 0 if the peer is successfully added, else the esp_now error number
 */
int MESHaddPeer(uint8_t *_MAC ) {
  mesh_peer_t _newPeer;
  memcpy(_newPeer.MAC, _MAC, 6);
  _newPeer.lastMessageFromPeer = millis();
#ifdef ESP32
  _newPeer.topic[0] = 0;
#endif
  MESH.peers.push_back(_newPeer);
// #ifdef ESP32
//   std::string _msg = "{\"Init\":1}"; // Init with a simple JSON only while developing
//   MESH.lastTeleMsgs.push_back(_msg); // We must keep this vector in sync with the peers-struct on the broker regarding the indices
// #endif //ESP32
  int err;
#ifdef ESP32
  esp_now_peer_info_t _peer;
  _peer.channel = MESH.channel;
  _peer.encrypt = false;
  _peer.ifidx = (wifi_interface_t)ESP_IF_WIFI_AP;
  memcpy(_peer.peer_addr, _MAC, 6);
  err = esp_now_add_peer(&_peer);
#else
  err = esp_now_add_peer(_MAC, ESP_NOW_ROLE_COMBO, MESH.channel, NULL, 0);
#endif
  if (0 == err) {
    char _peerMAC[18];
    ToHex_P(_MAC, 6, _peerMAC, 18, ':');
    AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Peer %s added successful"), _peerMAC);
  #ifdef ESP32
    if (ROLE_BROKER == MESH.role) { MESHsendTime(); }
  #endif //ESP32
    AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Rule Trigger: {\"%s\":{\"Peers\":%u}}"), D_CMND_MESH, MESH.peers.size());
    Response_P(PSTR("{\"%s\":{\"Peers\":%u}}"), D_CMND_MESH, MESH.peers.size());
    XdrvRulesProcess(0);
    return err;
  } else {
    AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Failed to add peer %d"), err);
  }
  return err;
}

//helper functions
/**
 * @brief Removes colons from the string pointed to by the input pointer.
 *
 * Used to read MAC addresses
 *
 * @param _string The a pointer to the string potentially with colons.
 */
void MESHstripColon(char* _string) {
  uint32_t _length = strlen(_string);
  uint32_t _index = 0;
  while (_index < _length) {
    char c = _string[_index];
    if (c == ':') {
      memmove(_string + _index, _string + _index +1, _length - _index);
    }
    _index++;
  }
  _string[_index] = 0;
}

/**
 * @brief Converts a character string pointer for a MAC address into an array of bytes
 *
 * @param _string A pointer to the string MAC address
 * @param _MAC An array of bytes for the MAC address
 */
void MESHHexStringToBytes(char* _string, uint8_t _MAC[]) { //uppercase
  MESHstripColon(_string);
  UpperCase(_string, _string);
  uint32_t index = 0;
  uint32_t _end = strlen(_string);
  memset(_MAC, 0, _end / 2);
  while (index < _end) {
    char c = _string[index];
    uint8_t value = 0;
    if ((c >= '0') && (c <= '9')) {
      value = (c - '0');
    }
    else if ((c >= 'A') && (c <= 'F')) {
      value = (10 + (c - 'A'));
    }
    _MAC[(index / 2)] += value << (((index + 1) % 2) * 4);
    index++;
  }
}

/**
 * @brief Encrypts one packet from a message and broadcasts it over the ESP-NOW network
 *
 * @param _packet
 */
void MESHsendPacket(mesh_packet_t *_packet) {
  MESHencryptPayload(_packet, 1);
//  esp_now_send(_packet->receiver, (uint8_t *)_packet, sizeof(MESH.sendPacket) - MESH_PAYLOAD_SIZE + _packet->chunkSize);
  esp_now_send(NULL, (uint8_t *)_packet, sizeof(MESH.sendPacket) - MESH_PAYLOAD_SIZE + _packet->chunkSize); // NULL peer address -> broadcast
  //  ^^ Return
  //        ESP_OK : succeed
  //        ESP_ERR_ESPNOW_NOT_INIT : ESPNOW is not initialized
  //        ESP_ERR_ESPNOW_ARG : invalid argument
  //        ESP_ERR_ESPNOW_INTERNAL : internal error
  //        ESP_ERR_ESPNOW_NO_MEM : out of memory
  //        ESP_ERR_ESPNOW_NOT_FOUND : peer is not found
  //        ESP_ERR_ESPNOW_IF : current WiFi interface doesn’t match that of peer

}

/**
 * @brief Sets the current wifi password as the encryption key for ESP-NOW
 *
 * @param _key A pointer to the place in memory to store the new encryption key
 */
void MESHsetKey(uint8_t* _key) {   // Must be 32 bytes!!!
  char* _pw = SettingsText(SET_STAPWD1 + Settings->sta_active);
  size_t _length = strlen(_pw);
  memset(_key, 0, 32);
  if (_length > 32) { _length = 32; }
  memcpy(_key, _pw, _length);
  AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: set crypto key to PASSWORD1"));
}

/**
 * @brief Encrypts or decrypts a mesh package using the encryption key (the wifi password)
 *
 * @param _packet The packet to encrypt
 * @param _encrypt 1 to encrypt, 0 to decrypt
 * @return *true* encryption succeeded
 * @return *false* encryption failed
 */
bool MESHencryptPayload(mesh_packet_t *_packet, int _encrypt) {

// AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: will encrypt: %u"), _encrypt);

  size_t _size = _packet->chunkSize;
  char _tag[16];

// AddLog(LOG_LEVEL_DEBUG, PSTR("cc: %u, _size: %u"), _counter,_size);
// AddLogBuffer(LOG_LEVEL_DEBUG,(uint8_t*)_tag,16);

  br_chacha20_run bc = br_chacha20_ct_run;

  br_poly1305_ctmul32_run((void*)MESH.key, (const void *)_packet,
    (void *)_packet->payload, _size, _packet->receiver+6, 2,
    _tag, bc, _encrypt);

//  AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: encryption done "));

  if (_encrypt==1) {
    memcpy(_packet->tag, _tag, 16);
//    AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: payload encrypted"));
    return true;
  }
  if (memcmp(_packet->tag, _tag, 16) == 0) {
//    AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: payload decrypted"));
    return true;
  }
  AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: payload decryption error"));
  return false;
}

/**
 * @brief Sets the sleep interval to that of the mesh interval.
 */
void MESHsetSleep(void) {
  if (MESH.role && (Settings->sleep > MESH.interval)) {
    Settings->sleep = MESH.interval;
    TasmotaGlobal.sleep = MESH.interval;
  }
}

/**
 * @brief Enables or disables the standard wifi interface
 *
 * @param state True to enable wifi, False to disable it
 */
void MESHsetWifi(bool state) {
#ifdef ESP8266                         // Only ESP8266 as ESP32 is a broker and needs Wifi
  if (state) {                         // Wifi On
    Settings->flag4.network_wifi = 1;  // (Re-)enable wifi as long as Mesh is not enabled
//    TasmotaGlobal.global_state.wifi_down = 0;
    Settings->flag.global_state = 0;   // (Wifi, MQTT) Control link led blinking (1)
  } else {                             // Wifi Off and use ESP-NOW
    Settings->flag4.network_wifi = 0;  // The "old" wifi off command
    TasmotaGlobal.global_state.wifi_down = 1;
    Settings->flag.global_state = 1;   // (Wifi, MQTT) Control link led blinking (1)
  }
#endif  // ESP8266
}

/**
 * @brief Calculates the maximum size of of MQTT payload within a MESH packet, given the space already taken by the topic
 *
 * @return *uint32_t*  The remaining space in the payload
 */
uint32_t MESHmaxPayloadSize(void) {
  return MESH_PAYLOAD_SIZE - MESH.currentTopicSize -1;
}

#endif //USE_TASMESH
