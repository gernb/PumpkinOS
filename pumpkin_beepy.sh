#!/bin/bash

# Save the original values
original_mono_cutoff=$(sudo cat /sys/module/sharp_drm/parameters/mono_cutoff)
original_touch_act=$(sudo cat /sys/module/beepy_kbd/parameters/touch_act)
original_touch_as=$(sudo cat /sys/module/beepy_kbd/parameters/touch_as)
original_fb_console_bind=$(sudo cat /sys/class/vtconsole/vtcon1/bind)

restore_values() {
  echo "$original_mono_cutoff" | sudo tee /sys/module/sharp_drm/parameters/mono_cutoff &>/dev/null
  echo "$original_touch_act" | sudo tee /sys/module/beepy_kbd/parameters/touch_act &>/dev/null
  echo "$original_touch_as" | sudo tee /sys/module/beepy_kbd/parameters/touch_as &>/dev/null
  echo "$original_fb_console_bind" | sudo tee /sys/class/vtconsole/vtcon1/bind &>/dev/null
}

# Trap signals to ensure cleanup
trap 'restore_values; exit' EXIT INT TERM

# Setup new values
echo 127 | sudo tee /sys/module/sharp_drm/parameters/mono_cutoff &>/dev/null
echo always | sudo tee /sys/module/beepy_kbd/parameters/touch_act &>/dev/null
echo mouse | sudo tee /sys/module/beepy_kbd/parameters/touch_as &>/dev/null
echo 0 | sudo tee /sys/class/vtconsole/vtcon1/bind &>/dev/null

# Execute the command
LD_LIBRARY_PATH=./bin ./pumpkin -d 1 ${@} -s libscriptlua.so ./script/pumpkin_beepy.lua

