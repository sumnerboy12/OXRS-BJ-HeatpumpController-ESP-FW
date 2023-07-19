# ESP Heatpump Controller firmware for [OXRS](https://oxrs.io)

Based on the excellent library [here](https://github.com/SwiCago/HeatPump) which enables control and monitoring of Mitsubishi heatpumps via an Arduino.

That library contains documentation and installation instructions as well as example sketches.

## Home Assistant

If you want to integrate your heatpump with Home Assistant the following YAML might help. 

The resulting widget in Home Assistant should look something like;

![image](https://github.com/sumnerboy12/OXRS-BJ-HeatpumpController-ESP-FW/assets/5060842/266a661f-6a4f-4a6f-9780-ab798e042b92)

You will just need to change all the topics to match your MQTT client ID in the code below;

```yaml
mqtt:
  climate:
    - name: Heatpump
      unique_id: heatpump
      object_id: heatpump
      optimistic: true
      availability_topic: stat/heatpump/lwt
      availability_template: '{% if value_json.online == true %}online{% else %}offline{% endif %}'
      current_temperature_topic: tele/heatpump
      current_temperature_template: '{{ value_json.roomTemperature }}'
      device:
        name: heatpump
        model: OXRS-BJ-HeatpumpController-ESP-FW
        identifiers:
          - heatpump
      fan_modes:
        - 'auto'
        - '1'
        - '2'
        - '3'
        - '4'
      fan_mode_command_topic: cmnd/heatpump
      fan_mode_command_template: '{"fan": "{{ value | upper }}"}'
      fan_mode_state_topic: stat/heatpump
      fan_mode_state_template: '{{ value_json.fan | lower }}'
      modes:
        - 'off'
        - 'heat'
        - 'dry'
        - 'cool'
        - 'auto'
      mode_command_topic: cmnd/heatpump
      mode_command_template: '{% if value == "off" %}{"power": "OFF"}{% else %}{"power": "ON", "mode": "{{ value | upper }}"}{% endif %}'
      mode_state_topic: stat/heatpump
      mode_state_template: '{% if value_json.power == "OFF" %}off{% else %}{{ value_json.mode | lower }}{% endif %}'
      power_command_topic: cmnd/heatpump
      power_command_template: '{"power": "{{ value }}"}'
      temperature_command_topic: cmnd/heatpump
      temperature_command_template: '{"temperature": {{ value }}}'
      temperature_state_topic: stat/heatpump
      temperature_state_template: '{{ value_json.temperature }}'
      temperature_unit: C
```
