// Daily Fish
//
// Every 24 hours the panel wakes, generates a new fish (or an insect) on the
// device, draws it, and goes back to deep sleep. Pressing Reset switches
// between fish and insects and draws a new one at once.
//
// There is no clock. What is remembered between pictures is how many have
// been drawn, which picks the next one, and which subject it is showing;
// both are kept in flash.
//
// Hardware: Seeed Studio XIAO 7.5" ePaper Panel (XIAO ESP32-C3 + 800x480
// black/white UC8179 display).

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <soc/usb_serial_jtag_struct.h>

#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <GxEPD2_BW.h>

#include "config.h"
#include "fishdraw.h"
#include "layout.h"
#include "ota.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// The display driver is used directly, with our own frame buffer, rather than
// through GxEPD2's buffered wrapper, so the buffer only exists while drawing.
static EPD_DRIVER_CLASS epd(EPD_PIN_CS, EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY);

// --- Drawing -------------------------------------------------------------------

struct CanvasSink : fishdraw::LineSink {
  GFXcanvas1* canvas;
  void line(int x0, int y0, int x1, int y1) override { canvas->drawLine(x0, y0, x1, y1, 0); }
};

struct ShowResult {
  bool ok;
  uint32_t generate_ms;
  uint32_t refresh_ms;
  fishdraw::Stats stats;
};

#ifdef BENCH_MODE
// Sends the frame to the computer as hex, for tools/bench/capture.py.
static void dump_frame(const uint8_t* buf) {
  static const char HEX_DIGITS[] = "0123456789abcdef";
  const int row_bytes = layout::SCREEN_W / 8;
  char line[row_bytes * 2 + 2];
  Serial.printf("FRAME %d %d\n", layout::SCREEN_W, layout::SCREEN_H);
  for (int y = 0; y < layout::SCREEN_H; y++) {
    for (int x = 0; x < row_bytes; x++) {
      uint8_t b = buf[y * row_bytes + x];
      line[x * 2] = HEX_DIGITS[b >> 4];
      line[x * 2 + 1] = HEX_DIGITS[b & 15];
    }
    line[row_bytes * 2] = '\n';
    line[row_bytes * 2 + 1] = 0;
    Serial.print(line);
    delay(3);  // keep pace with the USB link
  }
  Serial.println("END");
}
#endif

// Draws the fish or flowers called `name` with `footer` in the bottom right
// corner (and the firmware version bottom left), and puts the page on the panel.
static ShowResult show(layout::Subject subject, const char* name, const char* footer) {
  ShowResult res;
  memset(&res, 0, sizeof(res));

  GFXcanvas1* canvas = new GFXcanvas1(layout::SCREEN_W, layout::SCREEN_H);
  if (!canvas || !canvas->getBuffer()) {
    Serial.println("no memory for the frame buffer");
    delete canvas;
    return res;
  }
  canvas->fillScreen(1);  // 1 = white, 0 = black, as the panel wants it
  canvas->setFont(&FreeSans9pt7b);
  canvas->setTextColor(0);
  canvas->setTextWrap(false);

  CanvasSink sink;
  sink.canvas = canvas;
  uint32_t t0 = millis();
  res.ok = layout::draw_page(subject, name, sink, &res.stats);
  res.generate_ms = millis() - t0;

  int16_t bx, by;
  uint16_t bw, bh;
  if (!res.ok) {
    const char* text = "Nothing to show this time.";
    canvas->getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    canvas->setCursor((layout::SCREEN_W - (int)bw) / 2 - bx, layout::SCREEN_H / 2);
    canvas->print(text);
  }
  if (SHOW_VERSION) {
    canvas->setCursor(layout::FOOTER_MARGIN_X, layout::FOOTER_BASELINE_Y);
    canvas->print(FW_VERSION);
  }
  if (footer && footer[0]) {
    canvas->getTextBounds(footer, 0, 0, &bx, &by, &bw, &bh);
    canvas->setCursor(layout::SCREEN_W - layout::FOOTER_MARGIN_X - (int)bw - bx, layout::FOOTER_BASELINE_Y);
    canvas->print(footer);
  }

#ifdef BENCH_MODE
  dump_frame(canvas->getBuffer());
#endif

  uint32_t t1 = millis();
  epd.init(0, true, 10, false);
  epd.writeImage(canvas->getBuffer(), 0, 0, layout::SCREEN_W, layout::SCREEN_H);
  epd.refresh(false);  // full refresh
  epd.hibernate();     // the panel holds the image with no power
  res.refresh_ms = millis() - t1;

  delete canvas;
  return res;
}

// Puts a small boxed message in the middle of the screen, over whatever is
// showing, using the display's fast partial refresh: about a second and a
// half, with no full-screen flash. It is the only sign that a press of Reset
// has been noticed while a picture is being worked out.
static uint32_t show_notice(const char* text) {
  const int W = 336, H = 56;  // width and left edge must be multiples of 8
  const int X = (layout::SCREEN_W - W) / 2, Y = (layout::SCREEN_H - H) / 2;
  GFXcanvas1 box(W, H);
  if (!box.getBuffer()) return 0;
  box.fillScreen(1);
  box.drawRect(0, 0, W, H, 0);
  box.drawRect(1, 1, W - 2, H - 2, 0);
  box.setFont(&FreeSans9pt7b);
  box.setTextColor(0);
  box.setTextWrap(false);
  int16_t bx, by;
  uint16_t bw, bh;
  box.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  box.setCursor((W - (int)bw) / 2 - bx, (H - (int)bh) / 2 - by);
  box.print(text);

  uint32_t t0 = millis();
  epd.init(0, false, 10, false);
  // A partial refresh only moves pixels it believes are changing. Telling the
  // panel the box used to be the exact opposite makes it drive every pixel,
  // which wipes the old picture from under the box.
  epd.writeImageToPrevious(box.getBuffer(), X, Y, W, H, true);
  epd.writeImage(box.getBuffer(), X, Y, W, H);
  epd.refresh(X, Y, W, H);
  epd.powerOff();
  return millis() - t0;
}

static void report(const char* name, const ShowResult& r) {
  Serial.printf("%s: ok=%d generate=%u ms refresh=%u ms segments=%u store=%u/%u overflow=%d min-free-heap=%u\n", name,
                r.ok, (unsigned)r.generate_ms, (unsigned)r.refresh_ms, (unsigned)r.stats.segments,
                (unsigned)r.stats.arena_peak, (unsigned)r.stats.arena_size, r.stats.overflow,
                (unsigned)ESP.getMinFreeHeap());
}

// --- The fish counter ------------------------------------------------------------

// Adds one to the number of fish this panel has generated, saves it to flash
// (it survives resets, power loss and new firmware), and returns it.
static uint32_t next_fish_number() {
  Preferences prefs;
  if (!prefs.begin("fish", false)) return esp_random();
  uint32_t n = prefs.getUInt("count", 0) + 1;
  prefs.putUInt("count", n);
  prefs.end();
  return n;
}

static uint32_t current_fish_number() {
  Preferences prefs;
  if (!prefs.begin("fish", true)) return 1;
  uint32_t n = prefs.getUInt("count", 1);
  prefs.end();
  return n;
}

// Set while a picture is being generated and put on the screen, so that the
// next start can tell that the last one never finished.
static bool drawing_in_progress() {
  Preferences prefs;
  if (!prefs.begin("fish", true)) return false;
  bool busy = prefs.getUChar("busy", 0) != 0;
  prefs.end();
  return busy;
}

static void set_drawing_in_progress(bool busy) {
  Preferences prefs;
  if (!prefs.begin("fish", false)) return;
  prefs.putUChar("busy", busy ? 1 : 0);
  prefs.end();
}

// Whether this firmware was built with a subject in it.
static bool subject_enabled(layout::Subject s) {
  if (s == layout::INSECT) return ENABLE_INSECTS != 0;
  if (s == layout::FLOWER) return ENABLE_FLOWERS != 0;
  return true;
}

static const char* subject_noun(layout::Subject s) {
  return s == layout::INSECT ? "an insect" : s == layout::FLOWER ? "flowers" : "a fish";
}

// The subject on show: kept in flash, like the count. A subject saved by an
// earlier firmware that this one does not have falls back to fish.
static layout::Subject saved_subject() {
  Preferences prefs;
  if (!prefs.begin("fish", true)) return layout::FISH;
  uint8_t v = prefs.getUChar("subject", 0);
  prefs.end();
  layout::Subject s = v == 1 ? layout::FLOWER : v == 2 ? layout::INSECT : layout::FISH;
  return subject_enabled(s) ? s : layout::FISH;
}

static void save_subject(layout::Subject s) {
  Preferences prefs;
  if (!prefs.begin("fish", false)) return;
  prefs.putUChar("subject", (uint8_t)s);
  prefs.end();
}

// Moves on to the next subject this firmware has: fish, insects, flowers.
static layout::Subject switch_subject() {
  static const layout::Subject ORDER[] = {layout::FISH, layout::INSECT, layout::FLOWER};
  layout::Subject now = saved_subject();
  int at = 0;
  for (int i = 0; i < 3; i++) {
    if (ORDER[i] == now) at = i;
  }
  layout::Subject next = now;
  for (int step = 1; step <= 3; step++) {
    layout::Subject s = ORDER[(at + step) % 3];
    if (subject_enabled(s)) {
      next = s;
      break;
    }
  }
  save_subject(next);
  return next;
}

// Generates and shows picture number `n` of this panel. The panel's own
// hardware address is part of the seed, so two panels do not show the same one.
// With `notice`, says so on the screen first.
static bool draw_number(layout::Subject subject, uint32_t n, bool notice = false) {
  if (notice && LOADING_NOTICE) {
    char text[48];
    snprintf(text, sizeof(text), "Drawing %s no. %u ...", subject_noun(subject), (unsigned)n);
    uint32_t ms = show_notice(text);
    Serial.printf("notice shown in %u ms\n", (unsigned)ms);
  }
  char seed[48], name[64], footer[24];
  snprintf(seed, sizeof(seed), "%012llx-%u", (unsigned long long)ESP.getEfuseMac(), (unsigned)n);
  fishdraw::make_name(seed, 0, name, sizeof(name));
  snprintf(footer, sizeof(footer), "No. %u", (unsigned)n);
  ShowResult r = show(subject, name, SHOW_FISH_NUMBER ? footer : "");
  Serial.printf("%s, no. %u: ", subject_noun(subject), (unsigned)n);
  report(name, r);
  return r.ok;
}

// --- Updates and serial commands ---------------------------------------------------

// Looks for a newer release and, if one was installed, restarts into it.
static void update_if_newer() {
#if OTA_ENABLED
  ota::Result r = ota::check_and_update(FW_VERSION);
  Serial.printf("update check: %s\n", ota::describe(r));
  if (r == ota::UPDATED) {
    Serial.println("restarting into the new firmware");
    Serial.flush();
    delay(200);
    ESP.restart();
  }
#endif
}

// Commands accepted over USB serial while the panel is awake:
//   ssid <network name>   then
//   pass <password>       saves the Wi-Fi settings to flash
//   ota                   look for a newer release now
//   status                version, uptime, why it last restarted, memory
// Returns false if the line was not one of these.
static bool common_command(const String& line) {
  static String pending_ssid;
  if (line.startsWith("ssid ")) {
    pending_ssid = line.substring(5);
    Serial.println("network name noted; now send: pass <password>");
  } else if (line == "pass" || line.startsWith("pass ")) {
    if (!pending_ssid.length()) {
      Serial.println("send the network name first: ssid <network name>");
    } else {
      ota::save_wifi(pending_ssid.c_str(), line.length() > 5 ? line.c_str() + 5 : "");
      Serial.println("Wi-Fi settings saved");
    }
  } else if (line == "status") {
    Serial.printf("daily-fish %s, up %lu s, reset reason %d, free heap %u, lowest %u\n", FW_VERSION,
                  (unsigned long)(millis() / 1000), (int)esp_reset_reason(), (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMinFreeHeap());
  } else if (line == "ota") {
    update_if_newer();
  } else {
    return false;
  }
  return true;
}

// Reads whatever has arrived on serial; returns a complete line, or "".
static String read_serial_line() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      String line = buf;
      buf = "";
      line.trim();
      if (line.length()) return line;
    } else if (buf.length() < 200) {
      buf += c;
    }
  }
  return "";
}

// --- Sleep -----------------------------------------------------------------------

// True if a computer (not just a charger) is on the USB port: a host sends a
// start-of-frame every millisecond, which advances this counter.
static bool usb_host_connected() {
  uint32_t a = USB_SERIAL_JTAG.fram_num.sof_frame_index;
  delay(5);
  uint32_t b = USB_SERIAL_JTAG.fram_num.sof_frame_index;
  delay(5);
  uint32_t c = USB_SERIAL_JTAG.fram_num.sof_frame_index;
  return a != b || b != c;
}

static void deep_sleep(uint64_t seconds) {
  // In deep sleep the USB port vanishes and new firmware cannot be uploaded.
  // When plugged into a computer, wait a little first so that it can be.
  if (USB_UPLOAD_WINDOW_S > 0 && usb_host_connected()) {
    Serial.printf("USB host connected: staying awake %d s for uploads\n", USB_UPLOAD_WINDOW_S);
    uint32_t t0 = millis();
    while (millis() - t0 < USB_UPLOAD_WINDOW_S * 1000UL) {
      String line = read_serial_line();
      if (line.length() && !common_command(line)) Serial.println("commands: ssid <name> | pass <password> | ota | status");
      delay(50);
    }
    if (seconds > USB_UPLOAD_WINDOW_S + 60) seconds -= USB_UPLOAD_WINDOW_S;
  }
  Serial.printf("sleeping for %llu s\n", (unsigned long long)seconds);
  Serial.flush();
  esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
  esp_deep_sleep_start();
}

#ifndef BENCH_MODE

void setup() {
  Serial.begin(115200);
  Serial.printf("daily-fish %s\n", FW_VERSION);
  ota::seed_wifi_from_build();
  // A firmware that arrived over the air is on trial until it has drawn a
  // fish; if it resets before this, the bootloader returns to the old one.
  // The Reset button (or switching the power on) moves on to the next
  // subject. A timer wake, or a restart after an update, keeps what it had.
  esp_reset_reason_t why = esp_reset_reason();
  Serial.printf("reset reason %d\n", (int)why);
  layout::Subject subject;
  uint32_t number;
  // A fish takes up to half a minute to appear, and nothing shows meanwhile.
  // If Reset is pressed again during that time, carry on with the picture
  // that was being drawn rather than switching away from it. (After a crash,
  // do move on, so that one bad picture cannot be retried for ever.)
  if (drawing_in_progress() && why == ESP_RST_POWERON) {
    subject = saved_subject();
    number = current_fish_number();
    Serial.println("the last picture was interrupted; drawing it again");
  } else {
    subject = why == ESP_RST_POWERON && SWITCH_ON_RESET ? switch_subject() : saved_subject();
    number = next_fish_number();
  }
  set_drawing_in_progress(true);
  // Someone is standing at the panel if it was the button; tell them.
  bool drew = draw_number(subject, number, why == ESP_RST_POWERON);
  set_drawing_in_progress(false);
  if (drew) ota::confirm_running_firmware();
  update_if_newer();
  deep_sleep(REFRESH_INTERVAL_S);
}

void loop() {}

#else  // BENCH_MODE

// Commands, one per line:
//   next              draw the next picture, exactly as a daily wake would
//   switch            move to the next subject and draw, as Reset would
//   name Some name    draw the fish with that species name
//   insect Some name  draw the insect with that species name
//   flower Some name  draw the flowers with that species name (if built in)
// plus the common commands (ssid, pass, ota).
static void bench_command(const String& line) {
  if (line == "next") {
    draw_number(saved_subject(), next_fish_number());
  } else if (line == "switch") {
    draw_number(switch_subject(), next_fish_number(), true);
  } else if (line.startsWith("name ")) {
    ShowResult r = show(layout::FISH, line.c_str() + 5, "Bench");
    report(line.c_str() + 5, r);
  } else if (line.startsWith("insect ")) {
    ShowResult r = show(layout::INSECT, line.c_str() + 7, "Bench");
    report(line.c_str() + 7, r);
  } else if (line.startsWith("flower ")) {
    ShowResult r = show(layout::FLOWER, line.c_str() + 7, "Bench");
    report(line.c_str() + 7, r);
  } else if (!common_command(line)) {
    Serial.println("commands: next | switch | name <text> | insect <text> | flower <text> | ssid <name> | pass <password> | ota");
  }
  Serial.println("READY");
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  ota::confirm_running_firmware();
  ota::seed_wifi_from_build();
  Serial.printf("reset reason %d\n", (int)esp_reset_reason());
  Serial.printf("daily-fish %s bench. free heap %u, largest block %u\n", FW_VERSION, (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
  Serial.println("READY");
}

void loop() {
  String line = read_serial_line();
  if (line.length()) bench_command(line);
  delay(5);
}

#endif
