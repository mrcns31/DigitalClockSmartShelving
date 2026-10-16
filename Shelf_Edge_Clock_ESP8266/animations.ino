/*
 * Dakika ve saat başı animasyonları.
 *
 * Her animasyon eski saat yüzünden (oldFace) yenisine (newFace) geçişi çizer.
 * p: animasyonun ilerleyişi, 0 (başlangıç) ile 1 (bitiş) arası.
 * Alt ışık rengi downlightAnimationColour ile, parlaklığı dönüş değeriyle değiştirilebilir.
 *
 * Şerit düzeni: her rakam 63 LED (7 segment x 9 LED), sırasıyla
 * dakika birler (0), dakika onlar (63), saat birler (126), saatin onlar basamağı "1" (189, 18 LED).
 * Segment sırası: sağ-üst, üst, sol-üst, orta, sağ-alt, alt, sol-alt.
 */

const uint32_t WHITE = 0xFFFFFF;

void startAnimation(bool hourly, uint8_t type) {
  if (type == 0) {
    animationActive = false;
    return;
  }
  // Rastgele: "Kapalı" ve "Rastgele" dışındaki seçeneklerden biri
  if (hourly && type == HOUR_RANDOM) {
    type = random(1, HOUR_ANIMATION_COUNT - 1);
    if (type >= HOUR_RANDOM) type++;
  }
  if (!hourly && type == MIN_RANDOM) type = random(1, MIN_RANDOM);

  animationHourly = hourly;
  animationType = type;
  animationSeed = random(0x7FFFFFFF);

  float speed = ANIMATION_SPEED_FACTORS[settings.animationSpeed];
  unsigned long baseDuration = hourly ? HOUR_ANIMATION_MS : MINUTE_ANIMATION_MS;
  if (!hourly && type == MIN_FLASH_BREATHE) baseDuration = FLASH_BREATHE_MS;
  if (!hourly && (type == MIN_TYPEWRITER || type == MIN_COMET || type == MIN_HEARTBEAT || type == MIN_EMBER)) baseDuration = 1400;
  if (hourly && type == HOUR_CHIME) baseDuration = 800 + 450 * chimeCount();   // saat 12'de ~6 sn
  if (hourly && type == HOUR_COUNTDOWN) baseDuration = 4000;
  if (hourly && type == HOUR_MATRIX) baseDuration = 4000;
  animationPixelDuration = baseDuration * speed;

  // Alt ışık eşlik ediyorsa nefesin tamamlanması için süre gerekirse uzar
  animationDuration = animationPixelDuration;
  if (accompanyActive()) animationDuration = max(animationPixelDuration, (unsigned long)(FLASH_BREATHE_MS * speed));

  for (int d = 0; d < 4; d++) digitChanged[d] = false;
  for (int i = 0; i < FACE_PIXELS; i++) {
    if (oldFace[i] != newFace[i]) digitChanged[digitOfPixel(i)] = true;
  }
  prepareTypewriter();

  animationStart = millis();
  animationActive = true;
}

// Animasyon alt ışığı kendisi kullanmıyorsa ve ayar açıksa alt ışık nefes alır
bool accompanyActive() {
  if (!settings.downlightAccompany) return false;
  if (!animationHourly && animationType == MIN_FLASH_BREATHE) return false;
  if (animationHourly && (animationType == HOUR_BREATHE || animationType == HOUR_DOWNLIGHT_RAINBOW)) return false;
  return true;
}

// Bir kare çizer; alt ışıkların parlaklık çarpanını döndürür
float drawAnimationFrame() {
  unsigned long elapsed = millis() - animationStart;
  if (elapsed >= animationDuration) {
    animationActive = false;
    drawFace(newFace);
    return 1.0;
  }
  float p = min(1.0f, (float)elapsed / animationPixelDuration);

  float factor = 1.0;
  if (animationHourly) {
    factor = drawHourAnimation(p);
  } else {
    for (int i = 0; i < LEDCLOCK_COUNT; i++) stripClock.setPixelColor(i, minuteAnimationPixel(i, p));
    // Kısa parlama + alt ışık nefesi: alt ışıklar bir kez yumuşakça kısılıp açılır
    if (animationType == MIN_FLASH_BREATHE) factor = breath(p);
  }

  if (accompanyActive()) factor *= breath((float)elapsed / animationDuration);
  return factor;
}

float breath(float t) {
  return 1 - 0.85 * (0.5 - 0.5 * cos(2 * PI * t));
}

// Saat çanı: 12 saatlik düzende saat kaçsa o kadar vuruş
int chimeCount() {
  int hour = displayedHour % 12;
  return hour == 0 ? 12 : hour;
}

// Daktilo: her rakamda yanacak yeni segmentlere sıra numarası verir
void prepareTypewriter() {
  typewriterSteps = 1;
  for (int d = 0; d < 4; d++) {
    int rank = 0;
    int segments = (d == 3) ? 2 : 7;
    for (int seg = 0; seg < 7; seg++) {
      typewriterRank[d][seg] = 0;
      if (seg >= segments) continue;
      int first = digitOffset(d) + seg * 9;
      if (oldFace[first] == 0 && newFace[first] != 0) typewriterRank[d][seg] = rank++;
    }
    if (rank > typewriterSteps) typewriterSteps = rank;
  }
}


// ---------- Dakika animasyonları (sadece değişen rakamlar) ----------

uint32_t minuteAnimationPixel(int i, float p) {
  uint32_t o = oldFace[i];
  uint32_t n = newFace[i];
  if (i >= FACE_PIXELS) return 0;

  // Bu ikisi değişen rakamın tamamını etkiler, değişmeyen segmentleri de
  if (animationType == MIN_FLASH || animationType == MIN_COLOURWAVE || animationType == MIN_FLASH_BREATHE) {
    if (n == 0 || !digitChanged[digitOfPixel(i)]) return n;
    if (animationType == MIN_FLASH) return blendColour(WHITE, n, smoothStep(p));
    // Parlama, nefesin ilk ~yarısında biter (normal hızda yine ~0.9 sn)
    if (animationType == MIN_FLASH_BREATHE) return blendColour(WHITE, n, smoothStep(p * 2.2));
    uint32_t rainbow = Adafruit_NeoPixel::ColorHSV((uint16_t)(uint32_t)(p * 98304 + i * 1200));
    return blendColour(rainbow, n, smoothStep(p));
  }

  // Bunlar değişen rakamın tamamını etkiler
  if (animationType == MIN_COMET || animationType == MIN_HEARTBEAT || animationType == MIN_EMBER) {
    int d = digitOfPixel(i);
    if (!digitChanged[d]) return n;
    return wholeDigitMinutePixel(i, d, p, o, n);
  }

  if (animationType == MIN_BLINK) {          // bütün saat bir an kısılıp açılır
    return (p < 0.5) ? scaleColour(o, 1 - smoothStep(p * 2)) : scaleColour(n, smoothStep((p - 0.5) * 2));
  }

  if (o == n) return n;

  switch (animationType) {
    case MIN_SLIDE_UP: {    // aşağıdan yukarı silinir, alttan yükselerek yazılır
      float y = 1 - pixelHeight(i);
      if (n == 0) return (p < 0.5 && y >= p * 2) ? o : 0;
      if (o == 0) return (p >= 0.5 && y < (p - 0.5) * 2) ? n : 0;
      return blendColour(o, n, p);
    }

    case MIN_TYPEWRITER: {  // eskiler kısa sürede söner, yeni segmentler tek tek belirir
      if (n == 0) return (p < 0.15) ? scaleColour(o, 1 - p / 0.15) : 0;
      if (o == 0) {
        int d = digitOfPixel(i);
        int seg = (i - digitOffset(d)) / 9;
        float appearAt = 0.15 + typewriterRank[d][seg] * (0.8 / typewriterSteps);
        return (p >= appearAt) ? n : 0;
      }
      return n;
    }

    case MIN_CROSSFADE:
      return blendColour(o, n, smoothStep(p));

    case MIN_DRAW: {        // önce eski segmentler geri çekilir, sonra yeniler çizilir
      float pos = positionInSegment(i);
      if (n == 0) return (p < 0.5 && pos < 1 - p * 2) ? o : 0;
      if (o == 0) return (p >= 0.5 && pos < (p - 0.5) * 2) ? n : 0;
      return blendColour(o, n, p);
    }

    case MIN_WIPE: {        // yukarıdan aşağı silinir, yukarıdan aşağı yazılır
      float y = pixelHeight(i);
      if (n == 0) return (p < 0.5 && y >= p * 2) ? o : 0;
      if (o == 0) return (p >= 0.5 && y < (p - 0.5) * 2) ? n : 0;
      return blendColour(o, n, p);
    }

    case MIN_DISSOLVE: {    // LED'ler rastgele sırayla değişir, değişirken bir an parlar
      float switchAt = pixelRandom(i, animationSeed) * 0.85;
      uint32_t colour = (p >= switchAt) ? n : o;
      float distance = fabs(p - switchAt);
      if (distance < 0.06) colour = blendColour(WHITE, colour, distance / 0.06);
      return colour;
    }
  }
  return n;
}


uint32_t wholeDigitMinutePixel(int i, int digit, float p, uint32_t o, uint32_t n) {
  switch (animationType) {
    case MIN_COMET: {       // beyaz bir nokta rakamın çizgileri boyunca koşar, arkasında yeni rakam kalır
      const float tail = 10;
      int length = (digit == 3) ? 18 : 63;
      float head = p * (length + tail);
      float behind = head - (i - digitOffset(digit));
      if (behind < 0) return o;
      if (behind < tail) return blendColour(WHITE, n, behind / tail);
      return n;
    }

    case MIN_HEARTBEAT: {   // iki hızlı parlama: tık-tık
      if (n == 0) return (p < 0.1) ? scaleColour(o, 1 - p / 0.1) : 0;
      float beat = exp(-sq((p - 0.2) / 0.06)) + 0.7 * exp(-sq((p - 0.45) / 0.06));
      return blendColour(n, WHITE, min(1.0f, beat) * 0.85);
    }

    case MIN_EMBER: {       // turuncu kor gibi titreşir, sonra kendi rengine döner
      const uint32_t ember = 0xFF5000;
      unsigned long frame = (millis() - animationStart) / 40;
      float flicker = 0.45 + 0.55 * pixelRandom(i + frame * 131, animationSeed);
      uint32_t glow = scaleColour(ember, flicker);
      if (n == 0) return (o == 0) ? 0 : scaleColour(glow, 1 - smoothStep(p));
      return blendColour(glow, n, smoothStep(p));
    }
  }
  return n;
}


// ---------- Saat başı animasyonları (bütün saat yüzü) ----------

float drawHourAnimation(float p) {
  switch (animationType) {
    case HOUR_SNAKE: {      // parlak bir ışık şerit boyunca koşar, arkasında yeni saat kalır
      const float tail = 25;
      float head = p * (FACE_PIXELS + tail);
      for (int i = 0; i < FACE_PIXELS; i++) {
        float behind = head - i;
        uint32_t colour;
        if (behind < 0) colour = oldFace[i];
        else if (behind < tail) colour = blendColour(WHITE, newFace[i], behind / tail);
        else colour = newFace[i];
        frameFace[i] = colour;
      }
      break;
    }

    case HOUR_RAINBOW: {    // bütün segmentlerin üzerinden renk dalgası geçer
      float strength = sin(PI * p);
      const uint32_t* base = (p < 0.5) ? oldFace : newFace;
      for (int i = 0; i < FACE_PIXELS; i++) {
        uint32_t rainbow = Adafruit_NeoPixel::ColorHSV((uint16_t)(uint32_t)(i * 640 + p * 131072));
        frameFace[i] = blendColour(base[i], rainbow, strength);
      }
      break;
    }

    case HOUR_SLOT: {       // rakamlar hızla döner, soldan sağa tek tek yeni saate oturur
      memcpy(frameFace, newFace, sizeof(frameFace));
      unsigned long frame = (millis() - animationStart) / 80;
      faceBuffer = frameFace;
      for (int d = 0; d < 4; d++) {
        float settleAt = 0.4 + 0.15 * (3 - d);
        if (p >= settleAt) continue;
        int offset = d * 63;
        faceFill(0, offset, d == 3 ? 18 : 63);
        float r = pixelRandom(d + frame * 7, animationSeed);
        uint32_t colour = (d <= 1) ? settings.minuteColour : settings.hourColour;
        if (d == 3) {
          if (r > 0.5) faceFill(colour, offset, 18);
        } else {
          displayNumber((int)(r * 10), offset, colour);
        }
      }
      faceBuffer = newFace;
      break;
    }

    case HOUR_FIREWORKS: {  // rastgele renkli patlamalar, arkada yeni saat belirir
      float baseLevel = (p < 0.75) ? 0.35 : 0.35 + 0.65 * smoothStep((p - 0.75) / 0.25);
      for (int i = 0; i < FACE_PIXELS; i++) {
        frameFace[i] = scaleColour(blendColour(oldFace[i], newFace[i], constrain(p * 4, 0.0, 1.0)), baseLevel);
      }
      for (int k = 0; k < 10; k++) {
        float age = (p - k * 0.065) / 0.25;
        if (age < 0 || age >= 1) continue;
        int centre = pixelRandom(k, animationSeed) * FACE_PIXELS;
        uint32_t colour = Adafruit_NeoPixel::ColorHSV((uint16_t)(pixelRandom(k + 100, animationSeed) * 65535));
        float radius = 3 + 12 * age;
        float fade = (1 - age) * (1 - age);
        for (int i = max(0, (int)(centre - radius)); i < min(FACE_PIXELS, (int)(centre + radius) + 1); i++) {
          float strength = fade * (1 - fabs(i - centre) / radius);
          if (strength > 0) frameFace[i] = addColour(frameFace[i], scaleColour(colour, strength));
        }
      }
      break;
    }

    case HOUR_FILL: {       // bütün segmentler sırayla dolar (88:88), fazlalar sönünce yeni saat kalır
      float fillEnd = constrain(p / 0.45, 0.0, 1.0) * FACE_PIXELS;
      for (int i = 0; i < FACE_PIXELS; i++) {
        uint32_t full = (digitOfPixel(i) <= 1) ? settings.minuteColour : settings.hourColour;
        uint32_t colour;
        if (p < 0.45) colour = (i < fillEnd) ? full : oldFace[i];
        else if (p < 0.6) colour = full;
        else {
          float t = smoothStep((p - 0.6) / 0.4);
          colour = newFace[i] ? blendColour(full, newFace[i], t) : scaleColour(full, 1 - t);
        }
        frameFace[i] = colour;
      }
      break;
    }

    case HOUR_CHIME: {      // saat kaçsa o kadar kez yanıp söner
      int count = chimeCount();
      float local = constrain((p - 0.1) / 0.9, 0.0, 1.0) * (count + 0.5);
      float level;
      if (p < 0.1) level = 0;
      else if (local < count) level = sin(PI * (local - floor(local)));
      else level = smoothStep((local - count) / 0.5);
      for (int i = 0; i < FACE_PIXELS; i++) {
        frameFace[i] = (p < 0.1) ? scaleColour(oldFace[i], 1 - p / 0.1) : scaleColour(newFace[i], level);
      }
      break;
    }

    case HOUR_COUNTDOWN: {  // 3... 2... 1... sonra yeni saat
      for (int i = 0; i < LEDCLOCK_COUNT; i++) frameFace[i] = 0;
      if (p < 0.08) {
        for (int i = 0; i < FACE_PIXELS; i++) frameFace[i] = scaleColour(oldFace[i], 1 - p / 0.08);
      } else if (p < 0.92) {
        float slot = (p - 0.08) / 0.28;            // 0-1: "3", 1-2: "2", 2-3: "1"
        int number = 3 - (int)slot;
        float level = sin(PI * (slot - floor(slot)));
        faceBuffer = frameFace;
        displayNumber(number, 0, settings.minuteColour);
        displayNumber(number, 63, settings.minuteColour);
        displayNumber(number, 126, settings.hourColour);
        faceBuffer = newFace;
        for (int i = 0; i < FACE_PIXELS; i++) frameFace[i] = scaleColour(frameFace[i], level);
      } else {
        for (int i = 0; i < FACE_PIXELS; i++) frameFace[i] = scaleColour(newFace[i], smoothStep((p - 0.92) / 0.08));
      }
      break;
    }

    case HOUR_MATRIX: {     // yeşil damlalar segmentlerden aşağı akar
      const uint32_t green = 0x00FF46;
      const uint32_t headColour = 0xB4FFB4;
      float baseOld = 1 - smoothStep(p / 0.2);
      float baseNew = smoothStep((p - 0.75) / 0.25);
      for (int i = 0; i < FACE_PIXELS; i++) {
        int d = digitOfPixel(i);
        float y = pixelHeight(i);
        uint32_t colour = addColour(scaleColour(oldFace[i], baseOld), scaleColour(newFace[i], baseNew));
        for (int k = 0; k < 4; k++) {
          float startAt = pixelRandom(d * 10 + k, animationSeed) * 0.55;
          float head = (p - startAt) / 0.35 * 1.35;
          float distance = head - y;
          if (distance < 0 || distance > 0.35) continue;
          uint32_t drop = (distance < 0.06) ? headColour : scaleColour(green, 1 - distance / 0.35);
          colour = addColour(colour, drop);
        }
        frameFace[i] = colour;
      }
      break;
    }

    case HOUR_OCEAN: {      // mavi-turkuaz dalga sağdan sola akar, arkasında yeni saat kalır
      float centre = -0.3 + p * 1.6;
      for (int i = 0; i < FACE_PIXELS; i++) {
        float x = (float)i / FACE_PIXELS;
        float strength = exp(-sq((x - centre) / 0.15));
        uint16_t hue = 36000 + 7000 * sin(i * 0.25 + p * 12);
        uint32_t wave = Adafruit_NeoPixel::ColorHSV(hue);
        uint32_t base = (x > centre) ? oldFace[i] : newFace[i];
        frameFace[i] = blendColour(base, wave, strength);
      }
      break;
    }

    case HOUR_LIGHTNING: {  // rastgele beyaz çakmalar, ardından yeni saat
      const uint32_t flashColour = 0xDDE6FF;
      float baseLevel = (p < 0.85) ? 0.25 : 0.25 + 0.75 * smoothStep((p - 0.85) / 0.15);
      float strike = 0;
      for (int k = 0; k < 4; k++) {
        float at = 0.05 + pixelRandom(k, animationSeed) * 0.7;
        strike = max(strike, (float)exp(-sq((p - at) / 0.015)));
        strike = max(strike, 0.6f * (float)exp(-sq((p - at - 0.04) / 0.012)));   // ikinci küçük çakma
      }
      for (int i = 0; i < FACE_PIXELS; i++) {
        uint32_t base = scaleColour((p < 0.5) ? oldFace[i] : newFace[i], baseLevel);
        frameFace[i] = addColour(base, scaleColour(flashColour, strike));
      }
      break;
    }

    case HOUR_DOWNLIGHT_RAINBOW: {   // rakamlar hemen değişir, alt ışıklar renk döngüsü yapar
      drawFace(newFace);
      uint32_t rainbow = Adafruit_NeoPixel::ColorHSV((uint16_t)(uint32_t)(p * 131072));
      downlightAnimationColour = blendColour(WHITE, rainbow, sin(PI * p));
      return 1.0;
    }

    case HOUR_BREATHE:      // rakamlar hemen değişir, alt ışıklar üç kez nefes alır
      drawFace(newFace);
      return 1 - 0.85 * (0.5 - 0.5 * cos(2 * PI * 3 * p));

    default:
      drawFace(newFace);
      return 1.0;
  }

  for (int i = FACE_PIXELS; i < LEDCLOCK_COUNT; i++) frameFace[i] = 0;
  drawFace(frameFace);
  return 1.0;
}


// ---------- Yardımcılar ----------

int digitOfPixel(int i) {
  return (i < 189) ? i / 63 : 3;
}

int digitOffset(int digit) {
  return digit * 63;
}

// LED'in kendi segmenti içindeki yeri: 0 (başı) ile 1 (sonu) arası
float positionInSegment(int i) {
  return ((i - digitOffset(digitOfPixel(i))) % 9 + 0.5) / 9.0;
}

// LED'in rakam içindeki yüksekliği: 0 (üst) ile 1 (alt) arası
float pixelHeight(int i) {
  int digit = digitOfPixel(i);
  int segment = (i - digitOffset(digit)) / 9;
  float t = positionInSegment(i);
  if (digit == 3) return segment == 0 ? 0.5 * t : 0.5 + 0.5 * t;  // "1": üst ve alt dikey çizgi

  switch (segment) {
    case 0: return 0.5 - 0.5 * t;   // sağ-üst
    case 1: return 0.0;             // üst
    case 2: return 0.5 * t;         // sol-üst
    case 3: return 0.5;             // orta
    case 4: return 0.5 + 0.5 * t;   // sağ-alt
    case 5: return 1.0;             // alt
    default: return 1.0 - 0.5 * t;  // sol-alt
  }
}

// Her LED ve animasyon için sabit, rastgele görünen 0-1 arası sayı
float pixelRandom(uint32_t i, uint32_t seed) {
  uint32_t x = i * 2654435761u ^ seed;
  x ^= x >> 15;
  x *= 2246822519u;
  x ^= x >> 13;
  return (x & 0xFFFF) / 65536.0;
}

float smoothStep(float t) {
  t = constrain(t, 0.0, 1.0);
  return t * t * (3 - 2 * t);
}

uint32_t blendColour(uint32_t from, uint32_t to, float t) {
  t = constrain(t, 0.0, 1.0);
  uint8_t r = ((from >> 16) & 0xFF) + (((int)((to >> 16) & 0xFF) - (int)((from >> 16) & 0xFF)) * t);
  uint8_t g = ((from >> 8) & 0xFF) + (((int)((to >> 8) & 0xFF) - (int)((from >> 8) & 0xFF)) * t);
  uint8_t b = (from & 0xFF) + (((int)(to & 0xFF) - (int)(from & 0xFF)) * t);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

uint32_t scaleColour(uint32_t colour, float t) {
  return blendColour(0, colour, t);
}

uint32_t addColour(uint32_t a, uint32_t b) {
  int r = min(255, (int)((a >> 16) & 0xFF) + (int)((b >> 16) & 0xFF));
  int g = min(255, (int)((a >> 8) & 0xFF) + (int)((b >> 8) & 0xFF));
  int bl = min(255, (int)(a & 0xFF) + (int)(b & 0xFF));
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | bl;
}
