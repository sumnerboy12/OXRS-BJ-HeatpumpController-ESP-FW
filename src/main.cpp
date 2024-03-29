/**
  Mistubishi heatpump controller firmware for the Open eXtensible Rack System
  
  GitHub repository:
    https://github.com/sumnerboy12/OXRS-BJ-HeatpumpController-ESP-FW
    
  Copyright 2023 Ben Jones <ben.jones12@gmail.com>
*/

/*--------------------------- Libraries -------------------------------*/
#include <Arduino.h>
#include <HeatPump.h>
#include <OXRS_HASS.h>

#if defined(OXRS_WT32_ETH01)
#include <OXRS_WT32ETH01.h>
OXRS_WT32ETH01 oxrs;
#elif defined(OXRS_ESP8266)
#include <OXRS_8266.h>
OXRS_8266 oxrs;
#endif

/*--------------------------- Constants -------------------------------*/
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

// only want to publish the home assistant discovery config once
bool hassDiscoveryPublished = false;

/*--------------------------- Instantiate Globals ---------------------*/
// heatpump client
HeatPump heatpump;

// home assistant discovery config
OXRS_HASS hass(oxrs.getMQTT());

/*--------------------------- Program ---------------------------------*/
/**
  Heatpump callbacks
*/
void hpSettingsChanged() 
{
  heatpumpSettings settings = heatpump.getSettings();

  const size_t bufferSize = JSON_OBJECT_SIZE(6);
  DynamicJsonDocument json(bufferSize);

  json["power"]       = settings.power;
  json["mode"]        = settings.mode;
  json["temperature"] = settings.temperature;
  json["fan"]         = settings.fan;
  json["vane"]        = settings.vane;
  json["wideVane"]    = settings.wideVane;
 
  oxrs.publishStatus(json.as<JsonVariant>());
}

void hpStatusChanged(heatpumpStatus status) 
{
  const size_t bufferSize = JSON_OBJECT_SIZE(8);
  DynamicJsonDocument json(bufferSize);
  
  json["roomTemperature"]       = status.roomTemperature;
  json["operating"]             = status.operating;

  JsonObject timers = json.createNestedObject("timers");  
  timers["mode"]                = status.timers.mode;
  timers["onMinutesSet"]        = status.timers.onMinutesSet;
  timers["onMinutesRemaining"]  = status.timers.onMinutesRemaining;
  timers["offMinutesSet"]       = status.timers.offMinutesSet;
  timers["offMinutesRemaining"] = status.timers.offMinutesRemaining;

  oxrs.publishTelemetry(json.as<JsonVariant>());
}

void hpPacketDebug(byte * packet, unsigned int length, char * packetDirection) 
{
  if (!debugEnabled)
    return;
  
  oxrs.print(F("[hpmp] ["));
  oxrs.print(packetDirection);
  oxrs.print(F("] "));
  oxrs.write(packet, length);
  oxrs.println();
}

void setConfigSchema()
{
  // Define our config schema
  StaticJsonDocument<1024> json;

  // Firmware config
  JsonObject debug = json.createNestedObject("debug");
  debug["type"] = "boolean";

  // Add any Home Assistant config
  hass.setConfigSchema(json);

  // Pass our config schema down to the hardware library
  oxrs.setConfigSchema(json.as<JsonVariant>());
}

void setCommandSchema()
{
  // Define our config schema
  StaticJsonDocument<1024> json;
  
  // Firmware commands
  JsonObject power = json.createNestedObject("power");
  power["type"] = "string";
  JsonArray powerEnum = power.createNestedArray("enum");
  powerEnum.add("OFF");
  powerEnum.add("ON");

  JsonObject mode = json.createNestedObject("mode");
  mode["type"] = "string";
  JsonArray modeEnum = mode.createNestedArray("enum");
  modeEnum.add("HEAT");
  modeEnum.add("DRY");
  modeEnum.add("COOL");
  modeEnum.add("FAN");
  modeEnum.add("AUTO");

  JsonObject temperature = json.createNestedObject("temperature");
  temperature["type"] = "number";
  temperature["minimum"] = 10;
  temperature["maximum"] = 31;

  JsonObject fan = json.createNestedObject("fan");
  fan["type"] = "string";
  JsonArray fanEnum = fan.createNestedArray("enum");
  fanEnum.add("AUTO");
  fanEnum.add("QUIET");
  fanEnum.add("1");
  fanEnum.add("2");
  fanEnum.add("3");
  fanEnum.add("4");

  JsonObject vane = json.createNestedObject("vane");
  vane["type"] = "string";
  JsonArray vaneEnum = vane.createNestedArray("enum");
  vaneEnum.add("AUTO");
  vaneEnum.add("1");
  vaneEnum.add("2");
  vaneEnum.add("3");
  vaneEnum.add("4");
  vaneEnum.add("5");
  vaneEnum.add("SWING");

  JsonObject wideVane = json.createNestedObject("wideVane");
  wideVane["type"] = "string";
  JsonArray wideVaneEnum = wideVane.createNestedArray("enum");
  wideVaneEnum.add("<<");
  wideVaneEnum.add("<");
  wideVaneEnum.add("|");
  wideVaneEnum.add(">");
  wideVaneEnum.add(">>");
  wideVaneEnum.add("<>");
  wideVaneEnum.add("SWING");

  JsonObject remoteTemp = json.createNestedObject("remoteTemp");
  remoteTemp["type"] = "number";

  JsonObject custom = json.createNestedObject("custom");
  custom["type"] = "string";

  // Pass our command schema down to the hardware library
  oxrs.setCommandSchema(json.as<JsonVariant>());
}

void jsonConfig(JsonVariant json)
{
  if (json.containsKey("debug"))
  {
    debugEnabled = json["debug"].as<bool>();
  }

  // Handle any Home Assistant config
  hass.parseConfig(json);
}

void jsonCommand(JsonVariant json)
{
  bool update = false;

  if (json.containsKey("power"))
  {
    heatpump.setPowerSetting(json["power"].as<const char *>());
    update = true;
  }

  if (json.containsKey("mode"))
  {
    heatpump.setModeSetting(json["mode"].as<const char *>());
    update = true;
  }

  if (json.containsKey("temperature"))
  {
    heatpump.setTemperature(json["temperature"].as<float>());
    update = true;
  }

  if (json.containsKey("fan"))
  {
    heatpump.setFanSpeed(json["fan"].as<const char *>());
    update = true;
  }

  if (json.containsKey("vane"))
  {
    heatpump.setVaneSetting(json["vane"].as<const char *>());
    update = true;
  }

  if (json.containsKey("wideVane"))
  {
    heatpump.setWideVaneSetting(json["wideVane"].as<const char *>());
    update = true;
  }

  if (json.containsKey("remoteTemp"))
  {
    heatpump.setRemoteTemperature(json["remoteTemp"].as<float>()); 
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
    hpPacketDebug(bytes, byteCount, (char*)"customPacket");

    // send the pack to the heatpump for processing
    heatpump.sendCustomPacket(bytes, byteCount);
  }

  // do we need to send any updated settings
  if (update) heatpump.update();
}

void publishHassDiscovery()
{
  if (hassDiscoveryPublished)
    return;

  char topic[64];

  char component[8];
  sprintf_P(component, PSTR("climate"));

  char id[8];
  sprintf_P(id, PSTR("hvac"));

  DynamicJsonDocument json(2048);
  hass.getDiscoveryJson(json, id);

  json["name"] = "Heatpump";
  json["opt"] = true;

  json["curr_temp_t"] = oxrs.getMQTT()->getTelemetryTopic(topic);
  json["curr_temp_tpl"] = "{{ value_json.roomTemperature }}";

  JsonArray fanModes = json.createNestedArray("fan_modes");
  fanModes.add("auto");
  fanModes.add("1");
  fanModes.add("2");
  fanModes.add("3");
  fanModes.add("4");

  json["fan_mode_cmd_t"] = oxrs.getMQTT()->getCommandTopic(topic);
  json["fan_mode_cmd_tpl"] = "{\"fan\":\"{{ value | upper }}\"}";
  json["fan_mode_stat_t"] = oxrs.getMQTT()->getStatusTopic(topic);
  json["fan_mode_stat_tpl"] = "{{ value_json.fan | lower }}";

  JsonArray modes = json.createNestedArray("modes");
  modes.add("off");
  modes.add("heat");
  modes.add("dry");
  modes.add("cool");
  modes.add("auto");

  json["mode_cmd_t"] = oxrs.getMQTT()->getCommandTopic(topic);
  json["mode_cmd_tpl"] = "{% if value == 'off' %}{\"power\":\"OFF\"}{% else %}{\"power\":\"ON\",\"mode\":\"{{ value | upper }}\"}{% endif %}";
  json["mode_stat_t"] = oxrs.getMQTT()->getStatusTopic(topic);
  json["mode_stat_tpl"] = "{% if value_json.power == 'OFF' %}off{% else %}{{ value_json.mode | lower }}{% endif %}";

  json["power_command_topic"] = oxrs.getMQTT()->getCommandTopic(topic);
  json["power_command_template"] = "{\"power\":\"{{ value }}\"}";

  json["temp_cmd_t"] = oxrs.getMQTT()->getCommandTopic(topic);
  json["temp_cmd_tpl"] = "{\"temperature\":{{ value }}}";
  json["temp_stat_t"] = oxrs.getMQTT()->getStatusTopic(topic);
  json["temp_stat_tpl"] = "{{ value_json.temperature }}";
  json["temp_unit"] = "C";

  // Only publish once on boot
  hassDiscoveryPublished = hass.publishDiscoveryJson(json, component, id);
}

/**
  Setup
*/
void setup() 
{
  // Start hardware
  oxrs.begin(jsonConfig, jsonCommand);

  // Set up our config/command schemas
  setConfigSchema();
  setCommandSchema();

  // Set up the heatpump callbacks  
  heatpump.setSettingsChangedCallback(hpSettingsChanged);
  heatpump.setStatusChangedCallback(hpStatusChanged);
  heatpump.setPacketCallback(hpPacketDebug);  

  // Turn on auto-update, so our state is always master
  heatpump.enableAutoUpdate();

  // Initialise the serial connection to the heat pump
  oxrs.println(F("[hpmp] starting connection to heatpump over serial"));
  heatpump.connect(&Serial);
}

/**
  Main processing loop
*/
void loop() 
{
  // Let hardware handle any events etc
  oxrs.loop();

  // Check for any updates to the heatpump
  heatpump.sync();

  // Publish temp periodically
  if ((millis() - lastTempSend) >= PUBLISH_TEMP_MS) {
    hpStatusChanged(heatpump.getStatus());
    lastTempSend = millis();
  }

  // Reset to local temp sensor after 5 minutes of no remote temp udpates
  if ((millis() - lastRemoteTemp) >= REMOTE_TEMP_TIMEOUT_MS) {
    heatpump.setRemoteTemperature(0);
    lastRemoteTemp = millis();
  }

   // Check if we need to publish any Home Assistant discovery payloads
  if (hass.isDiscoveryEnabled())
  {
    publishHassDiscovery();
  }
}