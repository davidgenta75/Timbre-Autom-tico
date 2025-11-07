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
const int buzzerPin = 4;   
bool alarmaActivada = false;

// --- INTERRUPTOR PRINCIPAL ---
const int interruptorPin = 21;   // ON/OFF físico
bool apagado = false;
bool lastInterruptorState = HIGH;
unsigned long lastDebounce = 0;
unsigned long debounceDelay = 200;

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

void setup() {
  Serial.begin(115200);

  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  pinMode(interruptorPin, INPUT_PULLUP);

  Wire.begin(3, 4);   // SDA=3, SCL=4
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Iniciando...");

  if (!rtc.begin()) {
    Serial.println("Error: RTC no detectado!");
  }

  // --- Cargar WiFi almacenado ---
  Preferences preferences;
  preferences.begin("wifi", true);
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");
  preferences.end();

  if (ssid != "") {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    int intentos = 0;
    while (WiFi.status() != WL_CONNECTED && intentos < 20) {
      delay(500);
      intentos++;
    }
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
      }
    }
  }
}

void loop() {
  // --- Leer interruptor con antirrebote ---
  bool lectura = digitalRead(interruptorPin);
  if (lectura != lastInterruptorState) {
    lastDebounce = millis();
  }
  if ((millis() - lastDebounce) > debounceDelay) {
    if (lectura != apagado) {
      apagado = lectura;
    }
  }
  lastInterruptorState = lectura;

  // --- Obtener hora actual ---
  struct tm timeinfo;
  obtenerHora(timeinfo);

  // --- Pantalla rotativa ---
  static unsigned long ultimoCambio = 0;
  static bool mostrarHora = true;
  if (millis() - ultimoCambio > 8000) {
    mostrarHora = !mostrarHora;
    ultimoCambio = millis();
    lcd.clear();
  }

  // --- Línea superior: hora o fecha ---
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

  if (apagado == LOW) {
    // --- SISTEMA APAGADO ---
    lcd.setCursor(0, 1);
    lcd.print("    APAGADO     ");
    digitalWrite(buzzerPin, LOW);
    delay(500);
    return;
  }

  // --- SISTEMA ACTIVO ---
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

  // --- Timbre (lunes a viernes) ---
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

  // --- Línea inferior: próximo timbre ---
  lcd.setCursor(0, 1);
  lcd.print("Prox:");
  lcd.print(proxHora);
  lcd.print(" ");
  if (proxTipo == 'F') {
    lcd.print("FIN ");
  } else {
    lcd.print(textoTimbre(proxTipo));
  }

  delay(500);
}
