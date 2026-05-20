# ESP32 Automatic Blinds

This is a library for a custom PCB that is meant to automatically control shades that use the Somfy RTS protocol or similar RF protocols. The PCB contains an ESP32, a CC1101 transceiver, a BH1750 light sensor, and a 3.5" IPS Capacitive touchscreen display from Hosyond.

## Initial Setup

- The first step is to clone this github repository into an IDE that supports platformio, and then flash the firmware to your esp32.

### Using the Touchscreen Interface
1. Locate an existing remote for your blinds and make sure it supports RTS (it should say something like that on the back)
2. Hold the programming button on the back of your remote until the shade "jogs" (rapid up and down movement)
3. Open the `PAIRING` settings menu on the interface, and press the `Send PROG` button. The shade should jog again to indicate that it's paired. If it doesn't, you may need to press the `Send PROG` button multiple times. Once the shade jogs, press the `Mark Paired` button to mark it as paired. (The device is unable to self-detect pairing as the RTS protocol is only one-way to the shade)
4. Measure the amount of time your shade takes to go up and down, and input those times in seconds into the corresponding text inputs (`Shade Up Time` and `Shade Down Time`). This will help with the shade position estimation.
5. Press the `Link Remote` button in the same menu, and then press any button on your existing remotes. For this step, the device will automatically detect the remote and mark it as linked. This will allow the device to receive signals from existing remotes so it can more accurately estimate position of the shade/blinds.
6. Go back to `Home`, and then enter the `AUTOMATION` settings menu. Here, you can adjust the automation settings such as light sensor reading frequency, light level thresholds for shade up/down, and the time a certain light level must persist before the device sends a command to the shade. You may need to do some testing to find the right thresholds for your specific case.

### Using the web UI
1. Connect to the esp32's setup AP (named `AutoBlinds-XXXXXX`) and open `http://AutoBlinds.local` on your browser.
2. Locate an existing remote for your blinds and make sure it supports RTS (it should say something like that on the back)
3. Measure the amount of time your shade takes to go up and down, and input those times in seconds into the corresponding text inputs under `Shade Settings`. This will help with the shade position estimation.
4. Hold the programming button on the back of your remote until the shade "jogs" (rapid up and down movement)
5. Press the `Send Prog` button on the web UI. The shade should jog again to indicate that it's paired. If it doesn't, you may need to press the `Send Prog` button multiple times. Once the shade jogs, press the `Mark Paired` button to mark it as paired. (The device is unable to self-detect pairing as the RTS protocol is only one-way to the shade).
6. Press the `Learn Existing Remote` button and then press any button on your existing remote. The remote will appear under `Shade Settings`.
7. Change the automation settings as you wish under `Automation`.

## Notes

The shade position is estimated from the commands sent by this ESP32 and the configured travel times.

## Technical Details

- Stores one RTS remote address, one rolling code, one paired state, and one estimated shade position.
- Sends `up`, `down`, `my`, and `prog` RTS commands through the CC1101.
- Keeps WiFi, mDNS, NTP, radio, shade, and automation settings in ESP32 NVS so they survive reboot.
- Provides a small built-in web UI and touchscreen interface.
- Runs light level based automatic open/close logic through `AutomaticBlindController.cpp`.

## Default access

- If WiFi is not configured or connection fails, the ESP32 opens a setup AP named `AutoBlinds-XXXXXX`.
- After WiFi connects or after connecting to the setup AP, mDNS is available at `http://AutoBlinds.local` unless the hostname is changed.
