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
// How often to publish status
#define       PUBLISH_STAT_MS         300000

// How often to publish telemetry
#define       PUBLISH_TELE_MS         60000

// How long before we revert to using the internal temperature
// instead of the remote temp supplied by an external system
#define       REMOTE_TEMP_TIMEOUT_MS  300000

/*--------------------------- Global Variables ------------------------*/
// the last time we published status
uint32_t lastStatPublish    = 0L;

// the last time we published telemetry
uint32_t lastTelePublish    = 0L;

// the last time a remote temp value has been received
uint32_t lastRemoteTemp     = 0L;

// will send all packets received from the heatpump to the telemetry topic
// enabled/disabled by sending '{"debug": true|false}' to the command topic
bool debugEnabled           = false;

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

  JsonDocument json;
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
  JsonDocument json;
  json["roomTemperature"]       = status.roomTemperature;
  json["operating"]             = status.operating;

  // Work out what the operating state is - i.e. if in HEAT mode is it actually heating?
  heatpumpSettings settings = heatpump.getSettings();

  if (strcmp(settings.power, "ON") == 0) {
    if (status.operating) {
      if (strcmp(settings.mode, "HEAT") == 0) {
        json["operatingState"] = "heating";
      } else if (strcmp(settings.mode, "COOL") == 0) {
        json["operatingState"] = "cooling";
      } else if (strcmp(settings.mode, "AUTO") == 0) {
        if (status.roomTemperature > settings.temperature) {
          json["operatingState"] = "cooling";
        } else if (status.roomTemperature < settings.temperature) {
          json["operatingState"] = "heating";
        } else {
          json["operatingState"] = "idle";
        }
      } else if (strcmp(settings.mode, "DRY") == 0) {
        json["operatingState"] = "drying";
      }
    } else {
      json["operatingState"] = "idle";
    }
  } else {
    json["operatingState"] = "off";
  }

  JsonObject timers = json["timers"].to<JsonObject>();;  
  timers["mode"]                = status.timers.mode;
  timers["onMinutesSet"]        = status.timers.onMinutesSet;
  timers["onMinutesRemaining"]  = status.timers.onMinutesRemaining;
  timers["offMinutesSet"]       = status.timers.offMinutesSet;
  timers["offMinutesRemaining"] = status.timers.offMinutesRemaining;

  oxrs.publishTelemetry(json.as<JsonVariant>());
}

void hpPacket(byte * packet, unsigned int length, char * packetDirection) 
{
  if (!debugEnabled)
    return;
  
  String message;
  for (unsigned int idx = 0; idx < length; idx++) {
    // pad single hex digits with a 0
    if (packet[idx] < 16) {
      message += "0";
    }
    message += String(packet[idx], HEX) + " ";
  }

  oxrs.print(F("[hpmp] ["));
  oxrs.print(packetDirection);
  oxrs.print(F("] "));
  oxrs.print(message);
  oxrs.println();
}

void setConfigSchema()
{
  // Define our config schema
  JsonDocument json;

  // Firmware config
  JsonObject externalUpdate = json["externalUpdate"].to<JsonObject>();
  externalUpdate["title"] = "Enable IR Control";
  externalUpdate["type"] = "boolean";

  JsonObject debug = json["debug"].to<JsonObject>();
  debug["title"] = "Enable Debug Logging";
  debug["type"] = "boolean";

  // Add any Home Assistant config
  hass.setConfigSchema(json);

  // Pass our config schema down to the hardware library
  oxrs.setConfigSchema(json.as<JsonVariant>());
}

void setCommandSchema()
{
  // Define our config schema
  JsonDocument json;
  
  // Firmware commands
  JsonObject power = json["power"].to<JsonObject>();
  power["type"] = "string";
  JsonArray powerEnum = power["enum"].to<JsonArray>();
  powerEnum.add("OFF");
  powerEnum.add("ON");

  JsonObject mode = json["mode"].to<JsonObject>();
  mode["type"] = "string";
  JsonArray modeEnum = mode["enum"].to<JsonArray>();
  modeEnum.add("HEAT");
  modeEnum.add("DRY");
  modeEnum.add("COOL");
  modeEnum.add("FAN");
  modeEnum.add("AUTO");

  JsonObject temperature = json["temperature"].to<JsonObject>();
  temperature["type"] = "number";
  temperature["minimum"] = 10;
  temperature["maximum"] = 31;

  JsonObject fan = json["fan"].to<JsonObject>();
  fan["type"] = "string";
  JsonArray fanEnum = fan["enum"].to<JsonArray>();
  fanEnum.add("AUTO");
  fanEnum.add("QUIET");
  fanEnum.add("1");
  fanEnum.add("2");
  fanEnum.add("3");
  fanEnum.add("4");

  JsonObject vane = json["vane"].to<JsonObject>();
  vane["type"] = "string";
  JsonArray vaneEnum = vane["enum"].to<JsonArray>();
  vaneEnum.add("AUTO");
  vaneEnum.add("1");
  vaneEnum.add("2");
  vaneEnum.add("3");
  vaneEnum.add("4");
  vaneEnum.add("5");
  vaneEnum.add("SWING");

  JsonObject wideVane = json["wideVane"].to<JsonObject>();
  wideVane["type"] = "string";
  JsonArray wideVaneEnum = wideVane["enum"].to<JsonArray>();
  wideVaneEnum.add("<<");
  wideVaneEnum.add("<");
  wideVaneEnum.add("|");
  wideVaneEnum.add(">");
  wideVaneEnum.add(">>");
  wideVaneEnum.add("<>");
  wideVaneEnum.add("SWING");

  JsonObject remoteTemp = json["remoteTemp"].to<JsonObject>();
  remoteTemp["type"] = "number";

  JsonObject custom = json["custom"].to<JsonObject>();
  custom["type"] = "string";

  // Pass our command schema down to the hardware library
  oxrs.setCommandSchema(json.as<JsonVariant>());
}

void jsonConfig(JsonVariant json)
{
  if (json.containsKey("externalUpdate"))
  {
    if (json["externalUpdate"].as<bool>()) {
      heatpump.enableExternalUpdate();
    } else {
      heatpump.disableExternalUpdate();
    }
  }

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
    hpPacket(bytes, byteCount, (char*)"customPacket");

    // send the packet to the heatpump for processing
    heatpump.sendCustomPacket(bytes, byteCount);
  }

  // if we have any updates then send and check it was successful
  if (update && !heatpump.update()) {
    oxrs.println(F("[hpmp] updating heatpump settings failed"));
  }
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

  JsonDocument json;
  hass.getDiscoveryJson(json, id);

  json["name"] = "Heatpump";
  json["opt"] = false;

  json["curr_temp_t"] = oxrs.getMQTT()->getTelemetryTopic(topic);
  json["curr_temp_tpl"] = "{{ value_json.roomTemperature }}";

  json["act_t"] = oxrs.getMQTT()->getTelemetryTopic(topic);
  json["act_tpl"] = "{{ value_json.operatingState }}";
  
  JsonArray fanModes = json["fan_modes"].to<JsonArray>();
  fanModes.add("auto");
  fanModes.add("1");
  fanModes.add("2");
  fanModes.add("3");
  fanModes.add("4");

  json["fan_mode_cmd_t"] = oxrs.getMQTT()->getCommandTopic(topic);
  json["fan_mode_cmd_tpl"] = "{\"fan\":\"{{ value | upper }}\"}";
  json["fan_mode_stat_t"] = oxrs.getMQTT()->getStatusTopic(topic);
  json["fan_mode_stat_tpl"] = "{{ value_json.fan | lower }}";

  JsonArray modes = json["modes"].to<JsonArray>();
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
  heatpump.setPacketCallback(hpPacket);  

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

  // Publish status periodically
  if ((millis() - lastStatPublish) >= PUBLISH_STAT_MS) {
    hpSettingsChanged();
    lastStatPublish = millis();
  }

  // Publish telemetry periodically
  if ((millis() - lastTelePublish) >= PUBLISH_TELE_MS) {
    hpStatusChanged(heatpump.getStatus());
    lastTelePublish = millis();
  }

  // Reset to local temp sensor if no remote temp udpates
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