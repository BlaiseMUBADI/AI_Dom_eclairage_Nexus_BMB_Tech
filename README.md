# AI Dom éclairage Nexus BMB Tech

## Description
Système de domotique intelligent pour contrôle d'éclairage avec monitoring énergétique et contrôle vocal via Google Home.

## Fonctionnalités

### 🏠 Contrôle d'éclairage
- 4 lampes contrôlables (salon, extérieur, chambre 1, chambre 2)
- Interface web responsive
- **Contrôle vocal via Google Home** ✨

### ⚡ Monitoring énergétique
- Mesure tension, courant et puissance en temps réel
- Calcul de l'énergie consommée (Wh)
- Estimation de l'autonomie restante
- Pourcentage de batterie restant

### 📱 Interface web
- Serveur web asynchrone sur port 80
- Fichiers statiques depuis SPIFFS
- API REST pour contrôle et mesures

## Configuration

### 1. Configuration WiFi
Modifiez dans `main.cpp` :
```cpp
const char* ssid = "VOTRE_WIFI";
const char* password = "VOTRE_MOT_DE_PASSE";
```

### 2. Configuration SinricPro (pour Google Home)
1. Créez un compte sur [SinricPro](https://sinric.pro/)
2. Créez 4 appareils de type "Switch" :
   - Lampe Salon
   - Lampe Extérieur  
   - Lampe Chambre 1
   - Lampe Chambre 2
3. Remplacez dans `main.cpp` :
```cpp
#define APP_KEY             "VOTRE_APP_KEY"      
#define APP_SECRET          "VOTRE_APP_SECRET"   
#define ID_Lampe_salon      "ID_DE_VOTRE_LAMPE_SALON"
#define ID_Lampe_exterieur  "ID_DE_VOTRE_LAMPE_EXTERIEUR"
#define ID_Lampe_chambre1   "ID_DE_VOTRE_LAMPE_CHAMBRE1"
#define ID_Lampe_chambre2   "ID_DE_VOTRE_LAMPE_CHAMBRE2"
```

### 3. Configuration Google Home
1. Dans l'app Google Home, allez dans "+" > "Configurer un appareil"
2. Choisissez "Fonctionne avec Google"
3. Recherchez "SinricPro" et connectez votre compte
4. Vos lampes apparaîtront automatiquement

## Pins ESP32 utilisés
- **Lampes :** 14 (salon), 27 (extérieur), 26 (chambre 1), 25 (chambre 2)
- **LED indicateur :** 12
- **Capteurs :** 35 (tension), 34 (courant)

## Commandes vocales Google Home
- "Ok Google, allume la lampe du salon"
- "Ok Google, éteins la lampe extérieur"
- "Ok Google, allume toutes les lumières"
- "Ok Google, éteins la chambre 1"

## API endpoints
- `/lampe?id=X&etat=on/off` - Contrôle lampes (id: 1=salon, 2=extérieur, 3=chambre1, 4=chambre2)
- `/etat_lampes` - État actuel des lampes
- `/mesures?batt_ah=X` - Mesures énergétiques avec capacité batterie

## Installation
1. Clonez ce dépôt
2. Ouvrez avec PlatformIO
3. Configurez WiFi et SinricPro
4. Compilez et uploadez
5. Uploadez les fichiers SPIFFS

## Matériel requis
- ESP32
- 4 relais ou MOSFETs pour les lampes
- Capteur de tension (diviseur résistif)
- Capteur de courant (shunt 0.35Ω)
- LED indicateur

## Auteur
BlaiseMUBADI - BMB Tech

## Licence
MIT License