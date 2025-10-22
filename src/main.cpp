#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

// Wi-Fi
const char* ssid = "rr";
const char* password = "";

// Pins lampes
const int pin_lampe_salon = 14;
const int pin_lampe_exterieur = 27;
const int pin_lampe_ch1 = 26;
const int pin_lampe_ch2 = 25;
const int pin_led = 12;

// Capteurs
const int pin_capteur_courant = 35;
const int pin_capteur_tension = 34;
const float Vref = 3.1;
const float Vref_courant = 3.3;
const int ADC_RESOLUTION = 4095;
const float calibrationFactor = 9;
const float Rshunt = 0.35;

float puissance = 0;
float tension = 0;
float courant = 0;
float energie_utilisee_Wh = 0;
unsigned long dernier_temps = 0;

AsyncWebServer server(80);

void Lecture_val_moyenne(int pin_tension, int pin_courant, int samples = 20) {
  long somme_val_lue_tension = 0;
  long somme_val_lue_courant = 0;
  for (int i = 0; i < samples; i++) {
    somme_val_lue_tension += analogRead(pin_tension);
    somme_val_lue_courant += analogRead(pin_courant);
    delay(3);
  }
  somme_val_lue_tension = somme_val_lue_tension / samples;
  somme_val_lue_courant = somme_val_lue_courant / samples;

  float tension_lue_vth = (somme_val_lue_tension * Vref) / ADC_RESOLUTION;
  tension = tension_lue_vth * calibrationFactor;
  float Vshunt = (somme_val_lue_courant * Vref_courant) / ADC_RESOLUTION;
  courant = Vshunt / Rshunt;
  puissance = tension * courant;
}

void updateEnergie() {
  unsigned long maintenant = millis();
  if (dernier_temps == 0) {
    dernier_temps = maintenant;
    return;
  }
  float dt_h = (maintenant - dernier_temps) / 3600000.0; // durée en heures
  energie_utilisee_Wh += puissance * dt_h;
  dernier_temps = maintenant;
}

void setup() {
  Serial.begin(9600);
  pinMode(pin_lampe_salon, OUTPUT);
  pinMode(pin_lampe_exterieur, OUTPUT);
  pinMode(pin_lampe_ch1, OUTPUT);
  pinMode(pin_lampe_ch2, OUTPUT);
  pinMode(pin_led, OUTPUT);

  digitalWrite(pin_led, HIGH);

  WiFi.begin(ssid, password);
  Serial.print("Connexion au Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(pin_led, !digitalRead(pin_led));
    delay(250);
    Serial.print(".");
  }
  Serial.println("\nConnecté à : " + WiFi.localIP().toString());
  digitalWrite(pin_led, HIGH);

  if (!SPIFFS.begin(true)) {
    Serial.println("Erreur SPIFFS");
    return;
  }

  // Fichiers statiques
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html");
  });
  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html");
  });
  server.on("/login.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/login.html", "text/html");
  });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/style.css", "text/css");
  });
  server.on("/bootstrap.min.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/bootstrap.min.css", "text/css");
  });

  // Contrôle des lampes
  server.on("/lampe", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("id") && request->hasParam("etat")) {
      int id = request->getParam("id")->value().toInt();
      String etat = request->getParam("etat")->value();
      int pin = 0;
      if (id == 1) pin = pin_lampe_salon;
      else if (id == 2) pin = pin_lampe_exterieur;
      else if (id == 3) pin = pin_lampe_ch1;
      else if (id == 4) pin = pin_lampe_ch2;
      if (pin != 0) {
        digitalWrite(pin, etat == "on" ? HIGH : LOW);
        request->send(200, "text/plain", "OK");
        return;
      }
    }
    request->send(400, "text/plain", "Erreur");
  });

  // Etat des lampes
  server.on("/etat_lampes", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"l1\":" + String(digitalRead(pin_lampe_salon)) + ",";
    json += "\"l2\":" + String(digitalRead(pin_lampe_exterieur)) + ",";
    json += "\"l3\":" + String(digitalRead(pin_lampe_ch1)) + ",";
    json += "\"l4\":" + String(digitalRead(pin_lampe_ch2));
    json += "}";
    request->send(200, "application/json", json);
  });

  // Mesures et calculs énergie/autonomie
  server.on("/mesures", HTTP_GET, [](AsyncWebServerRequest *request){
    float batt_ah = 10.0; // valeur par défaut
    if (request->hasParam("batt_ah")) {
      batt_ah = request->getParam("batt_ah")->value().toFloat();
      if (batt_ah < 1) batt_ah = 1;
    }
    Lecture_val_moyenne(pin_capteur_tension, pin_capteur_courant);
    updateEnergie();
    float capacite_batterie_Wh = batt_ah * tension; // tension mesurée
    float capacite_restante_Wh = capacite_batterie_Wh - energie_utilisee_Wh;
    if (capacite_restante_Wh < 0) capacite_restante_Wh = 0;
    float autonomie = 0;
    String autonomie_str = "--";
    if (puissance > 0.1) {
      autonomie = capacite_restante_Wh / puissance;
      autonomie_str = String(autonomie, 2);
    } else if (capacite_restante_Wh > 0) {
      autonomie_str = "∞";
    }
    float conso_kwh = energie_utilisee_Wh / 1000.0;
    float pourcentage = (capacite_batterie_Wh > 0) ? (capacite_restante_Wh / capacite_batterie_Wh) * 100.0 : 0;
    String json = "{\"tension\":" + String(tension,2) +
                  ",\"courant\":" + String(courant,3) +
                  ",\"puissance\":" + String(puissance,2) +
                  ",\"autonomie\":\"" + autonomie_str + "\"" +
                  ",\"conso_kwh\":" + String(conso_kwh,3) +
                  ",\"restant_wh\":" + String(capacite_restante_Wh,2) +
                  ",\"restant_pct\":" + String(pourcentage,1) + "}";
    request->send(200, "application/json", json);
  });

  server.begin();
}

void loop() {
  // Rien ici, tout est géré par les requêtes HTTP
}
