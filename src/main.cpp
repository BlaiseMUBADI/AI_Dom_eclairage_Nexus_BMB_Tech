#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <SinricPro.h>
#include <SinricProSwitch.h>
#include <SinricProDimSwitch.h>
#include <ArduinoJson.h>

// ===== CONFIGURATION WIFI =====
const char* ssid = "rr";
const char* password = "";

// ===== CONFIGURATION SINRICPRO =====
// Remplacez par vos vraies clés SinricPro
#define APP_KEY             "b4d53a2d-f6f9-4a19-8bbe-abc156e7c55d"      
#define APP_SECRET          "65d37efb-412f-42be-bbcb-743f82e1b938-ab75ea30-f9d0-4d1f-936d-d49dd0be41eb"   

#define ID_Lampe_salon      "68da7012382e7b1db3a3038c"     // ID pour la lampe salon
#define ID_Lampe_exterieur  "68d9a2375009c4f120fc1ed4" // ID pour la lampe extérieure  
#define ID_Lampe_chambre1   "68d9abea5009c4f120fc237d"  // ID pour la lampe chambre 1
// Pas d'ID pour Salon2 - il sera contrôlé automatiquement avec le salon principal

// ===== CONFIGURATION MATÉRIELLE =====
// Pins lampes
const int pin_lampe_salon = 14;
const int pin_lampe_exterieur = 27;
const int pin_lampe_ch1 = 26;
const int pin_lampe_salon2 = 25;  // Renommé de pin_lampe_ch2 vers pin_lampe_salon2
const int pin_led = 12;

// Variables pour états des lampes (pour SinricPro)
struct {
  bool salon = false;
  bool exterieur = false;
  bool chambre1 = false;
  bool salon2 = false;  // Renommé de chambre2 vers salon2
} etat_lampes;

// Variables pour la luminosité de la chambre 1
struct {
  int luminosite = 0;  // Niveau de luminosité (0-100%)
} chambre1_dim;

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

// ===== CALLBACKS SINRICPRO =====
/**
 * Callback pour la lampe du salon - Contrôle aussi automatiquement le salon2
 */
bool onLampeSalon(const String &deviceId, bool &state) {
  Serial.printf("Lampe salon %s via Google Home\r\n", state ? "allumée" : "éteinte");
  etat_lampes.salon = state;
  etat_lampes.salon2 = state;  // Salon2 suit automatiquement le salon principal
  
  // Contrôler les deux lampes salon
  digitalWrite(pin_lampe_salon, state ? HIGH : LOW);
  digitalWrite(pin_lampe_salon2, state ? HIGH : LOW);
  
  Serial.printf("Salon2 %s automatiquement\r\n", state ? "allumé" : "éteint");
  return true;
}

/**
 * Callback pour la lampe extérieure
 */
bool onLampeExterieur(const String &deviceId, bool &state) {
  Serial.printf("Lampe extérieur %s via Google Home\r\n", state ? "allumée" : "éteinte");
  etat_lampes.exterieur = state;
  digitalWrite(pin_lampe_exterieur, state ? HIGH : LOW);
  return true;
}

/**
 * Callback pour la lampe chambre 1 - ON/OFF avec variateur
 */
bool onLampeChambre1(const String &deviceId, bool &state) {
  Serial.printf("Lampe chambre 1 %s via Google Home\r\n", state ? "allumée" : "éteinte");
  etat_lampes.chambre1 = state;
  
  if (state) {
    // Allumer avec le dernier niveau de luminosité
    int luminosite = chambre1_dim.luminosite;
    if (luminosite == 0) luminosite = 50; // Valeur par défaut si jamais réglée
    
    // Conversion du pourcentage (0-100%) en valeur PWM (0-255)
    int pwmValue = map(luminosite, 0, 100, 0, 255);
    analogWrite(pin_lampe_ch1, pwmValue);
    Serial.printf("Chambre 1 allumée à %d%% (PWM: %d)\r\n", luminosite, pwmValue);
  } else {
    // Éteindre complètement la lampe
    analogWrite(pin_lampe_ch1, 0);
    Serial.printf("Chambre 1 éteinte\r\n");
  }
  
  return true;
}

/**
 * Callback pour la luminosité de la chambre 1
 */
bool onLuminositeChambre1(const String &deviceId, int &luminosite) {
  chambre1_dim.luminosite = luminosite;
  Serial.printf("Chambre 1 luminosité changée à %d%%\r\n", luminosite);
  
  // Ne change la luminosité que si la lampe est allumée
  if (etat_lampes.chambre1) {
    // Conversion du pourcentage en signal PWM
    int pwmValue = map(luminosite, 0, 100, 0, 255);
    analogWrite(pin_lampe_ch1, pwmValue);
    Serial.printf("Luminosité appliquée: %d%% (PWM: %d)\r\n", luminosite, pwmValue);
  } else {
    Serial.printf("Chambre 1 éteinte - luminosité mémorisée: %d%%\r\n", luminosite);
  }
  
  return true;
}

/**
 * Callback pour ajustement relatif de la luminosité chambre 1 (+/- valeurs)
 */
bool onAjustLuminositeChambre1(const String &deviceId, int &levelDelta) {
  // Calcul du nouveau niveau de luminosité
  chambre1_dim.luminosite += levelDelta;
  
  // Limiter la valeur entre 0 et 100%
  chambre1_dim.luminosite = constrain(chambre1_dim.luminosite, 0, 100);
  
  Serial.printf("Chambre 1 luminosité ajustée de %+d%% → %d%%\r\n", 
                levelDelta, chambre1_dim.luminosite);
  
  // Retourner la nouvelle valeur absolue (requis par SinricPro)
  levelDelta = chambre1_dim.luminosite;
  
  // Appliquer le nouveau niveau de luminosité si la lampe est allumée
  if (etat_lampes.chambre1) {
    int pwmValue = map(chambre1_dim.luminosite, 0, 100, 0, 255);
    analogWrite(pin_lampe_ch1, pwmValue);
    Serial.printf("Nouvelle luminosité appliquée: %d%% (PWM: %d)\r\n", 
                  chambre1_dim.luminosite, pwmValue);
  }
  
  return true;
}

// Salon2 n'a plus de callback séparé - il est contrôlé automatiquement par le salon principal

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

// ===== INITIALISATION SINRICPRO =====
void setupSinricPro() {
  // Configuration des callbacks pour 3 appareils seulement (limite gratuite)
  SinricProSwitch& lampe_salon = SinricPro[ID_Lampe_salon];
  SinricProSwitch& lampe_exterieur = SinricPro[ID_Lampe_exterieur];
  SinricProDimSwitch& lampe_chambre1 = SinricPro[ID_Lampe_chambre1];  // DimSwitch pour variateur
  // Salon2 est contrôlé automatiquement avec le salon principal

  // Callbacks pour switches simples
  lampe_salon.onPowerState(onLampeSalon);  // Contrôle salon + salon2 automatiquement
  lampe_exterieur.onPowerState(onLampeExterieur);
  
  // Callbacks pour dimswitch (chambre 1 avec variateur)
  lampe_chambre1.onPowerState(onLampeChambre1);
  lampe_chambre1.onPowerLevel(onLuminositeChambre1);
  lampe_chambre1.onAdjustPowerLevel(onAjustLuminositeChambre1);

  // Callbacks de statut de connexion SinricPro
  SinricPro.onConnected([](){
    Serial.println("[SinricPro]: Connexion établie - Google Home disponible");
  });
  
  SinricPro.onDisconnected([](){
    Serial.println("[SinricPro]: Connexion perdue");
  });

  // Démarrage du service SinricPro
  SinricPro.begin(APP_KEY, APP_SECRET);
  Serial.println("[SinricPro]: Service démarré");
}

void setup() {
  Serial.begin(9600);
  pinMode(pin_lampe_salon, OUTPUT);
  pinMode(pin_lampe_exterieur, OUTPUT);
  pinMode(pin_lampe_ch1, OUTPUT);
  pinMode(pin_lampe_salon2, OUTPUT);  // Renommé de pin_lampe_ch2
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

  // Initialisation SinricPro pour contrôle vocal Google Home
  setupSinricPro();

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
      else if (id == 4) pin = pin_lampe_salon2;  // ID 4 = Salon2
      if (pin != 0) {
        if (id == 1) {
          // Salon principal - contrôle aussi salon2 automatiquement
          digitalWrite(pin_lampe_salon, etat == "on" ? HIGH : LOW);
          digitalWrite(pin_lampe_salon2, etat == "on" ? HIGH : LOW);
          etat_lampes.salon = (etat == "on");
          etat_lampes.salon2 = (etat == "on");
        } else if (id == 3) {
          // Chambre 1 avec variateur PWM
          if (etat == "on") {
            int luminosite = chambre1_dim.luminosite;
            if (luminosite == 0) luminosite = 50; // Valeur par défaut
            int pwmValue = map(luminosite, 0, 100, 0, 255);
            analogWrite(pin, pwmValue);
          } else {
            analogWrite(pin, 0);
          }
          etat_lampes.chambre1 = (etat == "on");
        } else if (id == 4) {
          // Salon2 - contrôle individuel via web
          digitalWrite(pin, etat == "on" ? HIGH : LOW);
          etat_lampes.salon2 = (etat == "on");
        } else {
          // Extérieur - contrôle simple
          digitalWrite(pin, etat == "on" ? HIGH : LOW);
          etat_lampes.exterieur = (etat == "on");
        }
        
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
    json += "\"l3\":" + String(etat_lampes.chambre1) + ",";  // État logique pour PWM
    json += "\"l4\":" + String(digitalRead(pin_lampe_salon2));  // Salon2
    json += "}";
    request->send(200, "application/json", json);
  });

  // Contrôle de la luminosité chambre 1
  server.on("/luminosite", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("id") && request->hasParam("niveau")) {
      int id = request->getParam("id")->value().toInt();
      int niveau = request->getParam("niveau")->value().toInt();
      
      if (id == 3 && niveau >= 0 && niveau <= 100) { // Chambre 1 seulement
        chambre1_dim.luminosite = niveau;
        
        if (etat_lampes.chambre1) {
          int pwmValue = map(niveau, 0, 100, 0, 255);
          analogWrite(pin_lampe_ch1, pwmValue);
        }
        
        request->send(200, "text/plain", "OK");
        return;
      }
    }
    request->send(400, "text/plain", "Erreur");
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
  // Gestion des commandes SinricPro (Google Home)
  SinricPro.handle();
  
  // Le reste est géré par les requêtes HTTP asynchrones
}
