# RPC control of a Gen2 Shelly device over BLE 

## Overview

This is a minimal Mongoose OS ESP32 application that allows to call
a Shelly RPC method over BLE to toggle a Switch

## Controlling your Shelly

- Change `shelly.btname` to your Shelly device name in mos.yml
- Change the channel id `shelly.channel` to the channel you want to toggle
## Building

make

## Flashing

make localflash