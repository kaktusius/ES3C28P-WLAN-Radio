#include <Arduino.h>
#include "WiFi.h"
#include "Wire.h"
#include "es8311.h"
#include "Audio.h"
#include <lvgl.h>
#include <TFT_eSPI.h>

// Unsere neue Senderliste
#include "senderliste.h"

// --- WLAN Zugangsdaten ---
const char *ssid = "WLAN-580136";
const char *password = "66199503496327545101";

// --- Hardware Pins ---
#define I2C_SDA GPIO_NUM_16
#define I2C_SCL GPIO_NUM_15
#define I2C_SPEED 400000

#define I2S_BCK GPIO_NUM_5
#define I2S_WS GPIO_NUM_7
#define I2S_DOUT GPIO_NUM_8
#define I2S_MCK GPIO_NUM_4
#define AP_ENABLE GPIO_NUM_1

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// Touch Pins
#define TOUCH_FT6336_INT GPIO_NUM_0
#define TOUCH_FT6336_RST GPIO_NUM_3
#define TOUCH_I2C_ADDR 0x38

Audio audio;
ES8311 codec;
TFT_eSPI tft = TFT_eSPI();

int aktuellerTitel = 0;
bool isPlaying = false;

// Touch-Variablen
int touch_last_x = 0;
int touch_last_y = 0;

// --- LVGL Puffer & Styles ---
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCREEN_WIDTH * 10];

static lv_style_t style_btn_container;
static lv_style_t style_premium_btn;

// Externe Deklaration deines Hintergrundbildes
LV_IMG_DECLARE(carbon);
lv_obj_t *img_bg = NULL;
lv_obj_t *label_status = NULL;
lv_obj_t *btn_play_lbl = NULL;

// [NEU] Globale Variable für das WLAN-Icon und den Timer
lv_obj_t *wifi_icon = NULL;
static lv_timer_t *wifi_timer = NULL;

// --- TOUCH TREIBER ---
void touch_init() {
  pinMode(TOUCH_FT6336_INT, INPUT);
  pinMode(TOUCH_FT6336_RST, OUTPUT);
  digitalWrite(TOUCH_FT6336_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_FT6336_RST, HIGH);
  delay(50);
}

bool touch_touched() {
  uint8_t data[5];
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;

  Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)5);
  if (Wire.available() >= 5) {
    for (int i = 0; i < 5; i++) data[i] = Wire.read();
    int punkte = data[0] & 0x0F;
    if (punkte > 0 && punkte <= 2) {
      int raw_x = ((data[1] & 0x0F) << 8) | data[2];
      int raw_y = ((data[3] & 0x0F) << 8) | data[4];
      touch_last_x = map(raw_y, 0, 320, 0, 240 - 1);
      touch_last_y = map(raw_x, 0, 240, 0, 320 - 1);
      return true;
    }
  }
  return false;
}

// --- LVGL Callbacks ---
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  if (touch_touched()) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = map(touch_last_x, 0, 240, 0, SCREEN_WIDTH - 1);
    data->point.y = map((320 - 1) - touch_last_y, 0, 320, 0, SCREEN_HEIGHT - 1);
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void zwinge_neuen_sender() {
  Serial.printf("Wechsle zu Sender: %s\n", radioStations[aktuellerTitel].name);
  lv_label_set_text(label_status, radioStations[aktuellerTitel].name);
  lv_label_set_text(btn_play_lbl, LV_SYMBOL_PAUSE " Pause");

  audio.stopSong();
  audio.connecttohost(radioStations[aktuellerTitel].url);
  isPlaying = true;
}

static void btn_event_cb(lv_event_t *e) {
  uintptr_t id = (uintptr_t)lv_event_get_user_data(e);

  if (id == 3) {  // NEXT
    aktuellerTitel++;
    if (aktuellerTitel >= STATION_COUNT) aktuellerTitel = 0;
    zwinge_neuen_sender();
  } else if (id == 1) {  // PREV
    aktuellerTitel--;
    if (aktuellerTitel < 0) aktuellerTitel = STATION_COUNT - 1;
    zwinge_neuen_sender();
  } else if (id == 2) {  // PLAY/PAUSE
    if (isPlaying) {
      audio.pauseResume();
      isPlaying = false;
      lv_label_set_text(label_status, ". PAUSE .");
      lv_label_set_text(btn_play_lbl, LV_SYMBOL_PLAY " Play");
    } else {
      lv_label_set_text(label_status, radioStations[aktuellerTitel].name);
      lv_label_set_text(btn_play_lbl, LV_SYMBOL_PAUSE " Pause");
      audio.connecttohost(radioStations[aktuellerTitel].url);
      isPlaying = true;
    }
  }
}

static void volume_slider_event_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  int vol = (int)lv_slider_get_value(slider);
  audio.setVolume(vol);
}

// --- EQUALIZER CONFIGURATION ---
#define EQ_BAR_COUNT 10
static lv_obj_t *eq_bars[EQ_BAR_COUNT];
static lv_timer_t *eq_timer = NULL;
static volatile uint8_t eq_band_values[EQ_BAR_COUNT] = { 0 };

static void eq_timer_cb(lv_timer_t *timer) {
  if (isPlaying) {
    uint16_t vu = audio.getVUlevel();
    uint8_t left_channel = (vu >> 8) & 0xFF;
    uint8_t right_channel = vu & 0xFF;
    uint8_t raw_energy = (left_channel + right_channel) / 2;

    for (int b = 0; b < EQ_BAR_COUNT; b++) {
      int32_t band_target = 0;
      if (b < 3) {  // BASS
        band_target = (raw_energy * 0.25f);
        if (band_target > 35) band_target = 35;
        eq_band_values[b] = (eq_band_values[b] * 3 + band_target * 7) / 10;
      } else if (b < 7) {  // MITTEN
        band_target = (raw_energy * 0.2f) + 1;
        if (band_target > 35) band_target = 35;
        eq_band_values[b] = (eq_band_values[b] * 6 + band_target * 4) / 10;
      } else {  // HÖHEN
        band_target = (raw_energy * 0.25f) + 1;
        if (band_target > 35) band_target = 35;
        eq_band_values[b] = (eq_band_values[b] * 8 + band_target * 2) / 10;
      }
      lv_bar_set_value(eq_bars[b], eq_band_values[b], LV_ANIM_OFF);
    }
  } else {
    for (int b = 0; b < EQ_BAR_COUNT; b++) {
      if (eq_band_values[b] > 1) {
        eq_band_values[b]--;
      } else {
        eq_band_values[b] = 1;
      }
      lv_bar_set_value(eq_bars[b], eq_band_values[b], LV_ANIM_OFF);
    }
  }
}

// [NEU] --- WLAN RSSI Timer Callback ---
static void wifi_timer_cb(lv_timer_t *timer) {
  if (WiFi.status() != WL_CONNECTED) {
    // Nicht verbunden -> Rotes Warnsymbol
    lv_label_set_text(wifi_icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(wifi_icon, lv_color_make(255, 50, 50), 0);
    return;
  }

  long rssi = WiFi.RSSI();

  if (rssi >= -60) {
    // Hervorragend -> Grün & Volles Symbol
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifi_icon, lv_color_make(50, 220, 50), 0);
  } else if (rssi < -60 && rssi >= -73) {
    // Gut -> Cyan / Blau
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifi_icon, lv_color_make(0, 150, 255), 0);
  } else if (rssi < -73 && rssi >= -85) {
    // Schwach -> Orange & zusätzlicher Punkt als Indikator für "schwach"
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI ".");
    lv_obj_set_style_text_color(wifi_icon, lv_color_make(255, 150, 0), 0);
  } else {
    // Kritisch -> Rot & Warning-Symbol blinkt quasi
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI " !");
    lv_obj_set_style_text_color(wifi_icon, lv_color_make(255, 50, 50), 0);
  }
}

void init_ui() {
  // Hintergrundbild laden
  img_bg = lv_img_create(lv_scr_act());
  lv_img_set_src(img_bg, &carbon);
  lv_obj_align(img_bg, LV_ALIGN_CENTER, 0, 0);

  // WLAN Symbol oben rechts erstellen (Jetzt global zugewiesen)
  wifi_icon = lv_label_create(lv_scr_act());
  lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(wifi_icon, lv_color_make(150, 150, 150), 0);  // Startet grau
  lv_obj_align(wifi_icon, LV_ALIGN_TOP_RIGHT, -15, 15);

  // Styles definieren
  lv_style_init(&style_btn_container);
  lv_style_set_bg_opa(&style_btn_container, LV_OPA_20);
  lv_style_set_bg_color(&style_btn_container, lv_color_make(100, 150, 255));
  lv_style_set_border_color(&style_btn_container, lv_color_make(100, 170, 255));
  lv_style_set_border_width(&style_btn_container, 1);
  lv_style_set_radius(&style_btn_container, 16);

  lv_style_init(&style_premium_btn);
  lv_style_set_radius(&style_premium_btn, 12);
  lv_style_set_bg_opa(&style_premium_btn, LV_OPA_COVER);
  lv_style_set_bg_color(&style_premium_btn, lv_color_make(30, 80, 180));
  lv_style_set_bg_grad_color(&style_premium_btn, lv_color_make(15, 40, 110));
  lv_style_set_bg_grad_dir(&style_premium_btn, LV_GRAD_DIR_VER);
  lv_style_set_text_color(&style_premium_btn, lv_color_white());

  // Sendername Label
  label_status = lv_label_create(lv_scr_act());
  lv_obj_set_style_text_font(label_status, &lv_font_montserrat_26, 0);
  lv_obj_align(label_status, LV_ALIGN_TOP_MID, 0, 25);
  lv_obj_set_style_text_color(label_status, lv_color_make(200, 255, 200), 0);
  lv_label_set_text(label_status, "Warte auf WLAN...");

  // EQUALIZER CONTAINER
  lv_obj_t *eq_container = lv_obj_create(lv_scr_act());
  lv_obj_set_size(eq_container, 190, 50);
  lv_obj_align(eq_container, LV_ALIGN_BOTTOM_MID, 0, -130);
  lv_obj_set_style_bg_opa(eq_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(eq_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(eq_container, 0, 0);
  lv_obj_set_scroll_dir(eq_container, LV_DIR_NONE);
  lv_obj_set_layout(eq_container, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(eq_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(eq_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

  static lv_style_t style_eq_bar;
  lv_style_init(&style_eq_bar);
  lv_style_set_bg_color(&style_eq_bar, lv_color_make(220, 255, 50));
  lv_style_set_bg_grad_color(&style_eq_bar, lv_color_make(20, 100, 20));
  lv_style_set_bg_grad_dir(&style_eq_bar, LV_GRAD_DIR_VER);
  lv_style_set_radius(&style_eq_bar, 3);

  static lv_style_t style_eq_bg;
  lv_style_init(&style_eq_bg);
  lv_style_set_bg_color(&style_eq_bg, lv_color_make(10, 20, 10));
  lv_style_set_radius(&style_eq_bg, 3);

  for (int i = 0; i < EQ_BAR_COUNT; i++) {
    eq_bars[i] = lv_bar_create(eq_container);
    lv_obj_set_size(eq_bars[i], 10, 40);
    lv_obj_add_style(eq_bars[i], &style_eq_bg, LV_PART_MAIN);
    lv_obj_add_style(eq_bars[i], &style_eq_bar, LV_PART_INDICATOR);
    lv_bar_set_range(eq_bars[i], 0, 35);
    lv_bar_set_value(eq_bars[i], 1, LV_ANIM_OFF);
  }

  eq_timer = lv_timer_create(eq_timer_cb, 40, NULL);

  // [NEU] WLAN Timer erstellen – aktualisiert alle 3000ms (3 Sekunden)
  wifi_timer = lv_timer_create(wifi_timer_cb, 3000, NULL);

  // 4. Lautstärke-Slider mit Symbol
  lv_obj_t *slider = lv_slider_create(lv_scr_act());
  lv_obj_set_size(slider, 220, 12);
  lv_obj_align(slider, LV_ALIGN_CENTER, 0, 10);
  lv_slider_set_range(slider, 0, 21);
  lv_slider_set_value(slider, 3, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider, volume_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_set_style_bg_color(slider, lv_color_hex(0xEEEEEE), LV_PART_MAIN);
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_border_color(slider, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
  lv_obj_set_style_border_width(slider, 1, LV_PART_MAIN);

  // 2. Fortschrittsbalken (Indicator) in Grün
  lv_obj_set_style_bg_color(slider, lv_color_hex(0x2ECC71), LV_PART_INDICATOR);  // Schönes Smaragdgrün
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

  // 3. Den Knopf (Knob) anpassen (Größer, Weiß mit grünem Rand)
  lv_obj_set_style_bg_color(slider, lv_color_hex(0x009900), LV_PART_KNOB);
  lv_obj_set_style_border_color(slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);  // Grüner Rand passend zum Balken
  lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);

  // Macht den Knopf etwas größer als die Schiene, damit man ihn gut greifen kann
  lv_obj_set_style_pad_all(slider, 5, LV_PART_KNOB);

  lv_obj_t *volume_symbol = lv_label_create(lv_scr_act());
  lv_label_set_text(volume_symbol, LV_SYMBOL_VOLUME_MAX);
  lv_obj_set_style_text_color(volume_symbol, lv_color_make(100, 255, 100), 0);
  lv_obj_set_style_text_font(volume_symbol, &lv_font_montserrat_20, 0);
  lv_obj_align_to(volume_symbol, slider, LV_ALIGN_OUT_LEFT_MID, -15, 0);


#define APPLY_GREEN_BTN_STYLE(btn) \
  do { \
    /* --- Normaler Zustand: Grünverlauf --- */ \
    lv_obj_set_style_bg_color(btn, lv_color_make(40, 100, 40), 0); \
    lv_obj_set_style_bg_grad_color(btn, lv_color_make(0, 40, 0), 0); \
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0); \
    /* --- Gedrückter Zustand: Dunklerer Grünverlauf --- */ \
    lv_obj_set_style_bg_color(btn, lv_color_make(34, 153, 84), LV_STATE_PRESSED); \
    lv_obj_set_style_bg_grad_color(btn, lv_color_make(23, 90, 23), LV_STATE_PRESSED); \
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, LV_STATE_PRESSED); \
    /* --- Restliches Styling --- */ \
    lv_obj_set_style_border_color(btn, lv_color_make(46, 204, 113), 0); \
    lv_obj_set_style_border_color(btn, lv_color_make(34, 153, 84), LV_STATE_PRESSED); \
    lv_obj_set_style_border_width(btn, 1, 0); \
    lv_obj_set_style_radius(btn, 8, 0); \
    lv_obj_set_height(btn, 48); \
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_16, 0); \
  } while (0)

  lv_obj_t *btn_container = lv_obj_create(lv_scr_act());
  lv_obj_set_size(btn_container, 290, 80);  // Höhe leicht erhöht für größere Buttons
  lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_style(btn_container, &style_btn_container, 0);
  lv_obj_set_layout(btn_container, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Container-Styling (Dunkler Grund mit feiner grüner Kontur)
  lv_obj_set_style_bg_color(btn_container, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_border_color(btn_container, lv_color_hex(0x2ECC71), 0);
  lv_obj_set_style_border_width(btn_container, 1, 0);
  lv_obj_set_style_radius(btn_container, 12, 0);
  lv_obj_set_style_pad_ver(btn_container, 5, 0);
  lv_obj_set_style_pad_hor(btn_container, 8, 0);

  // --- PREV Button ---
  lv_obj_t *btn_prev = lv_btn_create(btn_container);
  lv_obj_add_style(btn_prev, &style_premium_btn, 0);
  lv_obj_set_width(btn_prev, 60);
  APPLY_GREEN_BTN_STYLE(btn_prev);
  lv_obj_t *lbl1 = lv_label_create(btn_prev);
  lv_label_set_text(lbl1, LV_SYMBOL_PREV);
  lv_obj_center(lbl1);
  lv_obj_add_event_cb(btn_prev, btn_event_cb, LV_EVENT_CLICKED, (void *)1);

  // --- PLAY/PAUSE Button ---
  lv_obj_t *btn_play = lv_btn_create(btn_container);
  lv_obj_add_style(btn_play, &style_premium_btn, 0);
  lv_obj_set_width(btn_play, 125);  // Breiter für besseren Fokus
  APPLY_GREEN_BTN_STYLE(btn_play);
  btn_play_lbl = lv_label_create(btn_play);
  lv_label_set_text(btn_play_lbl, LV_SYMBOL_PLAY " Radio");
  lv_obj_set_style_text_align(btn_play_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(btn_play_lbl);
  lv_obj_add_event_cb(btn_play, btn_event_cb, LV_EVENT_CLICKED, (void *)2);

  // --- NEXT Button ---
  lv_obj_t *btn_next = lv_btn_create(btn_container);
  lv_obj_add_style(btn_next, &style_premium_btn, 0);
  lv_obj_set_width(btn_next, 60);
  APPLY_GREEN_BTN_STYLE(btn_next);
  lv_obj_t *lbl3 = lv_label_create(btn_next);
  lv_label_set_text(lbl3, LV_SYMBOL_NEXT);
  lv_obj_center(lbl3);
  lv_obj_add_event_cb(btn_next, btn_event_cb, LV_EVENT_CLICKED, (void *)3);

#undef APPLY_GREEN_BTN_STYLE
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("--- S3 RADIO  STARTET ---");

  tft.begin();
  tft.setRotation(1);

  Wire.begin(I2C_SDA, I2C_SCL, I2C_SPEED);

  audio.setPinout(I2S_BCK, I2S_WS, I2S_DOUT, I2S_MCK);
  audio.setVolume(3);

  if (codec.begin(&Wire, 0x18)) {
    codec.setSampleRate(44100);
    codec.setVolume(85);
  }

  pinMode(AP_ENABLE, OUTPUT);
  digitalWrite(AP_ENABLE, LOW);

  touch_init();

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_WIDTH * 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_WIDTH;
  disp_drv.ver_res = SCREEN_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  init_ui();
  lv_timer_handler();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println("\nWLAN OK!");

  // Einmalig direkt triggern, damit nach dem Connect sofort die Farbe stimmt
  wifi_timer_cb(NULL);

  // Ersten Sender aus der Liste
  zwinge_neuen_sender();
}

void loop() {
  audio.loop();

  // Taktung für LVGL-Updates (alle 15ms)
  static uint32_t last_lvgl = 0;
  if (millis() - last_lvgl > 15) {
    last_lvgl = millis();
    lv_timer_handler();
  }
}