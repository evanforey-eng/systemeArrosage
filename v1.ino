#include "Pinouts.h"
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <Wire.h>
#include <WebServer.h>
#ifdef ARDUINO_SAMD_VARIANT_COMPLIANCE
#define SERIAL SerialUSB
#else
#define SERIAL Serial
#endif
#define BUZZER A0
#define CAPTLUM A2
#define POMPE RX
#define LED D4
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif
#define NUMPIXELS 10
#define CAPTHUMIDITE A4


unsigned char low_data[8] = { 0 };
unsigned char high_data[12] = { 0 };


#define NO_TOUCH 0xFE
#define THRESHOLD 100
#define ATTINY1_HIGH_ADDR 0x78
#define ATTINY2_LOW_ADDR 0x77


const char *ssid = "L'arroseur_arroser";
const char *password = "Jardiniermax";

WebServer server(80);

unsigned long DepartActivationPompe = 0;
unsigned long dureActivationPompe = 0;


Adafruit_NeoPixel pixels(NUMPIXELS, LED, NEO_GRB + NEO_KHZ800);

int val;
bool EtatPompe = false;
bool EtatAuto = false;
bool EtatTime = false;
bool EtatBouton = false;
int sensorHum = 0;

void setup() {
  pinMode(POMPE, OUTPUT);
  pinMode(CAPTLUM, INPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  pixels.begin();
  Wire.begin();


  Serial.print("Configuring access point ");
  if (!WiFi.softAP(ssid, password)) {
    log_e("Sorft AP creation failed.");
    while (1)
      ;
  }
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address");
  Serial.println(myIP);

  Serial.println("Server started");

  server.on("/", handleRoot);
  server.on("/toggle_pompe", HTTP_POST, handleTogglePompe);
  server.on("/toggle_auto", HTTP_POST, handleToggleAuto);
  server.on("/tpspompe", HTTP_POST, handlePompeTimer);
  server.on("/data", handleData);

  server.begin();
}

void loop() {
  sensorHum = analogRead(CAPTHUMIDITE) * 100 / 2060;

  if (EtatTime && (millis() - DepartActivationPompe >= dureActivationPompe)) {
    EtatTime = false;
    EtatPompe = false;
    EtatBouton = false;
    digitalWrite(POMPE, LOW);
    Serial.println("Pompe arrêtée (fin du timer)");
  }

  if (!EtatAuto && EtatPompe) {
    pompe();
    server.send(200, "text/plain", EtatPompe ? "1" : "0");
  }

  if (EtatAuto) {
    if (((analogRead(CAPTLUM) * 100 / 2800) > 80) && sensorHum < 20) {
      pompe();
    } else if (sensorHum > 40) {
      digitalWrite(POMPE, LOW);
    } else if ((analogRead(CAPTLUM) * 100 / 2800) < 80) {
        digitalWrite(POMPE, LOW);
      }
  }
  if (!EtatAuto && !EtatPompe) {
    digitalWrite(POMPE, LOW);
  }
  server.handleClient();
  val = check();

  pixels.clear();
  nivEau();
  pixels.show();
}

void getHigh12SectionValue(void) {
  memset(high_data, 0, sizeof(high_data));
  Wire.requestFrom(ATTINY1_HIGH_ADDR, 12);
  while (12 != Wire.available())
    ;

  for (int i = 0; i < 12; i++) {
    high_data[i] = Wire.read();
  }
  delay(10);
}

void getLow8SectionValue(void) {
  memset(low_data, 0, sizeof(low_data));
  Wire.requestFrom(ATTINY2_LOW_ADDR, 8);
  while (8 != Wire.available())
    ;

  for (int i = 0; i < 8; i++) {
    low_data[i] = Wire.read();  // receive a byte as character
  }
  delay(10);
}

int check() {
  int sensorvalue_min = 250;
  int sensorvalue_max = 255;
  int low_count = 0;
  int high_count = 0;
  while (1) {
    uint32_t touch_val = 0;
    uint8_t trig_section = 0;
    low_count = 0;
    high_count = 0;
    getLow8SectionValue();
    getHigh12SectionValue();
    for (int i = 0; i < 8; i++) {
      if (low_data[i] >= sensorvalue_min && low_data[i] <= sensorvalue_max) {
        low_count++;
      }
    }
    for (int i = 0; i < 12; i++) {
      if (high_data[i] >= sensorvalue_min && high_data[i] <= sensorvalue_max) {
        high_count++;
      }
    }

    for (int i = 0; i < 8; i++) {
      if (low_data[i] > THRESHOLD) {
        touch_val |= 1 << i;
      }
    }
    for (int i = 0; i < 12; i++) {
      if (high_data[i] > THRESHOLD) {
        touch_val |= (uint32_t)1 << (8 + i);
      }
    }

    while (touch_val & 0x01) {
      trig_section++;
      touch_val >>= 1;
    }

    return trig_section * 5;
  }
}

void handleData() {
  // Exemple de données simulées ou mesurées
  int val1 = val;                                    // niveau d'eau
  int val2 = analogRead(CAPTHUMIDITE) * 100 / 2060;  // humidité
  int val3 = analogRead(CAPTLUM) * 100 / 2800;       // luminosité

  String json = "{";
  json += "\"val1\":" + String(val1) + ",";
  json += "\"val2\":" + String(val2) + ",";
  json += "\"val3\":" + String(val3) + ",";
  json += "\"EtatPompe\":" + String(EtatPompe ? "true" : "false") + ",";
  json += "\"EtatAuto\":" + String(EtatAuto ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}


const char *htmlPage = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <title>Système d'arrosage automatique</title>
  <style>
    body {
      font-family: 'Arial', sans-serif;
      background: #f5f7fa;
      margin: 0;
      padding: 20px;
      color: #333;
    }
    h1 {
      text-align: center;
      color: #4CAF50;
    }
    .container {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 20px;
      max-width: 800px;
      margin: 40px auto;
    }
    .card {
      background: white;
      padding: 20px;
      border-radius: 12px;
      box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
      text-align: center;
      transition: transform 0.3s ease;
    }
    .card:hover {
      transform: scale(1.03);
    }
    .label {
      font-size: 1.2em;
      margin-bottom: 10px;
      color: #555;
    }
    .value {
      font-size: 2.5em;
      font-weight: bold;
      color: #222;
    }
  </style>
  <script>
    function fetchData() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          document.getElementById('v1').innerText = data.val1 + '%';
          document.getElementById('v2').innerText = data.val2 + '%';
          document.getElementById('v3').innerText = data.val3 + ' %';
          document.getElementById('btnPompe').innerText = data.EtatPompe ? "OFF" : "ON";
          document.getElementById('btnPompe').style.backgroundColor = data.EtatPompe ? "#f44336" : "#4CAF50";
          document.getElementById('btnAuto').innerText = data.EtatAuto ? "Non Activé" : "Activé";
          document.getElementById('btnAuto').style.backgroundColor = data.EtatAuto ? "#f44336" : "#4CAF50";
        });
    }

    function togglePompe() {
    fetch('/toggle_pompe', { method: 'POST' })
      .then(() => fetchData());
  }
  function toggleAuto() {
    fetch('/toggle_auto', { method: 'POST' })
      .then(() => fetchData());
  }
  function Timer() {
  const tps = document.getElementById("tpspompe").value;

  fetch('/tpspompe', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({ duration: tps })
  }).then(() => {
    alert("Pompe activée pour " + tps + " secondes !");
  });
}
    setInterval(fetchData, 1000); // Rafraîchir toutes les secondes
    window.onload = fetchData;
  </script>
</head>
<body>
  <h1>Tableau de Bord des Capteurs</h1>
  <div class="container">
    <div class="card">
      <div class="label">Niveau d'eau</div>
      <div class="value" id="v1">...</div>
    </div>
    <div class="card">
      <div class="label">Humidité</div>
      <div class="value" id="v2">...</div>
    </div>
    <div class="card">
      <div class="label">Luminosité</div>
      <div class="value" id="v3">...</div>
    </div>
  </div>
  <div class="card">
  <div class="label">Contrôle Pompe</div>
  <button onclick="togglePompe()" id="btnPompe" style="padding: 10px 20px; font-size: 1em; border: none; border-radius: 8px; background-color: #4CAF50; color: white; cursor: pointer;">
    ON
  </button>
</div>
<div class="card">
<div class="label">Contrôle Mode Auto</div>
  <button onclick="toggleAuto()" id="btnAuto" style="padding: 10px 20px; font-size: 1em; border: none; border-radius: 8px; background-color: #4CAF50; color: white; cursor: pointer;">
    ON
  </button>
</div>
<div class="card">
  <div class="label">Pompe minuterie</div>
  <select id="tpspompe" style="padding: 10px; font-size: 1em; border-radius: 6px;">
    <option value="3">3 secondes</option>
    <option value="5">5 secondes</option>
    <option value="10">10 secondes</option>
  </select>
  <br><br>
  <button onclick="Timer()" style="padding: 10px 20px; font-size: 1em; border: none; border-radius: 8px; background-color: #2196F3; color: white; cursor: pointer;">
    Lancer Pompe
  </button>
</div>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

void handleValeur() {
  server.send(200, "text/plain", String(val));
}

void handleTogglePompe() {
  EtatPompe = !EtatPompe;
  EtatBouton = !EtatBouton;
}

void handleToggleAuto() {
  EtatAuto = !EtatAuto;
}

void handlePompeTimer() {
  String body = server.arg("plain");

  int duration = 0;
  if (body.indexOf("3") != -1) duration = 3000;
  else if (body.indexOf("5") != -1) duration = 5000;
  else if (body.indexOf("10") != -1) duration = 10000;

  if (duration > 0) {
    digitalWrite(POMPE, HIGH);
    EtatPompe = true;
    EtatTime = true;
    DepartActivationPompe = millis();
    dureActivationPompe = duration;
    server.send(200, "text/plain", "Pompe activée pour " + String(duration / 1000) + "s");
  } else {
    server.send(400, "text/plain", "Durée invalide");
    EtatPompe = false;
  }
}

//if ((millis() - DepartActivationPompe >= dureActivationPompe)) {
//    digitalWrite(POMPE, LOW);
//    EtatPompe = false;
//   }

void pompe() {
  if (val <= 10) {
    digitalWrite(POMPE, LOW);
  } else if (val <= 20 && val > 10) {
    digitalWrite(POMPE, LOW);
  } else if (val <= 30 && val > 20) {
    digitalWrite(POMPE, HIGH);
  } else if (val <= 40 && val > 30) {
    digitalWrite(POMPE, HIGH);
  } else if (val <= 50 && val > 40) {
    digitalWrite(POMPE, HIGH);
  } else if (val <= 60 && val > 50) {
    digitalWrite(POMPE, HIGH);
  } else if (val <= 70 && val > 60) {
    digitalWrite(POMPE, HIGH);
  } else if (val <= 80 && val > 70) {
    digitalWrite(POMPE, HIGH);

  } else if (val <= 90 && val > 80) {
    digitalWrite(POMPE, HIGH);

  } else if (val > 90) {
    digitalWrite(POMPE, HIGH);
  }
}
void nivEau() {
  if (val <= 10) {
    tone(BUZZER, 85);
    delay(500);
    noTone(BUZZER);
    delay(500);
    pixels.setPixelColor(0, pixels.Color(255, 0, 0));


  } else if (val <= 20 && val > 10) {
    tone(BUZZER, 85);
    delay(500);
    noTone(BUZZER);
    delay(500);
    for (int i = 0; i < 2; i++) {
      pixels.setPixelColor(i, pixels.Color(255, 0, 0));
    }


  } else if (val <= 30 && val > 20) {
    for (int i = 0; i < 3; i++) {
      pixels.setPixelColor(i, pixels.Color(255, 255, 0));
    }
  } else if (val <= 40 && val > 30) {
    for (int i = 0; i < 4; i++) {
      pixels.setPixelColor(i, pixels.Color(255, 255, 0));
    }
  } else if (val <= 50 && val > 40) {
    for (int i = 0; i < 5; i++) {
      pixels.setPixelColor(i, pixels.Color(255, 255, 0));
    }
  } else if (val <= 60 && val > 50) {
    for (int i = 0; i < 6; i++) {
      pixels.setPixelColor(i, pixels.Color(0, 255, 0));
    }
  } else if (val <= 70 && val > 60) {
    for (int i = 0; i < 7; i++) {
      pixels.setPixelColor(i, pixels.Color(0, 255, 0));
    }
  } else if (val <= 80 && val > 70) {
    for (int i = 0; i < 8; i++) {
      pixels.setPixelColor(i, pixels.Color(0, 255, 0));
    }
  } else if (val <= 90 && val > 80) {
    for (int i = 0; i < 9; i++) {
      pixels.setPixelColor(i, pixels.Color(0, 255, 0));
    }
  } else if (val > 90) {
    for (int i = 0; i < 10; i++) {
      pixels.setPixelColor(i, pixels.Color(0, 255, 0));
    }
  }
}