# Hidden Digital Clock SMart Shelving
A DIY, 3d Printed, Arduino powered, time-telling, shelving project.

==========

List of items used in this project and where to find them (the links are Amazon affiliate links and help to support this channel at no cost to you):

■ An Arduino Nano(x1): https://geni.us/ArduinoNanoV3

■ A 5m roll of WS2812B LEDS which has 60 LED’s a meter: https://geni.us/5mWS2812B60m

■ A Photosensitive resistor module: https://geni.us/PhotoresistorModule

■ A DS3231: https://geni.us/DS3231-RTC

■ Some Hook up wire - ideally three different colours: https://geni.us/22AWGWire 

■ A couple of 470 Ohm resistors: https://geni.us/Ufa2s 

■ A couple of electric terminal block strips - each 5 blocks long: https://geni.us/TerminalBlocks

■ No 8 wood screws: https://geni.us/No8Screw

■ A 5v power supply with screw terminals: https://geni.us/PowerSupplyTerm

■ Some thicker twin core cable


■ Battery powered glue gun: http://geni.us/BoschBatteryGlueGun

■ Battery powered soldering iron:  http://bit.ly/SealeySDL6 

==========

3D printed parts and wiring diagram can be downloaded from here: https://www.thingiverse.com/thing:4207524

==========

SAY THANKS:

Buy me a coffee to say thanks: https://ko-fi.com/diymachines
Support us on Patreon: https://www.patreon.com/diymachines

SUBSCRIBE: 
■ https://www.youtube.com/channel/UC3jc4X-kEq-dEDYhQ8QoYnQ?sub_confirmation=1

INSTAGRAM: https://www.instagram.com/diy_machines/?hl=en
FACEBOOK: https://www.facebook.com/diymachines/

==========

## ESP8266 WiFi sürümü (Shelf_Edge_Clock_ESP8266)

Arduino Nano + DS3231 yerine ESP8266 (NodeMCU / Wemos D1 mini) ile çalışan sürüm.
Saati internetten (NTP) alır; RTC modülüne gerek yoktur.

- Ayar sayfası: `http://<saat-ismi>.local` (varsayılan `http://clockcontrol.local`)
  - Saat ve alt ışık için otomatik (ışık sensörü) veya elle parlaklık, yumuşak parlaklık geçişi
  - Saat ve dakika renkleri
  - Dakika ve saat başı animasyonları, "Dene" düğmeleri, animasyon hızı, gece sessiz mod
  - WiFi ve saat ismi ayarları (`/wifi`)
- WiFi bulunamazsa saat isminde bir kurulum ağı açar (alt ışıklar turuncu yanıp söner)
- WiFi koparsa saat çalışmaya devam eder, bağlantı kendiliğinden geri gelir
- WiFi üzerinden kod yükleme (OTA)

Bağlantılar: saat şeridi D2, alt ışık D3, ışık sensörü A0. (D3 açılışta özel bir pindir; alt ışık D1'e taşınırsa açılış daha güvenilir olur.)

Kütüphaneler: Adafruit NeoPixel, WiFiManager (tzapu). Kart paketi: esp8266.

Derlemeden önce `secrets.example.h` dosyasını `secrets.h` adıyla kopyalayıp OTA şifresini yazın. `secrets.h` GitHub'a yüklenmez.
