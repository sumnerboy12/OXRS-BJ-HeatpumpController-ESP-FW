/**
  ESP8266 Mistubishi heatpump controller firmware for the Open eXtensible Rack System
  
  GitHub repository:
    https://github.com/sumnerboy12/OXRS-BJ-HeatpumpController-ESP-FW
    
  Copyright 2021 Ben Jones <ben.jones12@gmail.com>
*/

/*--------------------------- Macros ----------------------------------*/
#define STRINGIFY(s) STRINGIFY1(s)
#define STRINGIFY1(s) #s

/*--------------------------- Libraries -------------------------------*/
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>             // For MQTT
#include <OXRS_MQTT.h>                // For MQTT
#include <OXRS_API.h>                 // For REST API
#include <MqttLogger.h>               // For logging
#include <HeatPump.h>

/*--------------------------- Constants -------------------------------*/
// Serial
#define       SERIAL_BAUD_RATE        9600

// REST API
#define       REST_API_PORT           80

// How often to publish the internal heatpump temperature
#define       PUBLISH_TEMP_MS         60000

// How long before we revert to using the internal temperature
// instead of the remote temp supplied by an external system
#define       REMOTE_TEMP_TIMEOUT_MS  300000

/*--------------------------- Global Variables ------------------------*/
// the last time we published the room temp
uint32_t lastTempSend   = 0L;

// the last time a remote temp value has been received
uint32_t lastRemoteTemp = 0L;

// will send all packets received from the heatpump to the telemetry topic
// enabled/disabled by sending '{"debug": true|false}' to the command topic
bool debugEnabled       = false;

// stack size counter
char * stackStart;

/*--------------------------- Instantiate Globals ---------------------*/
// WiFi client
WiFiClient _client;

// MQTT client
PubSubClient _mqttClient(_client);
OXRS_MQTT _mqtt(_mqttClient);

// REST API
WiFiServer _server(REST_API_PORT);
OXRS_API _api(_mqtt);

// Logging (MQTT only since heatpump uses serial)
MqttLogger _logger(_mqttClient, "log", MqttLoggerMode::MqttAndSerial);

// heatpump client
HeatPump _heatpump;

/*--------------------------- Program ---------------------------------*/
uint32_t getStackSize()
{
  char stack;
  return (uint32_t)stackStart - (uint32_t)&stack;  
}

void _mqttCallback(char * topic, uint8_t * payload, unsigned int length) 
{
  // Pass down to our MQTT handler
  _mqtt.receive(topic, payload, length);
}

/**
  Heatpump callbacks
*/
void hpSettingsChanged() 
{
  heatpumpSettings settings = _heatpump.getSettings();

  const size_t bufferSize = JSON_OBJECT_SIZE(6);
  DynamicJsonDocument json(bufferSize);

  json["power"]       = settings.power;
  json["mode"]        = settings.mode;
  json["temperature"] = settings.temperature;
  json["fan"]         = settings.fan;
  json["vane"]        = settings.vane;
  json["wideVane"]    = settings.wideVane;
 
  _mqtt.publishStatus(json.as<JsonVariant>());
}

void hpStatusChanged(heatpumpStatus status) 
{
  const size_t bufferSize = JSON_OBJECT_SIZE(9);
  DynamicJsonDocument json(bufferSize);
  
  json["roomTemperature"]       = status.roomTemperature;
  json["operating"]             = status.operating;
  json["compressorFrequency"]   = status.compressorFrequency;

  JsonObject timers = json.createNestedObject("timers");  
  timers["mode"]                = status.timers.mode;
  timers["onMinutesSet"]        = status.timers.onMinutesSet;
  timers["onMinutesRemaining"]  = status.timers.onMinutesRemaining;
  timers["offMinutesSet"]       = status.timers.offMinutesSet;
  timers["offMinutesRemaining"] = status.timers.offMinutesRemaining;

  _mqtt.publishTelemetry(json.as<JsonVariant>());
}

void hpPacketDebug(byte * packet, unsigned int length, char * packetDirection) 
{
  if (!debugEnabled)
    return;
  
  _logger.print(F("[hpmp] ["));
  _logger.print(packetDirection);
  _logger.print(F("] "));
  _logger.write(packet, length);
  _logger.println();
}

/**
  Adoption info builders
*/
void getFirmwareJson(JsonVariant json)
{
  JsonObject firmware = json.createNestedObject("firmware");

  firmware["name"] = STRINGIFY(FW_NAME);
  firmware["shortName"] = STRINGIFY(FW_SHORT_NAME);
  firmware["maker"] = STRINGIFY(FW_MAKER);
  firmware["version"] = STRINGIFY(FW_VERSION);
}

void getSystemJson(JsonVariant json)
{
  JsonObject system = json.createNestedObject("system");

  system["heapUsedBytes"] = getStackSize();
  system["heapFreeBytes"] = ESP.getFreeHeap();
  system["flashChipSizeBytes"] = ESP.getFlashChipSize();

  system["sketchSpaceUsedBytes"] = ESP.getSketchSize();
  system["sketchSpaceTotalBytes"] = ESP.getFreeSketchSpace();

  FSInfo fsInfo;
  SPIFFS.info(fsInfo);
  
  system["fileSystemUsedBytes"] = fsInfo.usedBytes;
  system["fileSystemTotalBytes"] = fsInfo.totalBytes;
}

void getNetworkJson(JsonVariant json)
{
  byte mac[6];
  WiFi.macAddress(mac);
  
  char mac_display[18];
  sprintf_P(mac_display, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  JsonObject network = json.createNestedObject("network");

  network["mode"] = "wifi";
  network["ip"] = WiFi.localIP();
  network["mac"] = mac_display;
}

void getConfigSchemaJson(JsonVariant json)
{
  JsonObject configSchema = json.createNestedObject("configSchema");
  
  // Config schema metadata
  configSchema["$schema"] = JSON_SCHEMA_VERSION;
  configSchema["title"] = STRINGIFY(FW_SHORT_NAME);
  configSchema["type"] = "object";

  JsonObject properties = configSchema.createNestedObject("properties");

  // Firmware config
  JsonObject debug = properties.createNestedObject("debug");
  debug["type"] = "boolean";
}

void getCommandSchemaJson(JsonVariant json)
{
  JsonObject commandSchema = json.createNestedObject("commandSchema");
  
  // Command schema metadata
  commandSchema["$schema"] = JSON_SCHEMA_VERSION;
  commandSchema["title"] = STRINGIFY(FW_SHORT_NAME);
  commandSchema["type"] = "object";

  JsonObject properties = commandSchema.createNestedObject("properties");

  // Firmware commands
  JsonObject power = properties.createNestedObject("power");
  power["type"] = "boolean";

  JsonObject mode = properties.createNestedObject("mode");
  mode["type"] = "string";
  JsonArray modeEnum = mode.createNestedArray("enum");
  modeEnum.add("HEAT");
  modeEnum.add("DRY");
  modeEnum.add("COOL");
  modeEnum.add("FAN");
  modeEnum.add("AUTO");

  JsonObject temperature = properties.createNestedObject("temperature");
  temperature["type"] = "number";
  temperature["minimum"] = 10;
  temperature["maximum"] = 31;

  JsonObject fan = properties.createNestedObject("fan");
  fan["type"] = "string";
  JsonArray fanEnum = fan.createNestedArray("enum");
  fanEnum.add("AUTO");
  fanEnum.add("QUIET");
  fanEnum.add("1");
  fanEnum.add("2");
  fanEnum.add("3");
  fanEnum.add("4");

  JsonObject vane = properties.createNestedObject("vane");
  vane["type"] = "string";
  JsonArray vaneEnum = vane.createNestedArray("enum");
  vaneEnum.add("AUTO");
  vaneEnum.add("1");
  vaneEnum.add("2");
  vaneEnum.add("3");
  vaneEnum.add("4");
  vaneEnum.add("5");
  vaneEnum.add("SWING");

  JsonObject wideVane = properties.createNestedObject("wideVane");
  wideVane["type"] = "string";
  JsonArray wideVaneEnum = wideVane.createNestedArray("enum");
  wideVaneEnum.add("<<");
  wideVaneEnum.add("<");
  wideVaneEnum.add("|");
  wideVaneEnum.add(">");
  wideVaneEnum.add(">>");
  wideVaneEnum.add("<>");
  wideVaneEnum.add("SWING");

  JsonObject remoteTemp = properties.createNestedObject("remoteTemp");
  remoteTemp["type"] = "number";

  JsonObject custom = properties.createNestedObject("custom");
  custom["type"] = "string";

  JsonObject restart = properties.createNestedObject("restart");
  restart["type"] = "boolean";
}

/**
  API callbacks
*/
void _apiAdopt(JsonVariant json)
{
  // Build device adoption info
  getFirmwareJson(json);
  getSystemJson(json);
  getNetworkJson(json);
  getConfigSchemaJson(json);
  getCommandSchemaJson(json);
}

/**
  MQTT callbacks
*/
void _mqttConnected() 
{
  // MqttLogger doesn't copy the logging topic to an internal
  // buffer so we have to use a static array here
  static char logTopic[64];
  _logger.setTopic(_mqtt.getLogTopic(logTopic));

  // Publish device adoption info
  DynamicJsonDocument json(JSON_ADOPT_MAX_SIZE);
  _mqtt.publishAdopt(_api.getAdopt(json.as<JsonVariant>()));

  // Log the fact we are now connected
  _logger.println("[hpmp] mqtt connected");
}

void _mqttConfig(JsonVariant json)
{
  if (json.containsKey("debug"))
  {
    debugEnabled = json["debug"].as<bool>();
  }
}

void _mqttCommand(JsonVariant json)
{
  if (json.containsKey("power"))
  {
    _heatpump.setPowerSetting(json["power"].as<boolean>());
  }

  if (json.containsKey("mode"))
  {
    _heatpump.setModeSetting(json["mode"].as<const char *>());
  }

  if (json.containsKey("temperature"))
  {
    _heatpump.setTemperature(json["temperature"].as<float>());
  }

  if (json.containsKey("fan"))
  {
    _heatpump.setFanSpeed(json["fan"].as<const char *>());
  }

  if (json.containsKey("vane"))
  {
    _heatpump.setVaneSetting(json["vane"].as<const char *>());
  }

  if (json.containsKey("wideVane"))
  {
    _heatpump.setWideVaneSetting(json["wideVane"].as<const char *>());
  }

  if (json.containsKey("remoteTemp"))
  {
    _heatpump.setRemoteTemperature(json["remoteTemp"].as<float>()); 
    lastRemoteTemp = millis();
  } 

  if (json.containsKey("custom")) 
  {
    String custom = json["custom"];

    // copy custom packet to char array (+1 for the NULL at the end)
    char buffer[(custom.length() + 1)];
    custom.toCharArray(buffer, (custom.length() + 1));

    // max custom packet bytes is 20
    byte bytes[20];
    int byteCount = 0;
    char * nextByte;

    // loop over the byte string, breaking it up by spaces (or at the end of the line - \n)
    nextByte = strtok(buffer, " ");
    while (nextByte != NULL && byteCount < 20) {
      bytes[byteCount] = strtol(nextByte, NULL, 16); // convert from hex string
      nextByte = strtok(NULL, "   ");
      byteCount++;
    }

    // dump the packet so we can see what it is
    // handy because you can run the code without connecting the ESP to the heatpump, 
    // and test sending custom packets
    hpPacketDebug(bytes, byteCount, "customPacket");

    // send the pack to the heatpump for processing
    _heatpump.sendCustomPacket(bytes, byteCount);
  } 

  if (json.containsKey("restart") && json["restart"].as<bool>())
  {
    ESP.restart();
  }
}

/**
  Initialisation
*/
void initialiseSerial()
{
  Serial.begin(SERIAL_BAUD_RATE);
  delay(1000);
  
  _logger.println(F("[hpmp] starting up..."));

  DynamicJsonDocument json(128);
  getFirmwareJson(json.as<JsonVariant>());

  _logger.print(F("[hpmp] "));
  serializeJson(json, _logger);
  _logger.println();
}

void initialseWifi(byte * mac)
{
  // Ensure we are in the correct WiFi mode
  WiFi.mode(WIFI_STA);

  // Connect using saved creds, or start captive portal if none found
  // Blocks until connected or the portal is closed
  WiFiManager wm;  
  if (!wm.autoConnect("OXRS_WiFi", "superhouse"))
  {
    // If we are unable to connect then restart
    ESP.restart();
  }
  
  // Get ESP8266 base MAC address
  WiFi.macAddress(mac);

  // Format the MAC address for display
  char mac_display[18];
  sprintf_P(mac_display, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // Display MAC/IP addresses on serial
  _logger.print(F("[hpmp] mac address: "));
  _logger.println(mac_display);  
  _logger.print(F("[hpmp] ip address: "));
  _logger.println(WiFi.localIP());
}

void initialiseMqtt(byte * mac)
{
  // Set the default client id to the last 3 bytes of the MAC address
  char clientId[32];
  sprintf_P(clientId, PSTR("%02x%02x%02x"), mac[3], mac[4], mac[5]);  
  _mqtt.setClientId(clientId);
  
  // Register our callbacks
  _mqtt.onConnected(_mqttConnected);
  _mqtt.onConfig(_mqttConfig);
  _mqtt.onCommand(_mqttCommand);  
  
  // Start listening for MQTT messages
  _mqttClient.setCallback(_mqttCallback);  
}

void initialiseRestApi(void)
{
  // NOTE: this must be called *after* initialising MQTT since that sets
  //       the default client id, which has lower precendence than MQTT
  //       settings stored in file and loaded by the API

  // Set up the REST API
  _api.begin();

  // Register our callbacks
  _api.onAdopt(_apiAdopt);

  _server.begin();
}

void initialiseHeatpump()
{
  // Give the serial port time to display this message before
  // initialising the serial connection to the heat pump
  _logger.println(F("[hpmp] starting serial connection to heatpump (no more serial logging)"));
  _logger.setMode(MqttLoggerMode::MqttOnly);

  delay(1000);
  
  _heatpump.setSettingsChangedCallback(hpSettingsChanged);
  _heatpump.setStatusChangedCallback(hpStatusChanged);
  _heatpump.setPacketCallback(hpPacketDebug);  

  _heatpump.connect(&Serial);
}

/**
  Setup
*/
void setup() 
{
  // Store the address of the stack at startup so we can determine
  // the stack size at runtime (see getStackSize())
  char stack;
  stackStart = &stack;

  // Set up serial
  initialiseSerial();  

  // Set up network and obtain an IP address
  byte mac[6];
  initialseWifi(mac);

  // Set up MQTT (don't attempt to connect yet)
  initialiseMqtt(mac);

  // Set up the REST API
  initialiseRestApi();

  // Set up the heatpump client (this will change the serial baud rate)
  initialiseHeatpump();
}

/**
  Main processing loop
*/
void loop() 
{
  // Check our MQTT broker connection is still ok
  _mqtt.loop();

  // Handle any REST API requests
  WiFiClient client = _server.available();
  _api.loop(&client);

  // Check for any updates to the heatpump
  _heatpump.sync();

  // Publish temp periodically
  if ((millis() - lastTempSend) >= PUBLISH_TEMP_MS) {
    hpStatusChanged(_heatpump.getStatus());
    lastTempSend = millis();
  }

  // Reset to local temp sensor after 5 minutes of no remote temp udpates
  if ((millis() - lastRemoteTemp) >= REMOTE_TEMP_TIMEOUT_MS) {
    _heatpump.setRemoteTemperature(0);
    lastRemoteTemp = millis();
  }
}