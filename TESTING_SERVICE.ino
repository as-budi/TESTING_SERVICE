#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <esp_now.h>
#include <unordered_map>
#include <vector>

#define EEPROM_SIZE 512

int uid = 4;

String wifiSSID = "Honor 8X";
String wifiPassword = "abcdefgh";
String mqttbroker = "test.mosquitto.org";
int mqtt_port = 1883;
WiFiClient client;
const char* mqtt_client_name = "SDevice";
const char* mqtt_service = "service";
const char* mqtt_rule = "rule";
PubSubClient mqtt(client);

std::vector<int> matchingResults;

StaticJsonDocument<200> serviceDoc;

int r_id[1];
int t_id[3];
int t_val[3];
int s_id[1];
int s_val[2];

int s_value;
int t_index = 0;

//untuk memasukkan data br ke fungsi match data
int s_br;

bool executeMatchData = false;

//time variable
unsigned long wifiTimeout = 5000;  // 5 seconds
unsigned long previousMillis = 0;  // millis() returns an unsigned long.
const long interval = 20000;       //jalankan ESP-NOW mode selama 20 detik

bool MQTT_connected = false;


//variable UDP
int packetSize;
int len;

//state declaration
static unsigned int state;

int s_val_broadcast;

typedef struct broadcast_message {
  int device_id;
  int device_val;
} broadcast_message;
broadcast_message message;

int getNumberOfTId() {
    return t_index;
}

int findLastUsedAddress() {
  int address = 0;
  byte value = EEPROM.read(address);
  
  // Find the last used address by looking for the break value (254)
  while (value != 255 && address < EEPROM.length()) {
    address += sizeof(byte);
    value = EEPROM.read(address);
  }

  return address;
}



void connectWifi();
void connect_mqtt();

class DataLog {
private:
    std::unordered_map<int, int> data_log;

public:
    void receiveData(int trigger_id, int trigger_value) {
        auto it = data_log.find(trigger_id);

        if (it != data_log.end()) {
            // Update existing entry
            it->second = trigger_value;
            Serial.printf("Updated data for trigger_id %d: %d\n", trigger_id, trigger_value);
        } else {
            // Add new entry
            data_log[trigger_id] = trigger_value;
            Serial.printf("Received data for trigger_id %d: %d\n", trigger_id, trigger_value);
        }
    }

    void showDataLog() const {
        Serial.println("Data Log:");
        for ( auto& entry : data_log) {
            Serial.printf("Trigger_id: %d, Trigger_value: %d\n", entry.first, entry.second);
        }
    }

    const std::unordered_map<int, int>& getDataLog() const {
      return data_log;
    }
};

DataLog service;

void storeDataInEEPROM(int t_index, int* t_id, int* t_val, int* s_val) {
  int eepromAddress = findLastUsedAddress();

  EEPROM.write(eepromAddress, 254);
  EEPROM.commit();
  eepromAddress += sizeof(byte);

  // Store t_index in EEPROM
  EEPROM.write(eepromAddress, t_index);
  EEPROM.commit();
  eepromAddress += sizeof(byte);

  for (int i = 0; i < t_index; i++) {
    // Store t_id and t_val in EEPROM
    EEPROM.writeInt(eepromAddress, t_id[i]);
    EEPROM.writeInt(eepromAddress + sizeof(byte), t_val[i]);
    EEPROM.commit();
    eepromAddress += sizeof(int);
  }

  // Store s_val in EEPROM
  EEPROM.writeInt(eepromAddress, s_val[0]);
  EEPROM.commit();
  // Read and print data from EEPROM for verification
  for (int i = 0; i <= eepromAddress; i++) {
    Serial.print("Address ");
    Serial.print(i);
    Serial.print(": ");
    Serial.println(EEPROM.read(i));
  }
}


void callback(char* topic, byte* payload, unsigned int length) {
  // Callback ini akan dipanggil ketika pesan MQTT diterima
  Serial.print("Data dari topik : ");
  Serial.println(topic);

  t_index = 0; //reset t_id count

  if (strcmp(topic, mqtt_rule) == 0) {  //menerima data "rule"
    String message_rule;
    for (int i = 0; i < length; i++) {
      message_rule += (char)payload[i];
    }

    Serial.println(message_rule);

    // Parsing pesan JSON
    StaticJsonDocument<200> jsonDoc;  
    DeserializationError error = deserializeJson(jsonDoc, message_rule);
      if (error) {
      Serial.print("Failed to parse JSON: ");
      Serial.println(error.c_str());
      return;
    }

    JsonObject trigger = jsonDoc["trigger"];
    JsonObject servis = jsonDoc["servis"];

    int rule_id = jsonDoc["rule_id"]; // 1

      // Iterate over trigger and extract values
      for (JsonPair kv : trigger) {
        t_id[t_index] = atoi(kv.key().c_str()); // Convert the key to an integer
        t_val[t_index] = kv.value().as<int>();
        t_index++;
      }

      // Iterate over servis and extract values
      int s_id[2];  // Assuming you have 2 elements in the servis object
      int s_val[2];
      int s_index = 0;
      for (JsonPair kv : servis) {
        s_id[s_index] = atoi(kv.key().c_str()); // Convert the key to an integer
        s_val[s_index] = kv.value().as<int>();
        if (s_id[s_index] == uid) {
          s_index++;
          break; // Break the loop when the first matching s_id is found
        }
      }

      if (s_index == 0) {
        Serial.println("No matching s_id found. Ignoring data.");
        return;
      }

      storeDataInEEPROM(t_index, t_id, t_val, s_val);

      Serial.println("Data diterima:");
      Serial.print("r_id: ");
      Serial.println(rule_id);
      for (int i = 0; i < t_index; i++) {
        Serial.print("t_id: ");
        Serial.println(t_id[i]);
        Serial.print("t_val: ");
        Serial.println(t_val[i]);
      }

      // Print matching s_id values
      for (int i = 0; i < s_index; i++) {
        if (s_id[i] == uid) {
          Serial.print("s_id: ");
          Serial.println(s_id[i]);
          Serial.print("s_val: ");
          Serial.println(s_val[i]);
        }
      }

        Serial.print("Number of t_id received: ");
        Serial.println(getNumberOfTId());
        
        
  
  } else if (strcmp(topic, mqtt_service) == 0) {  //menerima data "service"
    String message_service;
    for (int i = 0; i < length; i++) {
      message_service += (char)payload[i];
    }

    //menampilkan data rule dalam bentuk json
    Serial.println(message_service);

    StaticJsonDocument<200> jsonDoc;
    DeserializationError error = deserializeJson(jsonDoc, message_service);
    if (error) {
      Serial.print("Failed to parse JSON: ");
      Serial.println(error.c_str());
      return;
    }
  int s_id = jsonDoc["s_id"]; // 4
  int s_val = jsonDoc["s_val"]; // 1

    // Tampilkan data yang diterima
   if(s_id != uid){ 
    Serial.println("ID tidak sama");
   }
   else{
    Serial.println("Data diterima:");
    Serial.print("s_id: ");
    Serial.println(s_id);
    Serial.print("s_val: ");
    Serial.println(s_val);
   }
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  EEPROM.begin(EEPROM_SIZE);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  mqtt.setServer(mqttbroker.c_str(), 1883);
  mqtt.setCallback(callback);

  if (esp_now_init() != ESP_OK){
    Serial.println("Error Initializing ESP-NOW");
    return;
  }

  connect_mqtt();
  //readData();
  state = 1;

  
}

int matchData(int device_id, int device_val) {
  int eepromAddress = 0;
  int ruleCount = 0;
  int bestMatch = -1;  // Index of the best matching rule
  int bestMatchTriggerCount = -1;  // Number of triggers in the best matching rule

  // Iterate through EEPROM to find rules
  while (eepromAddress < EEPROM_SIZE) {
    byte value = EEPROM.read(eepromAddress);

    if (value == 254) {
      // Break when the break value (254) is encountered
      ruleCount++;
      eepromAddress += sizeof(byte);
      continue;
    }

    // Read t_index from EEPROM
    int t_index = EEPROM.read(eepromAddress);
    eepromAddress += sizeof(byte);

    // Read t_id and t_val from EEPROM and compare with received data
    bool match = true;
    int matchingTriggers = 0;

    for (int i = 0; i < t_index; i++) {
      int stored_t_id = EEPROM.readInt(eepromAddress);
      int stored_t_val = EEPROM.readInt(eepromAddress + sizeof(int));
      eepromAddress += sizeof(int);


      // Check if converted data matches stored data
      if (stored_t_id == t_id[i] && stored_t_val == t_val[i]) {
        matchingTriggers++;
      } else {
        match = false;
      }
    }

    // Read and compare s_val from EEPROM
    int stored_s_val = EEPROM.readInt(eepromAddress);
    eepromAddress += sizeof(int);

    // If all conditions are met, check for best match
    if (match && stored_s_val == device_val) {
      if (matchingTriggers > bestMatchTriggerCount) {
        bestMatch = ruleCount;
        bestMatchTriggerCount = matchingTriggers;
      }
    }
  }

  // Check if a best match was found
  if (bestMatch != -1) {
    // Check DataLog for correctness of values from other t_id
    for (int i = 0; i < t_index; i++) {
      if (i != bestMatchTriggerCount) {
        auto it = service.getDataLog().find(t_id[i]);
        if (it != service.getDataLog().end() && it->second == t_val[i]) {
          // The value from the other t_id is correct
          return EEPROM.read(bestMatch * sizeof(byte));  // Return the s_val from the best matching rule
        }
      }
    }
  }

  // If no best match or correct values from other t_id found, return 0 (no match)
  return 0;
}


// int matchData(int check_t_id, int check_t_val) {  //match data ESP-NOW with EEPROM
//     matchingResults.clear();

//   for (int i = 0; i < 1; i++) {                  // perulangan membaca 1 alamat EEPROM (sesuai dengan contoh pesan JSON)
//     for (int j = 0; j < getNumberOfTId(); j++) {                // perulangan mencari alamat dari t_id
//       int t_id_address = findLastUsedAddress() + (4 * i) + (4 * j) + 1;  // Alamat untuk t_id
//       int read_t_id = EEPROM.read(t_id_address);
//       if (read_t_id == 255) {  // jika bernilai 255 atau tidak ada data, maka berhenti cari
//         break;
//       }

//       if (check_t_id == read_t_id) {                            // jika data t_id yang dicari benar
//         int matching_t_ids = 0;
//         int matching_t_val = 0;

//         for (int k = 0; k < 3; k++){
//         int t_val_address = findLastUsedAddress() + (4 * i) + (4 * j) + (4 * k) + 2;
//         int read_t_val = EEPROM.readInt(t_val_address);
//           if (read_t_val == 255) {
//             break;
//           }

//           if (check_t_val == read_t_val) {
//             matching_t_ids++;
//             matching_t_val = read_t_val;
//           }
//         }
//         if (matching_t_ids == getNumberOfTId()) {
//           int s_val_address = findLastUsedAddress() + (4 * i) + (4 * j) + (4 * matching_t_ids) + 3;
//           s_val_broadcast = EEPROM.readInt(s_val_address);
//           Serial.println("Match data found : ");
//           Serial.print("t_id : ");
//           Serial.println(read_t_id);
//           Serial.print("t_val : ");
//           Serial.println(matching_t_val);
//           matchingResults.push_back(s_val_broadcast);
//         } else {
//           Serial.println("Data doesn't match, checking alternative...");

//           //check the alternative
//           if (matching_t_ids == 1) {
//             int s_val_broadcast = findLastUsedAddress() + (4 * i) + (4 * j) + (4 * matching_t_ids) + 3;
//             Serial.println("Alternative matching data found : ");
//             Serial.print("t_id : ");
//             Serial.println(read_t_id);
//             Serial.print("t_val : ");
//             Serial.println(matching_t_val);
//             matchingResults.push_back(s_val_broadcast);
//           } else {
//             Serial.println("Alternative not found");
//           }
//         }
//       }
//     }
//   }

//   if (!matchingResults.empty()) {
//     return matchingResults[0];
//   } else {
//       return s_val_broadcast;  // keluarkan nilai default jika tidak ditemukan
//   }
// }

void loop() {
  // put your main code here, to run repeatedly:
  unsigned long startMillis = millis();
  switch (state){
    case 1:
      if (MQTT_connected){
        state = 3; 
      } else if (!MQTT_connected) {
        state = 2;
      }
      break;
    case 2:
      WiFi.disconnect();//ESPNOW start
      esp_now_register_recv_cb(data_receive);
      
      if (executeMatchData){
      s_br = matchData(message.device_id, message.device_val);
      Serial.print("Running program data from EEPROM with: ");
      Serial.println(s_br);
      if (s_br == 1){
        Serial.println("Data found in EEPROM, start running the program");
      } else {
        Serial.println("Failed to find data in EEPROM, failed to execute the program");
      }
      executeMatchData = false;
      }

    delay(1000);
    break;

    case 3: 
       mqtt.loop();
       if (!mqtt.connect(mqtt_client_name)) {
        Serial.println("Failed to Connect to MQTT");
        state = 2;
       }
        if (WiFi.status() != WL_CONNECTED){
          state = 2;
        }
        break;
      }

  }
 

void data_receive(const uint8_t* mac, const uint8_t* incomingData, int len) {
  memcpy(&message, incomingData, sizeof(message));
  Serial.print("Bytes received: ");
  Serial.println(len);
  Serial.print("dev_id: ");
  Serial.println(message.device_id);
  Serial.print("dev_val: ");
  Serial.println(message.device_val);
  Serial.println();
  service.receiveData(message.device_id, message.device_val);

  //Set flag to excecute matchData
  executeMatchData = true;
}

void connect_mqtt(){
  Serial.println("Connecting to WiFi...");

  unsigned long startMillis = millis();

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
    if (millis() - startMillis > wifiTimeout) {
      Serial.println("\nFailed to connect to WiFi");
      MQTT_connected = false;
      return;
    }
  }

  Serial.println("Connected to WiFi");

  mqtt.setServer(mqttbroker.c_str(), mqtt_port);
  mqtt.setCallback(callback);  // Mengatur callback yang akan dipanggil ketika pesan diterima
  if (mqtt.connect(mqtt_client_name)) {
    Serial.println("Connected to MQTT Broker");
    Serial.println(WiFi.SSID());
    Serial.println(WiFi.RSSI());
    Serial.println(WiFi.macAddress());
    Serial.println(WiFi.localIP());
    Serial.println(WiFi.gatewayIP());
    Serial.println(WiFi.dnsIP());
    mqtt.subscribe(mqtt_rule);     // Subscribe ke topik yang sama dengan pengirim
    mqtt.subscribe(mqtt_service);  // Subscribe ke topik yang sama dengan pengirim
    MQTT_connected = true;
  } else {
    Serial.println("Failed to connect to MQTT Broker");
    MQTT_connected = false;
  }
}


