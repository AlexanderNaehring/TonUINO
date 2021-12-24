Buttons:
=====================
* B0: middle button / enter / play / pause
* B1: right button / up / next
* B2: left button / down / previous

During idle:
------------
* Hold B0 (middle) for 5 seconds - Enter menu

During playback:
----------------
* Click B0: play/pause
* Click B1: volume up
* Click B2: volume down
* Hold B0 for 5 seconds: Reset progress to track 1 (story book mode)
* Hold B0 for 5 seconds: Single track repeat (all modes, except story book mode)
* Hold B1 for 2 seconds: Skip to the next track ((v)album, (v)party and story book mode)
* Hold B2 for 2 seconds: Skip to the previous track ((v)album, (v)party and story book mode)

In menu:
--------------------
* Click B0: Confirm selection
* Click B1: Next option
* Click B2: Previous option
* Double click B0: Announce current option
* Hold B0: Cancel parents menu or any submenu
* Hold B1: Jump 10 options forward
* Hold B2: Jump 10 options backwards

During NFC tag setup mode:
--------------------------
* Click B0: Confirm selection
* Click B1: Next folder, mode or track
* Click B2: Previous folder, mode or track
* Click B3: Jump 10 folders or tracks backwards
* Click B4: Jump 10 folders or tracks forward
* Double click B0: Announce current folder, mode or track number
* Hold B0 for 2 seconds: Cancel nfc tag setup mode
* Hold B1 for 2 seconds: Jump 10 folders or tracks forward
* Hold B2 for 2 seconds: Jump 10 folders or tracks backwards

During power up:
----------------
* Hold B0 + B1 + B2 - Erase all eeprom contents (resets stored progress and preferences)



Status LED(s):
==============

There are two options for a status led (which are mutually exclusive!):

1) Connect an LED to pin 6 and uncomment the '#define STATUSLED'.
   TonUINO will signal various status information, for example:

   - Pulse slowly when TonUINO is idle.
   - Solid when TonUINO is playing a title.
   - Blink every 500ms when interactive in menus etc.
   - Blink every 100ms if the LOWVOLTAGE feature is active and the battery is low.
   - Burst 4 times when TonUINO is locked and 8 times when unlocked.
   - Burst 4 times when repeat is activated and 8 times when deactivated.

   There are some more signals, they try to be intuitive. You'll see.

2) Connect one (or even several) ws281x rgb LED(s) to pin 6 and uncomment
   the '#define STATUSLEDRGB' below. TonUINO will signal various status information
   in different patterns and colors, for example:

   - Pulse slowly green when TonUINO is idle.
   - Solid green when TonUINO is playing a title.
   - Blink yellow every 500ms when interactive in menus etc.
   - Blink red every 100ms if the LOWVOLTAGE feature is active and the battery is low.
   - Burst 4 times red when TonUINO is locked and 8 times green when unlocked.
   - Burst 4 times white when repeat is activated and 8 times white when deactivated.
   - Burst 8 times magenta when the track in story book mode is reset to 1.

   There are some more signals, they try to be intuitive. You'll see.

For the vanilla led basically any 5V led will do, just don't forget to put an appropriate
resistor in series. For the ws281x led(s) you have several options. Stripes, single
neo pixels etc. The author did test the fuctionality with an 'addressable Through-Hole 5mm RGB LED'
from Pololu [1]. Please make sure you put a capacitor of at least 10 uF between the ground
and power lines of the led and consider adding a 100 to 1k ohm resistor between pin 6
and the led's data in. In general make sure you have enough current available (especially if
you plan more than one led - each takes up to 60mA!) and don't source the current for the led(s)
from the arduino power rail! Consult your ws281x led vendors documentation for guidance!

The ammount of ws281x led(s) as well as the max brightness can be set in the configuration
section below. The defaults are: One led and 20% brightness.

[1] https://www.pololu.com/product/2535

Cubiekid:
=========

If you happen to have a CubieKid case and the CubieKid circuit board, this firmware
supports both shutdown methods. The inactivity shutdown timer is enabled by default,
the shutdown due to low battery voltage (which can be configured in the shutdown
section below) can be enabled by uncommenting the '#define LOWVOLTAGE' below.

The CubieKid case as well as the CubieKid circuit board, have been designed and developed
by Jens Hackel aka DB3JHF and can be found here: https://www.thingiverse.com/thing:3148200

Pololu switch:
==============

If you want to use a pololu switch with this firmware the shutdown pin logic needs
to be flipped from HIGH (on) -> LOW (off) to LOW (on) -> HIGH (off). This can be done
by uncommenting the '#define POLOLUSWITCH' below.

Data stored on the NFC tags:
============================

On MIFARE Classic (Mini, 1K & 4K) tags:
---------------------------------------

Up to 16 bytes of data are stored in sector 1 / block 4, of which the first 9 bytes
are currently in use:
```
13 37 B3 47 01 02 04 10 19 00 00 00 00 00 00 00
----------- -- -- -- -- --
          |  |  |  |  |  |
          |  |  |  |  |  +- end track (0x01-0xFF)
          |  |  |  |  +- single/start track (0x01-0xFF)
          |  |  |  +- playback mode (0x01-0x08)
          |  |  +- folder (0x01-0x63)
          |  +- version (currently always 0x01)
```
end track: in vstory (0x06), valbum (0x07) and vparty (0x08) modes)
start track:  in single (0x04), vstory (0x06), valbum (0x07) and vparty (0x08) modes
modes: 

On MIFARE Ultralight / Ultralight C and NTAG213/215/216 tags:
-------------------------------------------------------------

Up to 16 bytes of data are stored in pages 8-11, of which the first 9 bytes
are currently in use:

page | data | comment
-- | -- | --
8 |  13 37 B3 47 | magic cookie to recognize that a card belongs to TonUINO
9  | 01 02 04 10 | version, folder, playback mode, single track / start strack
10  | 19 00 00 00 | end track, unused
11  | 00 00 00 00 | unused


Additional libraries:
========================================================

* [MFRC522.h](https://github.com/miguelbalboa/rfid)
* [DFMiniMp3.h](https://github.com/Makuna/DFMiniMp3)
* [AceButton.h](https://github.com/bxparks/AceButton)
* [IRremote.h](https://github.com/z3t0/Arduino-IRremote)
* [WS2812.h](https://github.com/cpldcpu/light_ws2812)
* [Vcc.h](https://github.com/Yveaux/Arduino_Vcc)