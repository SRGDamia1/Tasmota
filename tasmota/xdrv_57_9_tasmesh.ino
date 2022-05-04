/*
  xdrv_57_tasmesh.ino - Mesh support for Tasmota using ESP-Now

  Copyright (C) 2021  Christian Baars, Federico Leoni and Theo Arends

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

/*
  --------------------------------------------------------------------------------------------
  Version yyyymmdd  Action     Description
  --------------------------------------------------------------------------------------------
  0.9.5.1 20210622  integrate  Expand number of chunks to satisfy larger MQTT messages
                               Refactor to latest Tasmota standards
  ---
  0.9.4.1 20210503  integrate  Add some minor tweak for channel management by Federico Leoni
  ---
  0.9.0.0 20200927  started    From scratch by Christian Baars
*/

#ifdef USE_TASMESH

/*********************************************************************************************\
* Build a mesh of nodes using ESP-Now
* Connect it through an ESP32-broker to WLAN
\*********************************************************************************************/

#define XDRV_57             57

/*********************************************************************************************\
 * Callbacks
\*********************************************************************************************/

#ifdef ESP32

/**
 * @brief **ESP32** Call back on sending data - logs the receiver and the status.
 *
 * This function is run with high priority on the WiFi task and should not contain any lengthy operations.
 * https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html#_CPPv417esp_now_send_cb_t
 *
 * @param MAC The mac address of the receiver
 * @param sendStatus The status/result of the send to the receiver
 */
void CB_MESHDataSent(const uint8_t *MAC, esp_now_send_status_t sendStatus);
void CB_MESHDataSent(const uint8_t *MAC, esp_now_send_status_t sendStatus) {
  char _destMAC[18];
  ToHex_P(MAC, 6, _destMAC, 18, ':');
  AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Data sent to %s with send status %d"), _destMAC, sendStatus);
}

/**
 * @brief **ESP32** Call back on receiving data over the ESP-NOW mesh.
 *
 * This function is run with high priority on the WiFi task and should not contain any lengthy operations.
 * https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html#_CPPv417esp_now_recv_cb_t
 *
 * @param MAC The MAC of the peer from which the data came
 * @param packet A pointer to where the incoming packet is now stored in memory
 * @param len The length of the incoming packet
 */
void CB_MESHDataReceived(const uint8_t *MAC, const uint8_t *packet, int len) {
  static bool _locked = false;
  if (_locked) { return; }

  _locked = true;
  char _srcMAC[18];
  ToHex_P(MAC, 6, _srcMAC, 18, ':');
  AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Rcvd from %s"), _srcMAC);
  mesh_packet_t *_recvPacket = (mesh_packet_t*)packet;
  // register and refresh node packets from a node have a payload of the MAC of the desired broker followed by their own MQTT topid
  if ((_recvPacket->type == PACKET_TYPE_REGISTER_NODE) || (_recvPacket->type == PACKET_TYPE_REFRESH_NODE)) {
    if (MESHcheckPeerList((const uint8_t *)MAC) == false) { // if it's not an already know peer
      MESHencryptPayload(_recvPacket, 0); //decrypt it and check the payload
      if (memcmp(_recvPacket->payload, MESH.broker, 6) == 0) {  // if the MAC in the payload matches the MAC of the broker (ie, self on ESP32)
        MESHaddPeer((uint8_t*)MAC);  // add the new peer
//        AddLog(LOG_LEVEL_INFO, PSTR("MSH: Rcvd topic %s"), (char*)_recvPacket->payload + 6);
//        AddLogBuffer(LOG_LEVEL_INFO,(uint8_t *)&MESH.packetToConsume.front().payload,MESH.packetToConsume.front().chunkSize+5);
        for (auto &_peer : MESH.peers) {  // check all peers on the updated known peer list
          if (memcmp(_peer.MAC, _recvPacket->sender, 6) == 0) {  // when we read the peer who is the sender of the received packet
            strcpy(_peer.topic, (char*)_recvPacket->payload + 6);  // copy the node's MQTT topic to our peer list
            MESHsubscribe((char*)&_peer.topic);  // Subscribe to the peer's topic
            _locked = false;
            return;
          }
        }
      } else {  // if the broker MAC *doesn't* match ours
        // decript the message just to log its failure
        char _cryptMAC[18];
        ToHex_P(_recvPacket->payload, 6, _cryptMAC, 18, ':');
        AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Peer %s denied, wrong MAC %s"), _srcMAC, _cryptMAC);
        _locked = false;
        return;
      }
    } else {  // if this *is* a known node
      if (_recvPacket->type == PACKET_TYPE_REGISTER_NODE) {  //if it's trying to re-register
        // flag that the node is impatient
        MESH.flags.nodeWantsTimeASAP = 1; //this could happen after wake from deepsleep on battery powered device
      } else {
        // otherwise, just flag that we need to send the time when we get to it
        MESH.flags.nodeWantsTime = 1;
      }
    }
  }
  // for other types of packets (not register or refresh node)
  MESH.lmfap = millis();  // update the last reception from a peer time
  if (MESHcheckPeerList(MAC) == true){  // if this is a known peer
    // ^^ NOTE:  MESHcheckPeerList will update _peer.lastMessageFromPeer
    MESH.packetToConsume.push(*_recvPacket);  // add the message to the queue of messages to process
    AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Packet %d from %s to queue"), MESH.packetToConsume.size(), _srcMAC);
  }
  _locked = false;
}

#else  // ESP8266

/**
 * @brief **ESP8266** Call back on sending data - logs the receiver and the status.
 *
 * This function is run with high priority on the WiFi task and should not contain any lengthy operations.
 * https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html#_CPPv417esp_now_send_cb_t
 *
 * @param MAC The mac address of the receiver
 * @param sendStatus The status/result of the send to the receiver
 */
void CB_MESHDataSent(uint8_t *MAC, uint8_t sendStatus) {
  char _destMAC[18];
  ToHex_P(MAC, 6, _destMAC, 18, ':');
  AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Data sent to %s with send status %d"), _destMAC, sendStatus);
}

/**
 * @brief **ESP8266** Call back on receiving data over the ESP-NOW mesh.
 *
 * This function is run with high priority on the WiFi task and should not contain any lengthy operations.
 * https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html#_CPPv417esp_now_recv_cb_t
 *
 * @param MAC The MAC of the peer from which the data came
 * @param packet A pointer to where the incoming packet is now stored in memory
 * @param len The length of the incoming packet
 */
void CB_MESHDataReceived(uint8_t *MAC, uint8_t *packet, uint8_t len) {
  MESH.lmfap = millis(); //any peer
  if (memcmp(MAC, MESH.broker, 6) == 0) {  // directly from the broker
    MESH.lastMessageFromBroker = millis(); // update the last time heard from the broker
  }
  mesh_packet_t *_recvPacket = (mesh_packet_t*)packet;
  // for packets that we're going to process no matter who they are directed at
  switch (_recvPacket->type) {
    case PACKET_TYPE_TIME:  // if it's a time packet, sync the RTC
      Rtc.utc_time = _recvPacket->senderTime;
      Rtc.user_time_entry = true;
      MESH.lastMessageFromBroker = millis();  // time packets come from the broker
      if (MESH.flags.nodeGotTime == 0) {
        RtcSync("Mesh");
        // TasmotaGlobal.rules_flag.system_boot  = 1; // for now we consider the node booted and let trigger system#boot on RULES
      }
      MESH.flags.nodeGotTime = 1;
      //Wifi.retry = 0;
      AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Rule Trigger: {\"%s\":{\"Time\":1}}"), D_CMND_MESH);
      Response_P(PSTR("{\"%s\":{\"Time\":1}}"), D_CMND_MESH); //got the time, now we can publish some sensor data
      XdrvRulesProcess(0);
      AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Got time packet ..."));
      break;
    case PACKET_TYPE_PEERLIST:
      MESH.packetToConsume.push(*_recvPacket);  // Queue the peer list to process
      return;
      break;
    default:
      // nothing for now;
      break;
  }
  // now process packets with deference to who they're intended for
  //if this is a packet intended for some other node
  if (memcmp(_recvPacket->receiver, MESH.sendPacket.sender, 6) != 0) { //MESH.sendPacket.sender simply stores the MAC of the node
    if (ROLE_NODE_SMALL == MESH.role) {
      return; // a 'small node' does not perform mesh functions
    }
    AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Packet to resend ..."));
    MESH.packetToResend.push(*_recvPacket);  // queue it for re-sending
    return;
  } else { // if it IS intended for us
    if (_recvPacket->type == PACKET_TYPE_WANTTOPIC) {  // if it's the broker asking for our topic
      MESH.flags.brokerNeedsTopic = 1;  // set a flag for action
      AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Broker needs topic ..."));
      return; //nothing left to be done
    }
    // for(auto &_message : MESH.packetsAlreadyReceived){
    //   if(memcmp(_recvPacket,_message,15==0)){
    //     AddLog(LOG_LEVEL_INFO, PSTR("MSH: Packet already received"));
    //     return;
    //   }
    // }
    // MESH.packetsAlreadyReceived.push_back((mesh_packet_header_t*) _recvPacket);
    // AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Packet to consume ..."));
    MESH.packetToConsume.push(*_recvPacket);  // if we get here, queue the packet for action
  }
}

#endif  // ESP32

/*********************************************************************************************\
 * init driver
\*********************************************************************************************/

void MESHInit(void) {
  MESH.interval = MESH_REFRESH;
  MESH.role = ROLE_NONE;
  // MESH.packetsAlreadyReceived.reserve(5);
  MESH.peers.reserve(5);
#ifdef ESP32
  MESH.multiPackets.reserve(MESH_BUFFERS);
#endif

  MESH.sendPacket.counter = 0;
  MESH.sendPacket.chunks = 1;
  MESH.sendPacket.chunk = 0;
  MESH.sendPacket.type = PACKET_TYPE_TIME;
  MESH.sendPacket.TTL = 2;

  MESHsetWifi(1);           // (Re-)enable wifi as long as Mesh is not enabled

  AddLog(LOG_LEVEL_INFO, PSTR("MSH: Initialized"));
}

void MESHdeInit(void) {
#ifdef ESP8266  // only ESP8266, ESP32 as a broker should not use deepsleep
  AddLog(LOG_LEVEL_INFO, PSTR("MSH: Stopping"));
  // TODO: degister from the broker, so he can stop MQTT-proxy
  esp_now_deinit();
#endif  // ESP8266
}

/*********************************************************************************************\
 * MQTT proxy functions
\*********************************************************************************************/

#ifdef ESP32

/**
 * @brief **ESP32** Subscribes to a node's topic as a proxy
 *
 * @param topic - the topic received from the referring node
 */
void MESHsubscribe(char *topic) {
  char stopic[TOPSZ];
  GetTopic_P(stopic, CMND, topic, PSTR("#"));
  MqttSubscribe(stopic);
}

/**
 * @brief **ESP32** Unsubscribes from the node's topic
 *
 * @param topic - the topic to remove
 */
void MESHunsubscribe(char *topic) {
  char stopic[TOPSZ];
  GetTopic_P(stopic, CMND, topic, PSTR("#"));
  MqttUnsubscribe(stopic);
}

/**
 * @brief **ESP32**  Resubscribe to *ALL* connected nodes' topics
 */
void MESHconnectMQTT(void){
  for (auto &_peer : MESH.peers) {
    AddLog(LOG_LEVEL_INFO, PSTR("MSH: Reconnect topic %s"), _peer.topic);
    if (_peer.topic[0] != 0) {
      MESHsubscribe(_peer.topic);
    }
  }
}

/**
 * @brief **ESP32 only**  Intercepts mqtt message, that the broker (ESP32) subscribes to as a proxy for a node.
 *        Is called from xdrv_02_mqtt.ino. Will send the message in the payload via ESP-NOW.
 *
 * @param _topic
 * @param _data
 * @param data_len
 * @return true
 * @return false
 */
bool MESHinterceptMQTTonBroker(char* _topic, uint8_t* _data, unsigned int data_len) {
  if (MESH.role != ROLE_BROKER) { return false; }

  char stopic[TOPSZ];
//  AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Intercept topic %s"), _topic);
  for (auto &_peer : MESH.peers) {  // find the node matching the topic
    GetTopic_P(stopic, CMND, _peer.topic, PSTR("")); //cmnd/topic/
    if (strlen(_topic) != strlen(_topic)) {
      return false; // prevent false result when _topic is the leading substring of stopic
    }
    if (memcmp(_topic, stopic, strlen(stopic)) == 0) {
      MESH.sendPacket.chunkSize = strlen(_topic) +1;

      // if the message is too long, just bail
      if (MESH.sendPacket.chunkSize + data_len > MESH_PAYLOAD_SIZE) {
        AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Intercept payload oversized %d"), data_len);
        return false;
      }

      // prepare and a packet directed to the node with the right topic and broadcast it over the mesh
      memcpy(MESH.sendPacket.receiver, _peer.MAC, 6);
      memcpy(MESH.sendPacket.payload, _topic, MESH.sendPacket.chunkSize);
      memcpy(MESH.sendPacket.payload + MESH.sendPacket.chunkSize, _data, data_len);
      MESH.sendPacket.chunkSize += data_len;
      MESH.sendPacket.chunks = 1;
      AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Intercept payload '%s'"), MESH.sendPacket.payload);
      MESH.sendPacket.type = PACKET_TYPE_MQTT;
      MESH.sendPacket.senderTime = Rtc.utc_time;
      MESHsendPacket(&MESH.sendPacket); // encrypts and sends
      // int result = esp_now_send(MESH.sendPacket.receiver, (uint8_t *)&MESH.sendPacket, (sizeof(MESH.sendPacket))-(MESH_PAYLOAD_SIZE-MESH.sendPacket.chunkSize));
      //send to Node
      return true;
    }
  }
  return false;
}

#else  // ESP8266

/**
 * @brief **EP8266** Treat a packet received over the mesh as if it was received over MQTT, processing and handling
 *
 * @param _packet
 */
void MESHreceiveMQTT(mesh_packet_t *_packet);
void MESHreceiveMQTT(mesh_packet_t *_packet){
  uint32_t _slength = strlen((char*)_packet->payload);
  if (_packet->chunks == 1) { //single chunk message
    MqttDataHandler((char*)_packet->payload, (uint8_t*)(_packet->payload)+_slength+1, (_packet->chunkSize)-_slength);
  } else {
    AddLog(LOG_LEVEL_INFO, PSTR("MSH: Multiple chunks %u not supported yet"), _packet->chunks);
    // TODO: reconstruct message in buffer or only handle short messages
  }
}

#endif  // ESP32

/**
 * @brief Checks the role of a peer on the mesh
 *
 * @return *true*  If acting as a node
 * @return *false*  If acting as broker
 */
bool MESHroleNode(void) {
  return (MESH.role > ROLE_BROKER);
}

/**
 * @brief Redirects an outgoing mqtt message on a node to the ESP-NOW mesh just before
 * it otherwise would have been sent to the MQTT broker.
 *
 * This breaks the outgoing data into multiple ESP-NOW packets as necessary.
 *
 * @param _topic The outgoing MQTT topic
 * @param _data The outgoing MQTT message
 * @param _retained - currently unused
 * @return true
 * @return false
 */
bool MESHrouteMQTTtoMESH(const char* _topic, char* _data, bool _retained) {
  if (!MESHroleNode()) { return false; }

  size_t _bytesLeft = strlen(_topic) + strlen(_data) +2;
  MESH.sendPacket.counter++;
  MESH.sendPacket.chunk = 0;
  MESH.sendPacket.chunks = (_bytesLeft / MESH_PAYLOAD_SIZE) +1;
  memcpy(MESH.sendPacket.receiver, MESH.broker, 6);
  MESH.sendPacket.type = PACKET_TYPE_MQTT;
  MESH.sendPacket.chunkSize = MESH_PAYLOAD_SIZE;
  MESH.sendPacket.peerIndex = 0;
//  AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Chunks %u, Counter %u"), MESH.sendPacket.chunks, MESH.sendPacket.counter);
  size_t _topicSize = strlen(_topic) +1;
  size_t _offsetData = 0;
  while (_bytesLeft > 0) {
    size_t _byteLeftInChunk = MESH_PAYLOAD_SIZE;
    // MESH.sendPacket.chunkSize = MESH_PAYLOAD_SIZE;
    if (MESH.sendPacket.chunk == 0) {
      memcpy(MESH.sendPacket.payload, _topic, _topicSize);
      MESH.sendPacket.chunkSize = _topicSize;

      MESH.currentTopicSize = MESH.sendPacket.chunkSize;

      _bytesLeft -= _topicSize;
      _byteLeftInChunk -= _topicSize;
      AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Topic in payload '%s'"), (char*)MESH.sendPacket.payload);
//      AddLog(LOG_LEVEL_INFO, PSTR("MSH: After topic -> chunk:%u, pre-size: %u"),MESH.sendPacket.chunk,MESH.sendPacket.chunkSize);
    }
    if (_byteLeftInChunk > 0) {
      if (_byteLeftInChunk > _bytesLeft) {
//        AddLog(LOG_LEVEL_INFO, PSTR("MSH: only last chunk bL:%u bLiC:%u oSD:%u"),_bytesLeft,_byteLeftInChunk,_offsetData);
        _byteLeftInChunk = _bytesLeft;
//        AddLog(LOG_LEVEL_INFO, PSTR("MSH: only last chunk after correction -> chunk:%u, pre-size: %u"),MESH.sendPacket.chunk,MESH.sendPacket.chunkSize);
      }
      if (MESH.sendPacket.chunk > 0) { _topicSize = 0; }
//      AddLog(LOG_LEVEL_INFO, PSTR("MSH: %u"),_topicSize);
      memcpy(MESH.sendPacket.payload + _topicSize, _data + _offsetData, _byteLeftInChunk);
      AddLog(LOG_LEVEL_DEBUG, PSTR("MSH:  Data in payload '%s'"), (char*)MESH.sendPacket.payload + _topicSize);
      _offsetData += _byteLeftInChunk;
      _bytesLeft -= _byteLeftInChunk;
    }
    MESH.sendPacket.chunkSize += _byteLeftInChunk;
    MESH.packetToResend.push(MESH.sendPacket);
//    AddLog(LOG_LEVEL_INFO, PSTR("MSH: chunk:%u, size: %u"),MESH.sendPacket.chunk,MESH.sendPacket.chunkSize);
//    AddLogBuffer(LOG_LEVEL_INFO, (uint8_t*)MESH.sendPacket.payload, MESH.sendPacket.chunkSize);
    if (MESH.sendPacket.chunk == MESH.sendPacket.chunks) {
//      AddLog(LOG_LEVEL_INFO, PSTR("MSH: Too many chunks %u"), MESH.sendPacket.chunk +1);
    }

    SHOW_FREE_MEM(PSTR("MESHrouteMQTTtoMESH"));

    MESH.sendPacket.chunk++;
    MESH.sendPacket.chunkSize = 0;
  }
  return true;
}

/**
 * @brief The node registers or refreshes its status with the ESP-NOW broker by sending a message
 * with the MAC of the desired broker and its own mqtt topic
 *
 * @param mode 0 for a new node registration, 1 for a refresh
 *
 */
void MESHregisterNode(uint8_t mode){
  memcpy(MESH.sendPacket.receiver, MESH.broker, 6);  // Set desired receiver as MAC of broker
  AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Register node with topic '%s'"), (char*)MESH.sendPacket.payload +6);
  MESH.sendPacket.TTL = 2;
  MESH.sendPacket.chunks = 1;
  MESH.sendPacket.chunk = 0;
  MESH.sendPacket.chunkSize = strlen(TasmotaGlobal.mqtt_topic) + 1 + 6;
  memcpy(MESH.sendPacket.payload, MESH.broker, 6);  // First 6 bytes -> MAC of broker
  strcpy((char*)MESH.sendPacket.payload +6, TasmotaGlobal.mqtt_topic);  // Remaining bytes -> topic of node
  MESH.sendPacket.type = (mode == 0) ? PACKET_TYPE_REGISTER_NODE : PACKET_TYPE_REFRESH_NODE;
  MESHsendPacket(&MESH.sendPacket);
}

/*********************************************************************************************\
 * Generic functions
\*********************************************************************************************/

/**
 * @brief Initializes the ESP-NOW netowrk on an ESP8266 as a new node.  No action taken for an ESP32 at this time.
 *
 * @param _channel The wifi channel for the mesh
 * @param _role The node's role, @see MESH_Role
 */
void MESHstartNode(int32_t _channel, uint8_t _role){ //we need a running broker with a known channel at that moment
#ifdef ESP8266 // for now only ESP8266, might be added for the ESP32 later
  MESH.channel = _channel;
  WiFi.mode(WIFI_STA);
  WiFi.begin("", "", MESH.channel, nullptr, false); //fake connection attempt to set channel
  wifi_promiscuous_enable(1);
  wifi_set_channel(MESH.channel);
  wifi_promiscuous_enable(0);
  WiFi.disconnect();  // disconnect from the wifi router, in preference of the mesh
  MESHsetWifi(0);
  esp_now_deinit();  // in case it was already initialized but disconnected
  int init_result = esp_now_init();
  if (init_result != 0) {
    AddLog(LOG_LEVEL_INFO, PSTR("MSH: Node init failed with error: %d"), init_result);
    // try to re-launch wifi
    MESH.role = ROLE_NONE;
    MESHsetWifi(1);
    WifiBegin(3, MESH.channel);
    return;
  }

  AddLog(LOG_LEVEL_INFO, PSTR("MSH: Node initialized, channel: %u"),wifi_get_channel());
  AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Rule Trigger: {\"%s\":{\"Node\":1,\"Channel\":%u,\"Role\":%u}}"), D_CMND_MESH, wifi_get_channel(), _role);
  Response_P(PSTR("{\"%s\":{\"Node\":1,\"Channel\":%u,\"Role\":%u}}"), D_CMND_MESH, wifi_get_channel(), _role);
  XdrvRulesProcess(0);

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(CB_MESHDataSent);
  esp_now_register_recv_cb(CB_MESHDataReceived);

  MESHsetKey(MESH.key);
  memcpy(MESH.sendPacket.receiver, MESH.broker, 6);
  WiFi.macAddress(MESH.sendPacket.sender);
  MESHaddPeer(MESH.broker); //must always be peer 0!! -return code -7 for peer list full
  MESHcountPeers();
  MESH.lastMessageFromBroker = millis();  // Init
  MESH.role = (0 == _role) ? ROLE_NODE_SMALL : ROLE_NODE_FULL;
  MESHsetSleep();
  MESHregisterNode(0);
#endif  // ESP8266
}

/**
 * @brief Initializes the ESP-NOW network on an ESP32 as a mesh broker.
 * No action taken at this time for an ESP8266.
 */
void MESHstartBroker(void) {       // Must be called after WiFi is initialized!! Rule - on system#boot do meshbroker endon
#ifdef ESP32
  WiFi.mode(WIFI_AP_STA);
  AddLog(LOG_LEVEL_INFO, PSTR("MSH: Broker MAC %s"), WiFi.softAPmacAddress().c_str());
  WiFi.softAPmacAddress(MESH.broker); //set MESH.broker to the needed MAC
  uint32_t _channel = WiFi.channel();

  esp_now_deinit();  // in case it was already initialized by disconnected
  esp_err_t init_result = esp_now_init();
  if (esp_err_t() != ESP_OK) {
    AddLog(LOG_LEVEL_INFO, PSTR("MSH: Broker init failed with error: %s"), init_result);
    return;
  }

  AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Rule Trigger: {\"%s\":{\"Broker\":1,\"Channel\":%u}}"), D_CMND_MESH, _channel);
  Response_P(PSTR("{\"%s\":{\"Broker\":1,\"Channel\":%u}}"), D_CMND_MESH, _channel);
  XdrvRulesProcess(0);
//  AddLog(LOG_LEVEL_INFO, PSTR("MSH: Broker initialized on channel %u"), _channel);
  esp_now_register_send_cb(CB_MESHDataSent);
  esp_now_register_recv_cb(CB_MESHDataReceived);
  MESHsetKey(MESH.key);
  MESHcountPeers();
  memcpy(MESH.sendPacket.sender, MESH.broker, 6);
  MESH.role = ROLE_BROKER;
  MESHsetSleep();
#endif  // ESP32
}

/*********************************************************************************************\
 * Main loops
\*********************************************************************************************/

#ifdef ESP32

/**
 * @brief This is used for lower-priority prosessing of mesh action queues.
 * On each iteration, handle the packet in the front of the packetToConsume vector.
 */
void MESHevery50MSecond(void) {
  // if (MESH.packetToResend.size() > 0) {
  //   // pass the packets
  // }
  if (MESH.packetToConsume.size() > 0) {  // action only needed if we have packets in queue
// //    AddLog(LOG_LEVEL_DEBUG, PSTR("_"));
// //    AddLogBuffer(LOG_LEVEL_DEBUG,(uint8_t *)&MESH.packetToConsume.front(), 15);
//     for (auto &_headerBytes : MESH.packetsAlreadyReceived) {
// //      AddLog(LOG_LEVEL_DEBUG, PSTR("."));
// //      AddLogBuffer(LOG_LEVEL_DEBUG,(uint8_t *)_headerBytes.raw, 15);
//       if (memcmp(MESH.packetToConsume.front().sender, _headerBytes.raw, 15) == 0) {
//         MESH.packetToConsume.pop();
//         // ^^ if the message has already been received, remove it from the queue and move on
//         return;
//       }
//     }
//     mesh_first_header_bytes _bytes;
//     memcpy(_bytes.raw, &MESH.packetToConsume.front(), 15);
//     MESH.packetsAlreadyReceived.push_back(_bytes);
//     // ^^ immediately put the packet being consumed on the list of already consumed packets
//     // AddLog(LOG_LEVEL_DEBUG, PSTR("..."));
//     // AddLogBuffer(LOG_LEVEL_DEBUG,(uint8_t *)_bytes.raw, 15);
//     if (MESH.packetsAlreadyReceived.size() > MESH_MAX_PACKETS) {
//       MESH.packetsAlreadyReceived.erase(MESH.packetsAlreadyReceived.begin());
//       // ^^ if the queue of already received packets is full, delete the first one in the queue
//       // AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Erase received data"));
//     }

    // do something on the node
    // AddLogBuffer(LOG_LEVEL_DEBUG,(uint8_t *)&MESH.packetToConsume.front(), 30);

    MESHencryptPayload(&MESH.packetToConsume.front(), 0);  // decrypt the payload of the packet to consume
    switch (MESH.packetToConsume.front().type) {
      // case PACKET_TYPE_REGISTER_NODE: // NOTE:  This is handled directly in the receive call-back
      //   AddLog(LOG_LEVEL_INFO, PSTR("MSH: received topic: %s"), (char*)MESH.packetToConsume.front().payload + 6);
      //   // AddLogBuffer(LOG_LEVEL_INFO,(uint8_t *)&MESH.packetToConsume.front().payload,MESH.packetToConsume.front().chunkSize+5);
      //   for(auto &_peer : MESH.peers){
      //     if(memcmp(_peer.MAC,MESH.packetToConsume.front().sender,6)==0){
      //       strcpy(_peer.topic,(char*)MESH.packetToConsume.front().payload+6);
      //       MESHsubscribe((char*)&_peer.topic);
      //     }
      //   }
      //   break;
      case PACKET_TYPE_PEERLIST:  // add all peers on a peer list to own peer list
        for (uint32_t i = 0; i < MESH.packetToConsume.front().chunkSize; i += 6) {
          if (memcmp(MESH.packetToConsume.front().payload +i, MESH.sendPacket.sender, 6) == 0) {
            continue;              // Do not add myself
          }
          if (MESHcheckPeerList(MESH.packetToConsume.front().payload +i) == false) {
            MESHaddPeer(MESH.packetToConsume.front().payload +i);
          }
        }
        break;
      case  PACKET_TYPE_MQTT:      // Redirected MQTT from node in packet [char* _space_ char*]
        {
          // AddLog(LOG_LEVEL_INFO, PSTR("MSH: Received node output '%s'"), (char*)MESH.packetToConsume.front().payload);
          // bump up the time of the last message from this peer
          uint32_t idx = 0;
          for (auto &_peer : MESH.peers){
            if (memcmp(_peer.MAC, MESH.packetToConsume.front().sender, 6) == 0) {
              _peer.lastMessageFromPeer = millis();
              // MESH.lastTeleMsgs[idx] = std::string(_data);  // store the message for display
              break;
            }
            idx++;
          }
          if (MESH.packetToConsume.front().chunks > 1) {  // if we need to reassemble multiple packets into a single MQTT message
            bool _foundMultiPacket = false;
            for (auto it = MESH.multiPackets.begin(); it != MESH.multiPackets.end(); ) {
              mesh_packet_combined_t _packet_combined = *it;
              // iterate through the vector of partially reassembled multi-packet  chunks
              //  AddLog(LOG_LEVEL_INFO, PSTR("MSH: Append to multipacket"));
              if (memcmp(_packet_combined.header.sender, MESH.packetToConsume.front().sender, 12) == 0) {  // if the headers match
                if (_packet_combined.header.counter == MESH.packetToConsume.front().counter) {  // and the message counter mathes
                  _foundMultiPacket = true;
                  memcpy(_packet_combined.raw + (MESH.packetToConsume.front().chunk * MESH_PAYLOAD_SIZE), MESH.packetToConsume.front().payload, MESH.packetToConsume.front().chunkSize);
                  // ^^ copy the payload
                  bitSet(_packet_combined.receivedChunks, MESH.packetToConsume.front().chunk);
                  // ^^ set the appropriate bit for the received chunk number
                  // AddLog(LOG_LEVEL_INFO, PSTR("MSH: Multipacket rcvd chunk mask 0x%08X"), _packet_combined.receivedChunks);
                  uint32_t _temp = (1 << (uint8_t)MESH.packetToConsume.front().chunks) -1; //example: 1+2+4 == (2^3)-1
                  //^^ this is a filled bit-mask denoting all possible chunks received of the message for which the packet to consume is a chunk of
                  // AddLog(LOG_LEVEL_INFO, PSTR("MSH: _temp: %u = %u"),_temp,_packet_combined.receivedChunks);
                  if (_packet_combined.receivedChunks == _temp) {  // if all of the packets have been received
                    char * _data = (char*)_packet_combined.raw + strlen((char*)_packet_combined.raw) + 1;
                    // AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Publish multipacket"));
                    MqttPublishPayload((char*)_packet_combined.raw, _data); // publish it
                    it=MESH.multiPackets.erase(it);  // release the storage space
                    break;
                  }
                }
              } else {
                ++it;// move to the next reassembly vector
              }
            }
            if (!_foundMultiPacket) { // if we didn't find any matching packets in the multi-packet re-assembly storage
              mesh_packet_combined_t _packet;  // create a new re-assembly packet
              memcpy(_packet.header.sender, MESH.packetToConsume.front().sender, sizeof(_packet.header));
              memcpy(_packet.raw + (MESH.packetToConsume.front().chunk * MESH_PAYLOAD_SIZE), MESH.packetToConsume.front().payload, MESH.packetToConsume.front().chunkSize);
              _packet.receivedChunks = 0;
              bitSet(_packet.receivedChunks, MESH.packetToConsume.front().chunk);
              MESH.multiPackets.push_back(_packet);  // and push it into the storage vector
  //            AddLog(LOG_LEVEL_INFO, PSTR("MSH: New multipacket with chunks %u"), _packet.header.chunks);
            }
          } else {  // if this is not a multi-packet message, directly deal with it
  //          AddLog(LOG_LEVEL_INFO, PSTR("MSH: chunk: %u size: %u"), MESH.packetToConsume.front().chunk, MESH.packetToConsume.front().chunkSize);
  //          if (MESH.packetToConsume.front().chunk==0) AddLogBuffer(LOG_LEVEL_INFO,(uint8_t *)&MESH.packetToConsume.front().payload,MESH.packetToConsume.front().chunkSize);
            char * _data = (char*)MESH.packetToConsume.front().payload + strlen((char*)MESH.packetToConsume.front().payload) +1;
  //          AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Publish packet"));
            MqttPublishPayload((char*)MESH.packetToConsume.front().payload, _data);
  //          AddLogBuffer(LOG_LEVEL_INFO,(uint8_t *)&MESH.packetToConsume.front().payload,MESH.packetToConsume.front().chunkSize);
          }
          break;
        }
      default:  // NOTE:  time, register, and refresh packet types are handled in the high-priority call back
        AddLogBuffer(LOG_LEVEL_DEBUG, (uint8_t *)&MESH.packetToConsume.front(), MESH.packetToConsume.front().chunkSize +5);
        break;
    }
    MESH.packetToConsume.pop();  // remove the now consumed packet from the queue
//    AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Consumed one packet %u"), (char*)MESH.packetToConsume.size());
  }
}


/**
 * @brief Used to check status and health of the mesh.
 *
 * Requests regular topic updates from the nodes and removes them from the peer list if they have been gone for too long.
 */
void MESHEverySecond(void) {
  static uint32_t _second = 0;  // NOTE: static, not reinitialized every time fxn is run
  _second++;
  // send a time packet every x seconds
  if (MESH.flags.nodeWantsTimeASAP) {
    AddLog(LOG_LEVEL_INFO, PSTR("MSH: Node wants time from broker ASAP"));
    MESHsendTime();
    MESH.flags.nodeWantsTimeASAP = 0;
    return;
  }
  if (_second % 5 == 0) {
    if ((MESH.flags.nodeWantsTime == 1) || (_second % 30 == 0)) { //every 5 seconds on demand or every 30 seconds anyway
      AddLog(LOG_LEVEL_INFO, PSTR("MSH: Node wants time from broker or 30s since last time packet"));
      MESHsendTime();
      MESH.flags.nodeWantsTime = 0;
      return;
    }
  }
  uint32_t _peerNumber = _second%45; // get topic from peer every 45 seconds
  if (_peerNumber < MESH.peers.size()) {
    // if (MESH.peers[_peerNumber].topic[0] == 0) {
      AddLog(LOG_LEVEL_INFO, PSTR("MSH: Broker wants topic from peer %u"), _peerNumber);
      MESHdemandTopic(_peerNumber);
    // }
  }
  for (auto it = MESH.peers.begin(); it != MESH.peers.end(); ) {  // check on peers
    mesh_peer_t _peer=*it;
    if (millis() - _peer.lastMessageFromPeer > 70000) {  // if it's been more than 70s since we heard from the peer
      AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Peer with MAC %s and topic %s not seen in 70s, removing from list of peers"), _peer.MAC, _peer.topic);
      MESHunsubscribe((char*)&_peer.topic);  // Un-subscribe to the peer's topic
      it=MESH.peers.erase(it);  // erase the peer from the list
    } else {
       ++it;
    }
  }
  if (MESH.multiPackets.size() > MESH_BUFFERS) {
    // clean out memory space, if needed
    AddLog(LOG_LEVEL_INFO, PSTR("MSH: Multi packets in buffer %u"), MESH.multiPackets.size());
    MESH.multiPackets.erase(MESH.multiPackets.begin());
  }
}

#else  // ESP8266

/**
 * @brief This is used for lower-priority prosessing of mesh action queues.
 * On each iteration, handle the packets in the front of the packetToResend and packetToConsume vectors.
 */
void MESHevery50MSecond(void) {
  if (ROLE_NONE == MESH.role) { return; }

  if (MESH.packetToResend.size() > 0) {  // handle packets to resend
    AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Next packet %d to resend of type %u, TTL %u"),
      MESH.packetToResend.size(), MESH.packetToResend.front().type, MESH.packetToResend.front().TTL);
    if (MESH.packetToResend.front().TTL > 0) {  // make sure it hasn't exceed time to live
      MESH.packetToResend.front().TTL--;
      if (memcmp(MESH.packetToResend.front().sender, MESH.broker, 6) != 0) { //do not send back the packet to the broker
        MESHsendPacket(&MESH.packetToResend.front());
      }
    } else {
      MESH.packetToResend.pop();  // toss the packet if exceeded TTL
    }
    // pass the packets
  }

  if (MESH.packetToConsume.size() > 0) {  // handle the next packet to consume
    MESHencryptPayload(&MESH.packetToConsume.front(), 0);
    switch (MESH.packetToConsume.front().type) {
      case PACKET_TYPE_MQTT:
        if (memcmp(MESH.packetToConsume.front().sender, MESH.sendPacket.sender, 6) == 0) {
          //discard echo (ie, a packet sent by me)
          break;
        }
        // AddLog(LOG_LEVEL_INFO, PSTR("MSH: node received topic: %s"), (char*)MESH.packetToConsume.front().payload);
        MESHreceiveMQTT(&MESH.packetToConsume.front());
        // NOTE:  No receive buffer!! only single packet messages supported!!
        break;
      case PACKET_TYPE_PEERLIST:
        for (uint32_t i = 0; i < MESH.packetToConsume.front().chunkSize; i += 6) {
          if (memcmp(MESH.packetToConsume.front().payload +i, MESH.sendPacket.sender, 6) == 0) {
            continue; //do not add myself
          }
          if (MESHcheckPeerList(MESH.packetToConsume.front().payload +i) == false) {
            MESHaddPeer(MESH.packetToConsume.front().payload +i);
          }
        }
        break;
      default:  // NOTE:  time and topic request packets are handled immediately in the receive callback
        break;
    }
    MESH.packetToConsume.pop(); // remove the packet from the queue
  }
}

/**
 * @brief Used to handle requests from the broker to refresh topics and to
 * deInit the mesh if the broker hasn't been heard from in too long.
 */
void MESHEverySecond(void) {
  if (MESH.role > ROLE_BROKER) {
    if (MESH.flags.brokerNeedsTopic == 1) {
      AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Broker wants topic"));
      MESHregisterNode(1); //refresh info
      MESH.flags.brokerNeedsTopic = 0;
    }
    if (millis() - MESH.lastMessageFromBroker > 31000) {
      AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Broker not seen for >30 secs"));
      MESHregisterNode(1); //refresh info
    }
    if (millis() - MESH.lastMessageFromBroker > 70000) {
      AddLog(LOG_LEVEL_DEBUG, PSTR("MSH: Broker not seen for 70 secs, try to re-launch wifi"));
      MESH.role = ROLE_NONE;
      MESHdeInit();  // if we don't deinit after losing connection, we will get an error trying to reinit later
      MESHsetWifi(1);
      WifiBegin(3, MESH.channel);
    }
  }
}

#endif  // ESP8266

/*********************************************************************************************\
 * Presentation
\*********************************************************************************************/

void MESHshow(bool json) {
  if (json) {
    if (ROLE_BROKER == MESH.role) {
      ResponseAppend_P(PSTR(",\"MESH\":{\"channel\":%u"), MESH.channel);
      ResponseAppend_P(PSTR(",\"nodes\":%u"),MESH.peers.size());
      if (MESH.peers.size() > 0) {
        ResponseAppend_P(PSTR(",\"MAC\":["));
        bool comma = false;
        for (auto &_peer : MESH.peers) {
          char _MAC[18];
          ToHex_P(_peer.MAC, 6, _MAC,18, ':');
          ResponseAppend_P(PSTR("%s\"%s\""), (comma)?",":"", _MAC);
          comma = true;
        }
        ResponseAppend_P(PSTR("]"));
      }
      ResponseJsonEnd();
    }
  } else {
#ifdef ESP32 //web UI only on the the broker = ESP32
    if (ROLE_BROKER == MESH.role) {
//      WSContentSend_PD(PSTR("TAS-MESH:<br>"));
      WSContentSend_PD(PSTR("<b>Broker MAC</b> %s<br>"), WiFi.softAPmacAddress().c_str());
      WSContentSend_PD(PSTR("<b>Broker Channel</b> %u<hr>"), WiFi.channel());
      uint32_t idx = 0;
      for (auto &_peer : MESH.peers) {
        char _MAC[18];
        ToHex_P(_peer.MAC, 6, _MAC, 18, ':');
        WSContentSend_PD(PSTR("<b>Node MAC</b> %s<br>"), _MAC);
        WSContentSend_PD(PSTR("<b>Node last message</b> %u ms<br>"), millis() - _peer.lastMessageFromPeer);
        WSContentSend_PD(PSTR("<b>Node MQTT topic</b> %s"), _peer.topic);
/*
        WSContentSend_PD(PSTR("Node MQTT topic: %s <br>"), _peer.topic);
        if (MESH.lastTeleMsgs.size() > idx) {
          char json_buffer[MESH.lastTeleMsgs[idx].length() +1];
          strcpy(json_buffer, (char*)MESH.lastTeleMsgs[idx].c_str());
          JsonParser parser(json_buffer);
          JsonParserObject root = parser.getRootObject();
          for (auto key : root) {
            JsonParserObject subObj = key.getValue().getObject();
            if (subObj) {
              WSContentSend_PD(PSTR("<ul>%s:"), key.getStr());
              for (auto subkey : subObj) {
                WSContentSend_PD(PSTR("<ul>%s: %s</ul>"), subkey.getStr(), subkey.getValue().getStr());
              }
              WSContentSend_PD(PSTR("</ul>"));
            } else {
              WSContentSend_PD(PSTR("<ul>%s: %s</ul>"), key.getStr(), key.getValue().getStr());
            }
          }
//          AddLog(LOG_LEVEL_INFO,PSTR("MSH: teleJSON %s"), (char*)MESH.lastTeleMsgs[idx].c_str());
//          AddLog(LOG_LEVEL_INFO,PSTR("MSH: stringsize: %u"),MESH.lastTeleMsgs[idx].length());
        } else {
//          AddLog(LOG_LEVEL_INFO,PSTR("MSH: telemsgSize: %u"),MESH.lastTeleMsgs.size());
        }
*/
        WSContentSend_PD(PSTR("<hr>"));
        idx++;
      }
    }
#endif  // ESP32
  }
}

/*********************************************************************************************\
 * Commands
\*********************************************************************************************/

const char kMeshCommands[] PROGMEM = "Mesh|"  // Prefix
  "Broker|Node|Peer|Channel|Interval";

void (* const MeshCommand[])(void) PROGMEM = {
  &CmndMeshBroker, &CmndMeshNode, &CmndMeshPeer, &CmndMeshChannel, &CmndMeshInterval };

void CmndMeshBroker(void) {
#ifdef ESP32  // only ESP32 currently supported as broker
  MESH.channel = WiFi.channel(); // The Broker gets the channel from the router, no need to declare it with MESHCHANNEL (will be mandatory set it when ETH will be implemented)
  MESHstartBroker();
  ResponseCmndNumber(MESH.channel);
#endif  // ESP32
}

void CmndMeshNode(void) {
#ifndef ESP32  // only ESP8266 current supported as node
  if (XdrvMailbox.data_len > 0) {
    MESHHexStringToBytes(XdrvMailbox.data, MESH.broker);
    if (XdrvMailbox.index != 0) { XdrvMailbox.index = 1; }    // Everything not 0 is a full node
    // meshnode FA:KE:AD:DR:ES:S1
    bool broker = false;
    char EspSsid[11];
    String mac_address = XdrvMailbox.data;
    snprintf_P(EspSsid, sizeof(EspSsid), PSTR("ESP_%s"), mac_address.substring(6).c_str());
    int32_t getWiFiChannel(const char *EspSsid);
    if (int32_t ch = WiFi.scanNetworks()) {
      for (uint8_t i = 0; i < ch; i++) {
        if (!strcmp(EspSsid, WiFi.SSID(i).c_str())) {
          MESH.channel = WiFi.channel(i);
          broker = true;
          AddLog(LOG_LEVEL_INFO, PSTR("MSH: Successfully connected to Mesh Broker using MAC %s as %s on channel %d"),
            XdrvMailbox.data, EspSsid, MESH.channel);
          MESHstartNode(MESH.channel, XdrvMailbox.index);
          ResponseCmndNumber(MESH.channel);
        }
      }
    }
    if (!broker) {
      AddLog(LOG_LEVEL_INFO, PSTR("MSH: No Mesh Broker found using MAC %s"), XdrvMailbox.data);
    }
  }
#endif  // ESP32
}

void CmndMeshPeer(void) {
  if (XdrvMailbox.data_len > 0) {
    uint8_t _MAC[6];
    MESHHexStringToBytes(XdrvMailbox.data, _MAC);
    char _peerMAC[18];
    ToHex_P(_MAC, 6, _peerMAC, 18, ':');
    AddLog(LOG_LEVEL_DEBUG,PSTR("MSH: MAC-string %s (%s)"), XdrvMailbox.data, _peerMAC);
    if (MESHcheckPeerList((const uint8_t *)_MAC) == false) {
      MESHaddPeer(_MAC);
      MESHcountPeers();
      ResponseCmndChar(_peerMAC);
    } else if (WiFi.macAddress() == String(_peerMAC) || WiFi.softAPmacAddress() == String(_peerMAC)){
      // a device can be added as its own peer, but every send will result in a ESP_NOW_SEND_FAIL
      AddLog(LOG_LEVEL_DEBUG,PSTR("MSH: device %s cannot be a peer of itself"), XdrvMailbox.data, _peerMAC);
    } else {
      AddLog(LOG_LEVEL_DEBUG,PSTR("MSH: %s is already on peer list, will not add"), XdrvMailbox.data, _peerMAC);
    }
  }
}

void CmndMeshChannel(void) {
  if ((XdrvMailbox.payload > 0) && (XdrvMailbox.payload < 14)) {
    MESH.channel = XdrvMailbox.payload;
  }
  ResponseCmndNumber(MESH.channel);
}

void CmndMeshInterval(void) {
  if ((XdrvMailbox.payload > 1) && (XdrvMailbox.payload < 201)) {
    MESH.interval = XdrvMailbox.payload;   // 2 to 200 ms
    MESHsetSleep();
  }
  ResponseCmndNumber(MESH.interval);
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xdrv57(uint8_t function) {
  bool result = false;

  switch (function) {
    case FUNC_COMMAND:
      result = DecodeCommand(kMeshCommands, MeshCommand);
      break;
    case FUNC_PRE_INIT:
      MESHInit();                  // TODO: save state
      break;
  }
  if (MESH.role) {
    switch (function) {
      case FUNC_LOOP:
        static uint32_t mesh_transceive_msecond = 0;             // State 50msecond timer
        if (TimeReached(mesh_transceive_msecond)) {
          SetNextTimeInterval(mesh_transceive_msecond, MESH.interval);
          MESHevery50MSecond();
        }
        break;
      case FUNC_EVERY_SECOND:
        MESHEverySecond();
        break;
#ifdef USE_WEBSERVER
      case FUNC_WEB_SENSOR:
        MESHshow(0);
        break;
#endif
      case FUNC_JSON_APPEND:
        MESHshow(1);
        break;
#ifdef ESP32
      case FUNC_MQTT_SUBSCRIBE:
        MESHconnectMQTT();
        break;
#endif  // ESP32
      case FUNC_SHOW_SENSOR:
        MESHsendPeerList();          // Sync this to the Teleperiod with a delay
        break;
#ifdef USE_DEEPSLEEP
      case FUNC_SAVE_BEFORE_RESTART:
        MESHdeInit();  // NOTE: Node must manually re-init on wake (via rule or other method)
        break;
#endif  // USE_DEEPSLEEP
    }
  }
  return result;
}

#endif  // USE_TASMESH
