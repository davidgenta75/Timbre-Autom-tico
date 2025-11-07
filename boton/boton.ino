// ====== TIMBRE ESCOLAR AUTOMÁTICO (ESP32-C3 SuperMini) ======
// Alumno: Joaquín Genta 
// IPET N°363 - 7MO
#include <WiFi.h>
#include "time.h"
#include <Wire.h>
#include "RTClib.h"
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -3 * 3600;
const int   daylightOffset_sec = 0;

RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- BUZZER ---
const int buzzerPin = 2;   
bool alarmaActivada = false;

// --- INTERRUPTOR PRINCIPAL ---
const int interruptorPin = 9;   // ON/OFF
bool apagado = false;
bool lastInterruptorState = HIGH;
unsigned long lastDebounce = 0;

// --- HORARIOS DEL TIMBRE ---
struct HorarioTimbre {
  int hora;
  int minuto;
  char tipo;
};

HorarioTimbre horarios[] = {
  {7,  10, 'e'}, 
  {9,  10, 'r'}, 
  {9,  25, 'r'},
  {10, 45, 'r'}, 
  {10, 55, 'r'}, 
  {12, 55, 's'},
  {13, 30, 'e'}, 
  {15, 30, 'r'}, 
  {15, 40, 'r'},
  {17,  0, 's'},
  {17, 15, 'r'}, 
  {19, 15, 's'}
};
const int cantidadHorarios = sizeof(horarios) / sizeof(horarios[0]);

int duracionTimbre(char tipo) {
  if (tipo == 'e') return 5000;
  if (tipo == 's') return 5000;
  if (tipo == 'r') return 4000;
  return 10000;
}

String textoTimbre(char tipo) {
  if (tipo == 'e') return "ENTR";
  if (tipo == 's') return "SAL";
  if (tipo == 'r') return "REC";
  return "TMB";
}

bool obtenerHora(struct tm &timeinfo) {
  if (WiFi.status() == WL_CONNECTED && getLocalTime(&timeinfo)) {
    return true;
  } else {
    DateTime now = rtc.now();
    timeinfo.tm_year = now.year() - 1900;
    timeinfo.tm_mon  = now.month() - 1;
    timeinfo.tm_mday = now.day();
    timeinfo.tm_hour = now.hour();
    timeinfo.tm_min  = now.minute();
    timeinfo.tm_sec  = now.second();
    timeinfo.tm_wday = now.dayOfTheWeek();
    return false;
  }
}

void mostrarWiFiInfo() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi:");
  lcd.print(WiFi.SSID());
  lcd.setCursor(0, 1);
  lcd.print("IP:");
  lcd.print(WiFi.localIP().toString());
  delay(4000);
}

void setup() {
  Serial.begin(115200);

  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  pinMode(interruptorPin, INPUT_PULLUP);

  Wire.begin(6, 7);   // SDA=6, SCL=7
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Iniciando...");

  if (!rtc.begin()) {
    Serial.println("Error: RTC no detectado!");
  }

  // --- Conexión WiFi (usa credenciales guardadas o SmartConfig) ---
  Preferences preferences;
  preferences.begin("wifi", true);
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");
  preferences.end();

  WiFi.mode(WIFI_STA);
  if (ssid != "") {
    Serial.printf("Intentando conectar a %s\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
  }

  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(500);
    Serial.print(".");
    intentos++;
  }

  // --- Si no conecta, iniciar SmartConfig ---
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nNo conectado. Iniciando SmartConfig...");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SmartConfig...");
    WiFi.mode(WIFI_AP_STA);
    WiFi.beginSmartConfig();

    unsigned long startTime = millis();
    while (!WiFi.smartConfigDone() && millis() - startTime < 60000) { // Espera 1 min
      delay(500);
      Serial.print(".");
    }

    if (WiFi.smartConfigDone()) {
      Serial.println("\nSmartConfig recibido!");
      lcd.clear();
      lcd.print("Red recibida!");
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print("-");
      }
      Serial.println("\nWiFi conectado!");
      lcd.clear();
      lcd.print("WiFi conectado");

      // Guardar credenciales
      Preferences pref;
      pref.begin("wifi", false);
      pref.putString("ssid", WiFi.SSID());
      pref.putString("pass", WiFi.psk());
      pref.end();
    } else {
      Serial.println("\nSmartConfig falló o expiró.");
      lcd.clear();
      lcd.print("SmartCfg fallo");
    }
  }

  // --- Mostrar SSID e IP si conectado ---
  if (WiFi.status() == WL_CONNECTED) {
    mostrarWiFiInfo();
  }

  // --- Sincronizar NTP si hay conexión ---
  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      rtc.adjust(DateTime(
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
      ));
      Serial.println("Hora sincronizada con NTP");
    }
  }

  lcd.clear();
  lcd.print("Listo!");
  delay(1000);
}

void loop() {
  // --- Leer interruptor con anti-rebote ---
  bool interruptorState = digitalRead(interruptorPin);
  if (interruptorState == LOW && lastInterruptorState == HIGH && millis() - lastDebounce > 200) {
    apagado = !apagado;
    lastDebounce = millis();
  }
  lastInterruptorState = interruptorState;

  if (apagado) {
    lcd.setCursor(0, 0);
    lcd.print("    APAGADO     ");
    lcd.setCursor(0, 1);
    lcd.print("                ");
    digitalWrite(buzzerPin, LOW);
    delay(500);
    return;
  }

  // --- Sistema activo ---
  struct tm timeinfo;
  obtenerHora(timeinfo);

  // --- Resincronización diaria automática ---
  static unsigned long ultimaSync = 0;
  if (WiFi.status() == WL_CONNECTED && millis() - ultimaSync > 86400000) { // cada 24 h
    struct tm timeinfoNTP;
    if (getLocalTime(&timeinfoNTP)) {
      rtc.adjust(DateTime(
        timeinfoNTP.tm_year + 1900,
        timeinfoNTP.tm_mon + 1,
        timeinfoNTP.tm_mday,
        timeinfoNTP.tm_hour,
        timeinfoNTP.tm_min,
        timeinfoNTP.tm_sec
      ));
      ultimaSync = millis();
      Serial.println("RTC resincronizado con NTP");
    }
  }

  // Buscar próximo timbre
  String proxHora = "----";
  char proxTipo = 'F';
  for (int i = 0; i < cantidadHorarios; i++) {
    if ((timeinfo.tm_hour < horarios[i].hora) ||
        (timeinfo.tm_hour == horarios[i].hora && timeinfo.tm_min < horarios[i].minuto)) {
      char buf[6];
      sprintf(buf, "%02d:%02d", horarios[i].hora, horarios[i].minuto);
      proxHora = String(buf);
      proxTipo = horarios[i].tipo;
      break;
    }
  }

  // Activar timbre si corresponde
  if (timeinfo.tm_wday >= 1 && timeinfo.tm_wday <= 5) {
    for (int i = 0; i < cantidadHorarios; i++) {
      if (timeinfo.tm_hour == horarios[i].hora &&
          timeinfo.tm_min == horarios[i].minuto &&
          !alarmaActivada) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("  >> TIMBRE <<");
        lcd.setCursor(0, 1);
        lcd.print(textoTimbre(horarios[i].tipo));

        digitalWrite(buzzerPin, HIGH);
        delay(duracionTimbre(horarios[i].tipo));
        digitalWrite(buzzerPin, LOW);

        alarmaActivada = true;
        lcd.clear();
      }
    }
  }

  static int ultimoMin = -1;
  if (timeinfo.tm_min != ultimoMin) {
    alarmaActivada = false;
    ultimoMin = timeinfo.tm_min;
  }

  // --- Pantalla rotativa ---
  static unsigned long ultimoCambio = 0;
  static bool mostrarHora = true;
  if (millis() - ultimoCambio > 8000) {
    mostrarHora = !mostrarHora;
    ultimoCambio = millis();
    lcd.clear();
  }

  if (mostrarHora) {
    char horaStr[9];
    strftime(horaStr, sizeof(horaStr), "%H:%M:%S", &timeinfo);
    lcd.setCursor(0, 0);
    lcd.print("Hora: ");
    lcd.print(horaStr);
  } else {
    char fechaStr[11];
    strftime(fechaStr, sizeof(fechaStr), "%d/%m/%Y", &timeinfo);
    lcd.setCursor(0, 0);
    lcd.print("Fecha:");
    lcd.print(fechaStr);
  }

  lcd.setCursor(0, 1);
  lcd.print("Prox:");
  lcd.print(proxHora);
  lcd.print(" ");
  if (proxTipo == 'F') {
    lcd.print("FIN");
  } else {
    lcd.print(textoTimbre(proxTipo));
  }

  delay(500);
}
