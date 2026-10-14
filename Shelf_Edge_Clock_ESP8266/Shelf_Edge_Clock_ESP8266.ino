/*
 * Akıllı Raf Saati - ESP8266 (NodeMCU / Wemos D1 mini) sürümü
 *
 * Saati internetten (NTP) alır. Ayarlar telefondan http://clockcontrol.local adresinden yapılır:
 *   - Saat ve alt ışık için ayrı parlaklık (otomatik: ışık sensörüne göre, ya da elle)
 *   - Saat ve dakika renkleri
 *   - WiFi ağı ve saat ismi (/wifi sayfası)
 *
 * Gerekli kütüphaneler (Arduino IDE > Kütüphane Yöneticisi):
 *   - Adafruit NeoPixel
 *   - WiFiManager (tzapu, 2.x)
 * Kart: "NodeMCU 1.0" veya "LOLIN(WEMOS) D1 mini" (esp8266 kart paketi)
 *
 * WiFi davranışı:
 *   - Bağlanmaya çalışırken alt ışıklar MAVİ yanıp söner.
 *   - Kayıtlı ağa bağlanamazsa saat isminde (varsayılan "clockcontrol") bir kurulum ağı açar,
 *     bu sırada alt ışıklar TURUNCU yanıp söner. Telefonla bu ağa bağlanıp yeni WiFi seçilir.
 *   - Saat bir kez alındıktan sonra WiFi kopsa bile saat çalışmaya devam eder,
 *     WiFi geri gelince kendiliğinden yeniden bağlanır.
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <time.h>
#include "secrets.h"   // OTA_PASSWORD burada; GitHub'a yüklenmez (örnek: secrets.example.h)

// ================== AYARLAR ==================

// Kablolarınız kartın hangi pinine bağlıysa onu yazın (D1, D2, D5...).
// Sadece rakam (ör. 6) YAZMAYIN: ESP8266'da GPIO6 flash belleğe ayrılmıştır, kart çöker.
// Not: D3 açılışta özel bir pindir; alt ışık kablosu D1'e taşınırsa açılış daha güvenilir olur.
#define LEDCLOCK_PIN        D2
#define LEDDOWNLIGHT_PIN    D3
#define LIGHT_SENSOR_PIN    A0

#define LEDCLOCK_COUNT      216
#define LEDDOWNLIGHT_COUNT  12

// Kırmızı ile yeşil ters görünüyorsa NEO_GRB yerine NEO_RGB yazın
#define LED_TYPE            (NEO_GRB + NEO_KHZ800)

const char* DEFAULT_CLOCK_NAME = "clockcontrol";    // http://clockcontrol.local
const char* TIMEZONE           = "<+03>-3";         // Türkiye: UTC+3, yaz saati uygulaması yok

const bool SHOW_MIDNIGHT_AS_12 = true;              // false: gece yarısı 0:xx görünür

// Işık sensörü: düşük okuma = aydınlık oda = parlak saat
const int LIGHT_READING_BRIGHT    = 50;
const int LIGHT_READING_DARK      = 1000;
// Parlaklık değerleri göze göre (0-255) verilir, LED'lere gamma eğrisiyle çevrilir
const int BRIGHTNESS_MIN          = 40;
const int CLOCK_AUTO_MAX          = 230;
const int DOWNLIGHT_AUTO_MAX      = 170;
const int STATUS_BRIGHTNESS       = 50;   // durum ışıkları (doğrudan LED değeri)

// Parlaklık geçişi: değişirken saniyede 20 adım, her adımda farkın %4'ü kadar (~4 sn'lik yumuşak geçiş)
const unsigned long FADE_INTERVAL_MS = 50;
const float FADE_SPEED            = 0.04;
const float GAMMA                 = 2.2;

const uint32_t DEFAULT_HOUR_COLOUR   = 0x00FF00;
const uint32_t DEFAULT_MINUTE_COLOUR = 0xFF0000;

const unsigned long WIFI_CONNECT_TIMEOUT_S  = 20;   // kayıtlı ağa bağlanma süresi
const unsigned long CONFIG_PORTAL_TIMEOUT_S = 300;  // kurulum ağının açık kalma süresi
const unsigned long NO_TIME_RESTART_MS      = 10UL * 60 * 1000;
const unsigned long STATUS_PRINT_MS         = 60UL * 1000;

// Animasyonlar (animations.ino)
const unsigned long ANIMATION_FRAME_MS      = 30;    // animasyon sırasında saniyede ~33 kare
const unsigned long MINUTE_ANIMATION_MS     = 900;   // "Normal" hızda süreler
const unsigned long HOUR_ANIMATION_MS       = 3000;
const float ANIMATION_SPEED_FACTORS[]       = { 1.6, 1.0, 0.6 };  // Yavaş, Normal, Hızlı

// =============================================

Adafruit_NeoPixel stripClock(LEDCLOCK_COUNT, LEDCLOCK_PIN, LED_TYPE);
Adafruit_NeoPixel stripDownlighter(LEDDOWNLIGHT_COUNT, LEDDOWNLIGHT_PIN, LED_TYPE);

WiFiManager wm;
ESP8266WebServer server(80);
bool networkServicesStarted = false;

// EEPROM'da saklanan ayarlar
const uint8_t SETTINGS_MAGIC = 0xC2;
struct Settings {
  uint8_t magic;
  uint32_t hourColour;
  uint32_t minuteColour;
  bool autoClockBrightness;
  bool autoDownlightBrightness;
  uint8_t clockBrightness;
  uint8_t downlightBrightness;
  char clockName[32];
  // Animasyon ayarları (sonradan eklendi; eski kayıtlarda yoksa varsayılanlar kullanılır)
  uint8_t animMagic;
  uint8_t minuteAnimation;
  uint8_t hourAnimation;
  uint8_t animationSpeed;    // 0 yavaş, 1 normal, 2 hızlı
  bool quietEnabled;
  uint16_t quietStart;       // gün içindeki dakika (23:00 = 1380)
  uint16_t quietEnd;
};
Settings settings;
const uint8_t ANIM_MAGIC = 0xA1;

enum MinuteAnimation { MIN_OFF, MIN_CROSSFADE, MIN_DRAW, MIN_WIPE, MIN_DISSOLVE, MIN_FLASH, MIN_COLOURWAVE, MINUTE_ANIMATION_COUNT };
enum HourAnimation { HOUR_OFF, HOUR_SNAKE, HOUR_RAINBOW, HOUR_SLOT, HOUR_FIREWORKS, HOUR_FILL, HOUR_BREATHE, HOUR_RANDOM, HOUR_ANIMATION_COUNT };

// Saat yüzü: her LED'in olması gereken rengi. Animasyonlar eski yüzden yenisine geçer.
const int FACE_PIXELS = 207;               // rakamlara ait LED sayısı (kalanlar boşta)
uint32_t oldFace[LEDCLOCK_COUNT];
uint32_t newFace[LEDCLOCK_COUNT];
uint32_t frameFace[LEDCLOCK_COUNT];
uint32_t* faceBuffer = newFace;            // digits.ino'daki fonksiyonların yazdığı yer
int displayedHour = -1;
int displayedMinute = -1;

bool animationActive = false;
bool animationHourly = false;
uint8_t animationType = 0;
unsigned long animationStart = 0;
unsigned long animationDuration = 0;
uint32_t animationSeed = 0;
bool digitChanged[4];

// Işık sensörü yumuşatma
const int numReadings = 10;
int readings[numReadings];
int readIndex = 0;
bool readingsInitialised = false;
int lightSensorAverage = 0;

// Yumuşak geçiş için o anki parlaklıklar (-1: henüz ayarlanmadı)
float clockLevel = -1;
float downlightLevel = -1;
uint8_t shownClockPwm = 0;
uint8_t shownDownlightPwm = 0;

unsigned long lastDisplayUpdate = 0;
unsigned long lastFadeStep = 0;
unsigned long lastAnimationFrame = 0;
unsigned long lastStatusPrint = 0;
bool blinkOn = false;


void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Akilli Raf Saati basliyor");

  randomSeed(ESP.random());
  loadSettings();

  stripClock.begin();
  stripClock.show();
  stripDownlighter.begin();
  stripDownlighter.setBrightness(STATUS_BRIGHTNESS);
  stripDownlighter.fill(stripDownlighter.Color(0, 0, 255)); // mavi: bağlanıyor
  stripDownlighter.show();

  // Saat sunucusu: ESP kendi saatini tutar, internetten düzenli olarak düzeltir
  configTime(TIMEZONE, "pool.ntp.org", "time.google.com");

  WiFi.mode(WIFI_STA);
  WiFi.hostname(settings.clockName);
  WiFi.setSleepMode(WIFI_NONE_SLEEP); // WiFi uyku moduna girip kopmasın
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  wm.setConnectTimeout(WIFI_CONNECT_TIMEOUT_S);
  wm.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT_S);
  wm.setConfigPortalBlocking(false); // kurulum ağı açıkken de program çalışmaya devam etsin
  wm.setAPCallback(configModeCallback);

  if (wm.autoConnect(settings.clockName)) {
    Serial.print("WiFi baglandi, IP: ");
    Serial.println(WiFi.localIP());
  }
}

void loop() {
  wm.process();

  if (WiFi.status() == WL_CONNECTED && !wm.getConfigPortalActive() && !networkServicesStarted) {
    startNetworkServices();
  }

  if (networkServicesStarted) {
    server.handleClient();
    MDNS.update();
    ArduinoOTA.handle();
  }

  // LED'leri saniyede bir güncelle. Sık güncelleme WiFi'yi bozar:
  // her show() çağrısı LED'lere veri gönderirken birkaç milisaniye kesmeleri kapatır.
  unsigned long now = millis();
  bool redraw = false;
  if (now - lastDisplayUpdate >= 1000) {
    lastDisplayUpdate = now;
    blinkOn = !blinkOn;
    readLightSensor();
    checkTimeChange();
    redraw = true;
  }

  // Animasyon oynarken kısa süreliğine sık güncelle
  if (animationActive && now - lastAnimationFrame >= ANIMATION_FRAME_MS) {
    lastAnimationFrame = now;
    redraw = true;
  }

  // Parlaklık değişirken daha sık güncelle, değişim bitince dur
  if (now - lastFadeStep >= FADE_INTERVAL_MS) {
    lastFadeStep = now;
    if (stepBrightness()) redraw = true;
  }

  if (redraw) updateDisplay();

  if (now - lastStatusPrint >= STATUS_PRINT_MS) {
    lastStatusPrint = now;
    printStatus();
  }

  // Saat hiç alınamadıysa ve kurulum ağı da kapandıysa baştan dene
  if (!timeIsValid() && !wm.getConfigPortalActive() && WiFi.status() != WL_CONNECTED && now > NO_TIME_RESTART_MS) {
    Serial.println("Saat alinamadi, yeniden baslatiliyor");
    ESP.restart();
  }
}


// ---------- WiFi ve ağ servisleri ----------

void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.print("Kurulum agi acildi: ");
  Serial.print(myWiFiManager->getConfigPortalSSID());
  Serial.print("  Adres: ");
  Serial.println(WiFi.softAPIP());
}

void startNetworkServices() {
  Serial.print("WiFi bagli, IP: ");
  Serial.print(WiFi.localIP());
  Serial.print("  Sinyal: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  ArduinoOTA.setHostname(settings.clockName); // <isim>.local adını da (mDNS) başlatır
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/getSettings", HTTP_GET, handleGetSettings);
  server.on("/update", HTTP_POST, handleUpdate);
  server.on("/preview", HTTP_POST, handlePreview);
  server.on("/wifi", HTTP_GET, handleWifiPage);
  server.on("/wifi", HTTP_POST, handleWifiSave);
  server.on("/wifi-reset", HTTP_POST, handleWifiReset);
  server.begin();

  networkServicesStarted = true;
  Serial.print("Ayar sayfasi: http://");
  Serial.print(settings.clockName);
  Serial.print(".local  veya  http://");
  Serial.println(WiFi.localIP());
}


// ---------- Web sayfaları ----------

const char MAIN_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="tr"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Saat Ayarları</title>
<style>
body{font-family:Arial;margin:20px;background:#f0f0f0}
.container{max-width:480px;margin:0 auto}
.box{margin:16px 0;padding:15px;background:#fff;border-radius:8px}
h3{margin-top:0}
label{display:inline-block;width:190px}
input[type=range]{width:180px;vertical-align:middle}
input[type=color]{width:70px;height:40px;vertical-align:middle}
.value{font-weight:bold;color:#2196F3}
.info{color:#555;font-size:14px;line-height:1.6}
button{padding:10px 20px;background:#4CAF50;color:#fff;border:none;border-radius:4px;cursor:pointer}
button.small{padding:6px 12px;background:#2196F3;margin-left:6px}
select{padding:6px;font-size:14px}
.note{color:#555;font-size:14px}
a{color:#2196F3}
</style></head><body><div class="container">
<h2><span id="clockTitle">Saat</span> Ayarları</h2>
<div class="box"><h3>Saat Parlaklık Kontrolü</h3>
<p><label>Saat Otomatik Parlaklık:</label><input type="checkbox" id="autoBar1"></p>
<p><label>Saat Parlaklığı:</label><input type="range" id="bar1" min="0" max="255"> <span class="value" id="bar1Value"></span></p>
</div>
<div class="box"><h3>Alt Işık Parlaklık Kontrolü</h3>
<p><label>Alt Işık Otomatik Parlaklık:</label><input type="checkbox" id="autoBar2"></p>
<p><label>Alt Işık Parlaklığı:</label><input type="range" id="bar2" min="0" max="255"> <span class="value" id="bar2Value"></span></p>
</div>
<div class="box"><h3>Renk Ayarları</h3>
<p><label>Dakika Rengi:</label><input type="color" id="minuteColor"></p>
<p><label>Saat Rengi:</label><input type="color" id="hourColor"></p>
</div>
<div class="box"><h3>Animasyonlar</h3>
<p><label>Dakika geçişi:</label><select id="minuteAnim">
<option value="0">Kapalı</option><option value="1">Yumuşak geçiş</option><option value="2">Segment çizimi</option>
<option value="3">Perde</option><option value="4">Işıltı</option><option value="5">Kısa parlama</option>
<option value="6">Renk dalgası</option></select>
<button class="small" onclick="preview('minute')">Dene</button></p>
<p><label>Saat başı:</label><select id="hourAnim">
<option value="0">Kapalı</option><option value="1">Işık yılanı</option><option value="2">Gökkuşağı dalgası</option>
<option value="3">Slot makinesi</option><option value="4">Havai fişek</option><option value="5">Doldur ve göster</option>
<option value="6">Alt ışık nefesi</option><option value="7">Rastgele</option></select>
<button class="small" onclick="preview('hour')">Dene</button></p>
<p><label>Animasyon hızı:</label>
<input type="radio" name="speed" value="0" id="speed0">Yavaş
<input type="radio" name="speed" value="1" id="speed1">Normal
<input type="radio" name="speed" value="2" id="speed2">Hızlı</p>
<p><label>Gece sessiz mod:</label><input type="checkbox" id="quiet"></p>
<p class="note">Bu saatler arasında animasyon oynamaz:
<input type="time" id="quietStart"> – <input type="time" id="quietEnd"></p>
</div>
<button onclick="updateSettings()">Ayarları Güncelle</button>
<div class="box info" id="info"></div>
<p><a href="/wifi">WiFi ve saat ismi ayarları</a></p>
</div>
<script>
function $(id){return document.getElementById(id)}
fetch('/getSettings').then(function(r){return r.json()}).then(function(d){
  $('clockTitle').textContent=d.clockName;
  $('autoBar1').checked=d.autoClockBrightness;
  $('autoBar2').checked=d.autoDownlightBrightness;
  $('bar1').value=$('bar1Value').textContent=d.clockBrightness;
  $('bar2').value=$('bar2Value').textContent=d.downlightBrightness;
  $('bar1').disabled=d.autoClockBrightness;
  $('bar2').disabled=d.autoDownlightBrightness;
  $('minuteColor').value='#'+d.minuteColor;
  $('hourColor').value='#'+d.hourColor;
  $('minuteAnim').value=d.minuteAnimation;
  $('hourAnim').value=d.hourAnimation;
  $('speed'+d.animationSpeed).checked=true;
  $('quiet').checked=d.quietEnabled;
  $('quietStart').value=d.quietStart;
  $('quietEnd').value=d.quietEnd;
  $('info').innerHTML='Saat: '+d.time+'<br>WiFi: '+d.ssid+' ('+d.rssi+' dBm)<br>IP: '+d.ip+'<br>Işık sensörü: '+d.lightSensor;
});
$('bar1').oninput=function(){$('bar1Value').textContent=this.value};
$('bar2').oninput=function(){$('bar2Value').textContent=this.value};
$('autoBar1').onchange=function(){$('bar1').disabled=this.checked};
$('autoBar2').onchange=function(){$('bar2').disabled=this.checked};
function updateSettings(){
  fetch('/update',{method:'POST',body:new URLSearchParams({
    autoBar1:$('autoBar1').checked?1:0, autoBar2:$('autoBar2').checked?1:0,
    bar1:$('bar1').value, bar2:$('bar2').value,
    minuteColor:$('minuteColor').value.substring(1), hourColor:$('hourColor').value.substring(1),
    minuteAnim:$('minuteAnim').value, hourAnim:$('hourAnim').value,
    animSpeed:document.querySelector('input[name=speed]:checked').value,
    quiet:$('quiet').checked?1:0, quietStart:$('quietStart').value, quietEnd:$('quietEnd').value
  })}).then(function(r){alert(r.ok?'Ayarlar güncellendi!':'Hata oluştu!')})
  .catch(function(e){alert('Hata oluştu: '+e)});
}
function preview(type){
  fetch('/preview',{method:'POST',body:new URLSearchParams({
    type:type, anim:$(type=='hour'?'hourAnim':'minuteAnim').value
  })}).then(function(r){if(!r.ok)r.text().then(alert)});
}
</script></body></html>)rawliteral";

const char WIFI_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="tr"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>WiFi Ayarları</title>
<style>
body{font-family:Arial;margin:20px;background:#f0f0f0}
.container{max-width:400px;margin:0 auto;padding:20px;background:#fff;border-radius:8px}
label{display:block;margin-bottom:5px}
input{width:100%;padding:8px;margin-bottom:12px;box-sizing:border-box}
button{width:100%;padding:10px;margin-top:8px;background:#4CAF50;color:#fff;border:none;border-radius:4px;cursor:pointer}
.danger{background:#c62828}
.note{color:#555;font-size:13px}
</style></head><body><div class="container">
<h2>WiFi Ayarları</h2>
<p class="note">Saat sadece 2.4 GHz WiFi ağlarına bağlanabilir (5G olan ağlara bağlanamaz).</p>
<label>WiFi adı (SSID):</label><input type="text" id="ssid">
<label>Şifre:</label><input type="password" id="password">
<label>Saat ismi:</label><input type="text" id="clockName">
<p class="note">Saat ismi adres olarak da kullanılır: http://&lt;isim&gt;.local
(sadece küçük harf, rakam ve tire). WiFi adını boş bırakırsanız sadece isim değişir.</p>
<button onclick="save()">Kaydet ve yeniden başlat</button>
<form method="POST" action="/wifi-reset" onsubmit="return confirm('Kayıtlı WiFi silinsin mi? Saat kurulum ağını açacak.')">
<button class="danger">WiFi ayarlarını sıfırla</button></form>
<p><a href="/">← Ana sayfa</a></p>
</div><script>
function $(id){return document.getElementById(id)}
fetch('/getSettings').then(function(r){return r.json()}).then(function(d){$('clockName').value=d.clockName});
function save(){
  fetch('/wifi',{method:'POST',body:new URLSearchParams({
    ssid:$('ssid').value, password:$('password').value, clockName:$('clockName').value
  })}).then(function(r){return r.text()}).then(function(t){alert(t)})
  .catch(function(e){alert('Hata oluştu: '+e)});
}
</script></body></html>)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", MAIN_PAGE);
}

void handleWifiPage() {
  server.send_P(200, "text/html; charset=utf-8", WIFI_PAGE);
}

String colourToHex(uint32_t colour) {
  char buffer[7];
  snprintf(buffer, sizeof(buffer), "%06X", (unsigned int)(colour & 0xFFFFFF));
  return String(buffer);
}

String jsonEscape(const String& text) {
  String result;
  for (unsigned int i = 0; i < text.length(); i++) {
    char c = text[i];
    if (c == '"' || c == '\\') result += '\\';
    result += c;
  }
  return result;
}

void handleGetSettings() {
  String json = "{";
  json += "\"clockName\":\"" + String(settings.clockName) + "\",";
  json += "\"autoClockBrightness\":" + String(settings.autoClockBrightness ? "true" : "false") + ",";
  json += "\"autoDownlightBrightness\":" + String(settings.autoDownlightBrightness ? "true" : "false") + ",";
  json += "\"clockBrightness\":" + String(settings.clockBrightness) + ",";
  json += "\"downlightBrightness\":" + String(settings.downlightBrightness) + ",";
  json += "\"minuteColor\":\"" + colourToHex(settings.minuteColour) + "\",";
  json += "\"hourColor\":\"" + colourToHex(settings.hourColour) + "\",";
  json += "\"minuteAnimation\":" + String(settings.minuteAnimation) + ",";
  json += "\"hourAnimation\":" + String(settings.hourAnimation) + ",";
  json += "\"animationSpeed\":" + String(settings.animationSpeed) + ",";
  json += "\"quietEnabled\":" + String(settings.quietEnabled ? "true" : "false") + ",";
  json += "\"quietStart\":\"" + minutesToText(settings.quietStart) + "\",";
  json += "\"quietEnd\":\"" + minutesToText(settings.quietEnd) + "\",";
  json += "\"time\":\"" + currentTimeText() + "\",";
  json += "\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"lightSensor\":" + String(lightSensorAverage);
  json += "}";
  server.send(200, "application/json", json);
}

bool parseHexColour(String text, uint32_t& colour) {
  if (text.startsWith("#")) text = text.substring(1);
  if (text.length() != 6) return false;
  char* end;
  colour = strtoul(text.c_str(), &end, 16);
  return *end == '\0';
}

void handleUpdate() {
  uint32_t hourColour, minuteColour;
  if (!parseHexColour(server.arg("hourColor"), hourColour) || !parseHexColour(server.arg("minuteColor"), minuteColour)) {
    server.send(400, "text/plain", "Gecersiz renk");
    return;
  }
  settings.hourColour = hourColour;
  settings.minuteColour = minuteColour;
  settings.autoClockBrightness = server.arg("autoBar1") == "1";
  settings.autoDownlightBrightness = server.arg("autoBar2") == "1";
  settings.clockBrightness = constrain(server.arg("bar1").toInt(), 0, 255);
  settings.downlightBrightness = constrain(server.arg("bar2").toInt(), 0, 255);

  if (server.hasArg("minuteAnim")) {
    settings.minuteAnimation = constrain(server.arg("minuteAnim").toInt(), 0, MINUTE_ANIMATION_COUNT - 1);
    settings.hourAnimation = constrain(server.arg("hourAnim").toInt(), 0, HOUR_ANIMATION_COUNT - 1);
    settings.animationSpeed = constrain(server.arg("animSpeed").toInt(), 0, 2);
    settings.quietEnabled = server.arg("quiet") == "1";
    int quietStart = textToMinutes(server.arg("quietStart"));
    int quietEnd = textToMinutes(server.arg("quietEnd"));
    if (quietStart >= 0) settings.quietStart = quietStart;
    if (quietEnd >= 0) settings.quietEnd = quietEnd;
  }

  saveSettings();
  if (displayedMinute >= 0) renderFace(newFace, displayedHour, displayedMinute); // yeni renkler
  updateDisplay();
  server.send(200, "text/plain", "Ayarlar guncellendi");
}

// Ayar sayfasındaki "Dene" düğmesi: bir önceki dakikadan/saatten şimdiye geçişi oynatır
void handlePreview() {
  if (displayedMinute < 0) {
    server.send(409, "text/plain; charset=utf-8", "Saat henüz internetten alınmadı.");
    return;
  }
  bool hourly = server.arg("type") == "hour";
  int type = server.arg("anim").toInt();
  if (type <= 0 || type >= (hourly ? (int)HOUR_ANIMATION_COUNT : (int)MINUTE_ANIMATION_COUNT)) {
    server.send(400, "text/plain; charset=utf-8", "Önce bir animasyon seçin (Kapalı denenemez).");
    return;
  }

  int previousHour = displayedHour;
  int previousMinute = displayedMinute - 1;
  if (hourly || previousMinute < 0) {
    previousHour = (displayedHour + 23) % 24;
    previousMinute = 59;
  }
  renderFace(oldFace, previousHour, previousMinute);
  startAnimation(hourly, type);
  server.send(200, "text/plain", "OK");
}

String minutesToText(uint16_t minutes) {
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes / 60, minutes % 60);
  return String(buffer);
}

// "23:00" -> 1380, hatalıysa -1
int textToMinutes(const String& text) {
  int colon = text.indexOf(':');
  if (colon < 1) return -1;
  int hours = text.substring(0, colon).toInt();
  int minutes = text.substring(colon + 1).toInt();
  if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) return -1;
  return hours * 60 + minutes;
}

// Saat ismi adres (http://<isim>.local) olarak kullanıldığı için sadece küçük harf, rakam ve tire
String sanitizeClockName(const String& name) {
  String result;
  for (unsigned int i = 0; i < name.length() && result.length() < sizeof(settings.clockName) - 1; i++) {
    char c = tolower(name[i]);
    if (isalnum(c) || c == '-') result += c;
  }
  return result;
}

void handleWifiSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  String clockName = sanitizeClockName(server.arg("clockName"));

  if (clockName.length() > 0) {
    strncpy(settings.clockName, clockName.c_str(), sizeof(settings.clockName) - 1);
    settings.clockName[sizeof(settings.clockName) - 1] = '\0';
    saveSettings();
  }

  String message = "Ayarlar kaydedildi. Saat yeniden basliyor, yeni adres: http://";
  message += settings.clockName;
  message += ".local";
  if (ssid.length() > 0) {
    message += "\nBaglanamazsa '";
    message += settings.clockName;
    message += "' kurulum agini acacak.";
  }
  server.send(200, "text/plain; charset=utf-8", message);
  delay(500);

  if (ssid.length() > 0) {
    WiFi.persistent(true);
    WiFi.begin(ssid.c_str(), password.c_str()); // yeni ağ kalıcı olarak kaydedilir
    delay(500);
  }
  ESP.restart();
}

void handleWifiReset() {
  server.send(200, "text/html; charset=utf-8",
              String("<meta charset='utf-8'><p>WiFi ayarları silindi. Telefonunuzdan <b>") +
              settings.clockName + "</b> ağına bağlanıp yeni ağı seçin.</p>");
  delay(500);
  wm.resetSettings();
  ESP.restart();
}


// ---------- Ayarlar (EEPROM) ----------

void loadSettings() {
  EEPROM.begin(128);
  EEPROM.get(0, settings);
  settings.clockName[sizeof(settings.clockName) - 1] = '\0';
  if (settings.magic != SETTINGS_MAGIC || sanitizeClockName(settings.clockName).length() == 0) {
    settings.magic = SETTINGS_MAGIC;
    settings.hourColour = DEFAULT_HOUR_COLOUR;
    settings.minuteColour = DEFAULT_MINUTE_COLOUR;
    settings.autoClockBrightness = true;
    settings.autoDownlightBrightness = false;
    settings.clockBrightness = 128;
    settings.downlightBrightness = 50;
    strncpy(settings.clockName, DEFAULT_CLOCK_NAME, sizeof(settings.clockName) - 1);
    settings.clockName[sizeof(settings.clockName) - 1] = '\0';
    settings.animMagic = 0;
  }

  // Animasyon ayarları eski kayıtta yoksa varsayılanlar; diğer ayarlar korunur
  if (settings.animMagic != ANIM_MAGIC || settings.minuteAnimation >= MINUTE_ANIMATION_COUNT ||
      settings.hourAnimation >= HOUR_ANIMATION_COUNT || settings.animationSpeed > 2 ||
      settings.quietStart >= 1440 || settings.quietEnd >= 1440) {
    settings.animMagic = ANIM_MAGIC;
    settings.minuteAnimation = MIN_CROSSFADE;
    settings.hourAnimation = HOUR_SNAKE;
    settings.animationSpeed = 1;
    settings.quietEnabled = true;
    settings.quietStart = 23 * 60;
    settings.quietEnd = 7 * 60;
  }
}

void saveSettings() {
  EEPROM.put(0, settings);
  EEPROM.commit();
}


// ---------- Saat, parlaklık ve LED'ler ----------

bool timeIsValid() {
  return time(nullptr) > 1700000000; // 2023'ten sonraki bir tarih: saat internetten alınmış
}

String currentTimeText() {
  if (!timeIsValid()) return "henuz alinamadi";
  time_t now = time(nullptr);
  struct tm timeInfo;
  localtime_r(&now, &timeInfo);
  char buffer[20];
  strftime(buffer, sizeof(buffer), "%d.%m.%Y %H:%M:%S", &timeInfo);
  return String(buffer);
}

void readLightSensor() {
  int value = analogRead(LIGHT_SENSOR_PIN);

  // İlk açılışta dizi sıfırla değil ilk okumayla dolsun, saat parlak başlamasın
  if (!readingsInitialised) {
    for (int i = 0; i < numReadings; i++) readings[i] = value;
    readingsInitialised = true;
  }
  readings[readIndex] = value;
  readIndex = (readIndex + 1) % numReadings;

  long sum = 0;
  for (int i = 0; i < numReadings; i++) sum += readings[i];
  lightSensorAverage = sum / numReadings;
}

int autoBrightness(int maxBrightness) {
  int brightness = map(lightSensorAverage, LIGHT_READING_BRIGHT, LIGHT_READING_DARK, maxBrightness, BRIGHTNESS_MIN);
  return constrain(brightness, BRIGHTNESS_MIN, maxBrightness);
}

int clockTargetLevel() {
  return settings.autoClockBrightness ? autoBrightness(CLOCK_AUTO_MAX) : settings.clockBrightness;
}

int downlightTargetLevel() {
  return settings.autoDownlightBrightness ? autoBrightness(DOWNLIGHT_AUTO_MAX) : settings.downlightBrightness;
}

// Göz düşük parlaklıktaki değişimi çok daha güçlü algılar; gamma eğrisi bunu dengeler
uint8_t levelToPwm(float level) {
  if (level <= 0) return 0;
  int pwm = round(255.0 * pow(level / 255.0, GAMMA));
  return constrain(pwm, 1, 255);
}

void stepTowards(float& level, int target) {
  if (level < 0) {          // ilk açılış: doğrudan hedef parlaklıkla başla
    level = target;
    return;
  }
  float step = (target - level) * FADE_SPEED;
  if (fabs(step) < 0.1) step = (target > level) ? 0.1 : -0.1;
  if (fabs(target - level) <= fabs(step)) level = target;
  else level += step;
}

// LED'lere giden değer değiştiyse true döner
bool stepBrightness() {
  stepTowards(clockLevel, clockTargetLevel());
  stepTowards(downlightLevel, downlightTargetLevel());
  return levelToPwm(clockLevel) != shownClockPwm || levelToPwm(downlightLevel) != shownDownlightPwm;
}

void updateDisplay() {
  if (clockLevel < 0) stepBrightness();
  shownClockPwm = levelToPwm(clockLevel);
  stripClock.setBrightness(shownClockPwm);  // renkler yazılmadan önce ayarlanmalı

  if (timeIsValid()) {
    if (displayedMinute < 0) checkTimeChange();

    float downlightFactor = 1.0;
    if (animationActive) downlightFactor = drawAnimationFrame();
    else drawFace(newFace);

    shownDownlightPwm = levelToPwm(downlightLevel);
    stripDownlighter.setBrightness(round(shownDownlightPwm * downlightFactor));
    stripDownlighter.fill(stripDownlighter.Color(255, 255, 255));
  } else {
    stripClock.clear();
    // Saat henüz yok: alt ışıklarla durumu göster
    uint32_t statusColour = wm.getConfigPortalActive()
                              ? stripDownlighter.Color(255, 120, 0)  // turuncu: kurulum ağı açık
                              : stripDownlighter.Color(0, 0, 255);   // mavi: bağlanıyor
    stripDownlighter.setBrightness(STATUS_BRIGHTNESS);
    stripDownlighter.fill(blinkOn ? statusColour : 0);
  }

  stripClock.show();
  stripDownlighter.show();
}

void displayTheTime(int hour24, int minute) {
  displayNumber(minute % 10, 0, settings.minuteColour);
  displayNumber(minute / 10, 63, settings.minuteColour);

  int hour = hour24;
  if (hour > 12) hour -= 12;
  if (hour == 0 && SHOW_MIDNIGHT_AS_12) hour = 12;

  displayNumber(hour % 10, 126, settings.hourColour);
  if (hour > 9) {
    faceFill(settings.hourColour, 189, 18); // saatin onlar basamağı: "1"
  }
}

// digits.ino'daki rakam fonksiyonları LED'lere değil, faceBuffer'a çizer
void faceFill(uint32_t colour, int first, int count) {
  for (int i = first; i < first + count && i < LEDCLOCK_COUNT; i++) faceBuffer[i] = colour;
}

void renderFace(uint32_t* buffer, int hour24, int minute) {
  faceBuffer = buffer;
  for (int i = 0; i < LEDCLOCK_COUNT; i++) buffer[i] = 0;
  displayTheTime(hour24, minute);
  faceBuffer = newFace;
}

void drawFace(const uint32_t* buffer) {
  for (int i = 0; i < LEDCLOCK_COUNT; i++) stripClock.setPixelColor(i, buffer[i]);
}

// Saniyede bir çağrılır: dakika değiştiyse yeni yüzü hazırlar ve animasyonu başlatır
void checkTimeChange() {
  if (!timeIsValid()) return;
  time_t now = time(nullptr);
  struct tm timeInfo;
  localtime_r(&now, &timeInfo);
  if (timeInfo.tm_hour == displayedHour && timeInfo.tm_min == displayedMinute) return;

  bool firstDraw = displayedMinute < 0;
  bool hourChanged = timeInfo.tm_hour != displayedHour;
  displayedHour = timeInfo.tm_hour;
  displayedMinute = timeInfo.tm_min;

  memcpy(oldFace, newFace, sizeof(newFace));
  renderFace(newFace, displayedHour, displayedMinute);

  if (firstDraw || isQuietTime(displayedHour * 60 + displayedMinute)) {
    animationActive = false;
    return;
  }
  if (hourChanged) startAnimation(true, settings.hourAnimation);
  else startAnimation(false, settings.minuteAnimation);
}

bool isQuietTime(int minuteOfDay) {
  if (!settings.quietEnabled || settings.quietStart == settings.quietEnd) return false;
  if (settings.quietStart < settings.quietEnd) {
    return minuteOfDay >= settings.quietStart && minuteOfDay < settings.quietEnd;
  }
  return minuteOfDay >= settings.quietStart || minuteOfDay < settings.quietEnd; // gece yarısını aşan aralık
}

void displayNumber(int digitToDisplay, int offsetBy, uint32_t colourToUse) {
  switch (digitToDisplay) {
    case 0: digitZero(offsetBy, colourToUse); break;
    case 1: digitOne(offsetBy, colourToUse); break;
    case 2: digitTwo(offsetBy, colourToUse); break;
    case 3: digitThree(offsetBy, colourToUse); break;
    case 4: digitFour(offsetBy, colourToUse); break;
    case 5: digitFive(offsetBy, colourToUse); break;
    case 6: digitSix(offsetBy, colourToUse); break;
    case 7: digitSeven(offsetBy, colourToUse); break;
    case 8: digitEight(offsetBy, colourToUse); break;
    case 9: digitNine(offsetBy, colourToUse); break;
    default: break;
  }
}

void printStatus() {
  Serial.println("=== Sistem Durumu ===");
  Serial.print("Saat: ");                 Serial.println(currentTimeText());
  Serial.print("WiFi: ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(WiFi.SSID()); Serial.print(" ("); Serial.print(WiFi.RSSI()); Serial.println(" dBm)");
    Serial.print("IP Adresi: ");          Serial.println(WiFi.localIP());
  } else {
    Serial.println(wm.getConfigPortalActive() ? "kurulum agi acik" : "bagli degil");
  }
  Serial.print("Isik sensoru: ");         Serial.println(lightSensorAverage);
  Serial.print("Saat parlakligi: ");
  Serial.println(settings.autoClockBrightness ? "otomatik" : String(settings.clockBrightness));
  Serial.print("Alt isik parlakligi: ");
  Serial.println(settings.autoDownlightBrightness ? "otomatik" : String(settings.downlightBrightness));
  Serial.print("Saat rengi: #");          Serial.println(colourToHex(settings.hourColour));
  Serial.print("Dakika rengi: #");        Serial.println(colourToHex(settings.minuteColour));
  Serial.print("Animasyon (dakika/saat): ");
  Serial.print(settings.minuteAnimation); Serial.print(" / "); Serial.print(settings.hourAnimation);
  Serial.println(isQuietTime(displayedHour * 60 + displayedMinute) ? "  (gece sessiz mod)" : "");
  Serial.println("=====================");
}
