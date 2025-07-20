#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
// Add after other includes
#include <iostream>
#include <sstream>
#include <vector>

#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

// // Telegram SSL Certificate
// #include <TelegramCertificate.h>

// WiFi Credentials
#define SSID "sim"
#define PASSWORD "simple12"

// Telegram Bot Token and Chat ID
#define BOT_TOKEN "BOT_TOKEN"  // Replace with your bot token
#define CHAT_ID "CHAT_ID"      // Replace with your chat ID

// Telegram bot check interval (1000ms = 1 second)
#define BOT_CHECK_INTERVAL 1000
unsigned long lastBotCheck = 0;

// Auto sensor data mode configuration
bool autoSenseDataMode = false;
unsigned long lastAutoSenseUpdate = 0;
#define AUTO_SENSE_INTERVAL 30000  // 30 seconds

// GPIO Assignments
#define DHTPIN 4
#define SOIL_MOISTURE_PIN 34
#define MQ2_PIN 35
#define RELAY_PIN 21
#define SOIL_SERVO_PIN 13
#define SEED_SERVO_PIN 14



// Servo Parameters
#define SOIL_SERVO_DIPPED_ANGLE 180
#define SOIL_SERVO_RETRACT_ANGLE 0
Servo soilServo;
Servo seedServo;

// DHT Sensor
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Global Variables
float temperature = 0;
float humidity = 0;
int soilMoisture = 0;
int gasValue = 0;
bool pumpStatus = false;
bool autoSoilMode = false;
unsigned long previousMillis = 0;
bool isDipped = false;

unsigned long servoTestStartTime = 0;  // Add this new variable to track when a servo test started
bool servoTestInProgress = false;      // Add this flag to track if a test is in progress

// Telegram Bot client
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// Claw Configuration
struct ServoPins {
  Servo servo;
  int servoPin;
  String servoName;
  int initialPosition;
};

std::vector<ServoPins> servoPins = {
  { Servo(), 15, "Base", 90 },  // Modify pin numbers as needed to avoid conflicts
  { Servo(), 16, "Shoulder", 90 },
  { Servo(), 17, "Elbow", 90 },
  { Servo(), 18, "Gripper", 90 },
};

struct RecordedStep {
  int servoIndex;
  int value;
  int delayInStep;
};

std::vector<RecordedStep> recordedSteps;
bool recordSteps = false;
bool playRecordedSteps = false;
unsigned long previousTimeInMilliClaw = millis();


// Create AsyncWebServer object
AsyncWebServer server(80);

// Create WebSocket object for claw
AsyncWebSocket wsRobotArmInput("/RobotArmInput");


// HTML Templates
// Current pump status API
const char *getPumpStatus() {
  return pumpStatus ? "ON" : "OFF";
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <title>Agro Expert Robot</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 20px; background-color: #f7f7f7; }
    .container { max-width: 800px; margin: 0 auto; }
    h1 { color: #2e7d32; }
    .sensor { background: white; padding: 20px; margin: 15px 0; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
    .sensor-value { font-size: 24px; font-weight: bold; margin: 5px 0; }
    .sensor-label { color: #555; font-size: 14px; }
    .warning { color: #d32f2f; font-weight: bold; }
    .ok { color: #388e3c; font-weight: bold; }
    .control-panel { background: white; padding: 20px; margin: 15px 0; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
    button { 
      padding: 12px 24px; 
      margin: 5px; 
      border: none; 
      border-radius: 5px; 
      background-color: #2e7d32; 
      color: white; 
      cursor: pointer;
      transition: background-color 0.3s; 
    }
    button:hover { background-color: #1b5e20; }
    #farmerAI { 
      font-size: 20px; 
      font-weight: bold; 
      padding: 15px 30px; 
      background-color: #1565c0; 
    }
    #farmerAI:hover { background-color: #0d47a1; }
    .slider-container { margin: 20px 0; }
    input[type="range"] { width: 80%; max-width: 300px; }
    .seed-angle { font-weight: bold; margin-top: 5px; }
  </style>
</head>
<body>
  <div class="container">
    <h1>Agro Expert Robot</h1>
    
    <div class="sensor">
      <h3>Sensor Data</h3>
      <div class="sensor-row">
        <div class="sensor-label">Soil Moisture</div>
        <div class="sensor-value" id="soilMoisture">--</div>
        <div id="moistureMsg" class="ok">Loading...</div>
      </div>
      <div class="sensor-row">
        <div class="sensor-label">Temperature</div>
        <div class="sensor-value" id="temperature">--</div>
      </div>
      <div class="sensor-row">
        <div class="sensor-label">Humidity</div>
        <div class="sensor-value" id="humidity">--</div>
      </div>
      <div class="sensor-row">
        <div class="sensor-label">Gas Value</div>
        <div class="sensor-value" id="gasValue">--</div>
      </div>
    </div>

          <div class="control-panel">
      <h3>Water Pump</h3>
      <button onclick="controlPump('on')" style="background-color: #388e3c;">Turn On Pump</button>
      <button onclick="controlPump('off')" style="background-color: #d32f2f;">Turn Off Pump</button>
      <div id="pumpStatus" style="margin-top: 10px; font-weight: bold;">Status: Unknown</div>
    </div>

    <div class="control-panel">
      <h3>Soil Moisture Sensor Control</h3>
      <button onclick="controlServo('test')">Test Soil Moisture</button>
      <button onclick="controlServo('auto')">Automatic Soil Moisture</button>
    </div>

    <div class="control-panel">
      <h3>Seed Dispenser</h3>
      <div class="slider-container">
        <input type="range" min="0" max="180" value="90" id="seedSlider" oninput="updateSeedAngle(this.value)">
        <div class="seed-angle">Angle: <span id="seedAngleValue">90</span>°</div>
      </div>
      <button onclick="setSeedAngle()">Set Angle</button>
    </div>
    
    <div style="display: flex; justify-content: center; gap: 10px;">
      <a href="/farmer-ai"><button id="farmerAI">FARMER AI</button></a>
      <a href="/claw-control"><button id="clawControl" style="background-color: #6a1b9a;">CLAW CONTROL</button></a>
    </div>

  </div>

  <script>
    let currentSeedAngle = 90;
    
    // Function to update the displayed seed angle without sending to the server
    function updateSeedAngle(angle) {
      document.getElementById('seedAngleValue').textContent = angle;
      currentSeedAngle = angle;
    }
    
    // Function to send the seed angle to the server
    function setSeedAngle() {
      fetch(`/seed?angle=${currentSeedAngle}`);
    }
    
    function controlPump(action) {
      console.log(`Sending pump command: ${action}`);
      fetch(`/pump?action=${action}`)
        .then(response => response.text())
        .then(data => {
          console.log(`Pump command response: ${data}`);
          // Optional: Update some visual indicator that the command was sent
          const statusMsg = document.createElement('div');
          statusMsg.textContent = `Pump ${action === 'on' ? 'activated' : 'deactivated'}`;
          statusMsg.style.color = action === 'on' ? '#388e3c' : '#d32f2f';
          statusMsg.style.fontWeight = 'bold';
          statusMsg.style.padding = '5px';
          
          // Insert after the buttons and remove after 2 seconds
          const controlPanel = document.querySelector('.control-panel');
          controlPanel.appendChild(statusMsg);
          setTimeout(() => statusMsg.remove(), 2000);
        })
        .catch(error => console.error('Error controlling pump:', error));
    }
    
    function controlServo(mode) {
      fetch(`/servo?mode=${mode}`);
    }
    
    // Function to update sensor values via AJAX
    function updateSensorValues() {
      fetch('/sensor-data')
        .then(response => response.json())
        .then(data => {
          document.getElementById('soilMoisture').textContent = data.soilMoisture + ' %';
          document.getElementById('temperature').textContent = data.temperature + ' °C';
          document.getElementById('humidity').textContent = data.humidity + ' %';
          document.getElementById('gasValue').textContent = data.gasValue;
          
          // Update pump status
          const pumpStatusElement = document.getElementById('pumpStatus');
          if (data.pumpStatus === "ON") {
            pumpStatusElement.textContent = "Status: ON";
            pumpStatusElement.style.color = "#388e3c";
          } else {
            pumpStatusElement.textContent = "Status: OFF";
            pumpStatusElement.style.color = "#d32f2f";
          }
          
          // Update moisture message
          const moistureMsg = document.getElementById('moistureMsg');
          if (data.soilMoisture < 40) {
            moistureMsg.textContent = 'Moisture low! Water needed.';
            moistureMsg.className = 'warning';
          } else {
            moistureMsg.textContent = 'Moisture OK.';
            moistureMsg.className = 'ok';
          }
        })
        .catch(error => console.error('Error fetching sensor data:', error));
    }
    
    // Initial update and set interval for periodic updates
    updateSensorValues();
    setInterval(updateSensorValues, 2000);
  </script>
</body>
</html>
)rawliteral";

const char farmer_ai_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <title>Farmer AI</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background-color: #f7f7f7; }
    .container { max-width: 800px; margin: 0 auto; }
    h1 { color: #1565c0; }
    .sensor { background: white; padding: 15px; margin: 15px 0; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
    .recommendation { 
      background: #e8f5e9; 
      padding: 15px; 
      margin: 10px 0; 
      border-radius: 10px;
      box-shadow: 0 1px 3px rgba(0,0,0,0.1);
    }
    .perfect-match { background: #c8e6c9; border-left: 5px solid #388e3c; }
    .good-match { background: #e8f5e9; border-left: 5px solid #81c784; }
    button { 
      padding: 12px 24px; 
      margin: 5px; 
      border: none; 
      border-radius: 5px; 
      background-color: #1565c0; 
      color: white; 
      cursor: pointer;
      transition: background-color 0.3s; 
    }
    button:hover { background-color: #0d47a1; }
  </style>
</head>
<body>
  <div class="container">
    <h1>Farmer AI Recommendations</h1>
    
    <div class="sensor" id="sensorData">
      <p>Current Soil Moisture: <span id="soilMoisture">--</span> %</p>
      <p>Current Temperature: <span id="temperature">--</span> °C</p>
      <p>Current Humidity: <span id="humidity">--</span> %</p>
    </div>
    
    <h3>Recommended Crops:</h3>
    <div id="recommendationsContainer">Loading recommendations...</div>
    
    <br>
    <a href="/"><button>Back to Home</button></a>
  </div>

  <script>
    // Function to update sensor values and recommendations
    function updateData() {
      // Fetch sensor data
      fetch('/sensor-data')
        .then(response => response.json())
        .then(data => {
          document.getElementById('soilMoisture').textContent = data.soilMoisture;
          document.getElementById('temperature').textContent = data.temperature;
          document.getElementById('humidity').textContent = data.humidity;
        })
        .catch(error => console.error('Error fetching sensor data:', error));
      
      // Fetch recommendations
      fetch('/recommendations')
        .then(response => response.text())
        .then(html => {
          document.getElementById('recommendationsContainer').innerHTML = html;
        })
        .catch(error => console.error('Error fetching recommendations:', error));
    }
    
    // Initial update and set interval for periodic updates
    updateData();
    setInterval(updateData, 5000);
  </script>
</body>
</html>
)rawliteral";

const char claw_control_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <title>Claw Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 20px; background-color: #f7f7f7; }
    .container { max-width: 800px; margin: 0 auto; }
    h1 { color: #6a1b9a; }
    table { width: 400px; margin: auto; table-layout: fixed; border-spacing: 10px; }
    .slider {
      -webkit-appearance: none;
      width: 100%;
      height: 20px;
      border-radius: 5px;
      background: #d3d3d3;
      outline: none;
      opacity: 0.7;
      -webkit-transition: .2s;
      transition: opacity .2s;
    }
    .slider:hover { opacity: 1; }
    .slider::-webkit-slider-thumb {
      -webkit-appearance: none;
      appearance: none;
      width: 40px;
      height: 40px;
      border-radius: 50%;
      background: #6a1b9a;
      cursor: pointer;
    }
    .slider::-moz-range-thumb {
      width: 40px;
      height: 40px;
      border-radius: 50%;
      background: #6a1b9a;
      cursor: pointer;
    }
    input[type=button] {
      background-color: #6a1b9a;
      color: white;
      border-radius: 30px;
      width: 100%;
      height: 40px;
      font-size: 20px;
      text-align: center;
      border: none;
      cursor: pointer;
    }
    button { 
      padding: 12px 24px; 
      margin: 5px; 
      border: none; 
      border-radius: 5px; 
      background-color: #2e7d32; 
      color: white; 
      cursor: pointer;
      transition: background-color 0.3s; 
    }
    button:hover { background-color: #1b5e20; }
  </style>
</head>
<body>
  <div class="container">
    <h1>Claw Control</h1>
    
    <table id="clawTable">
      <tr/><tr/>
      <tr>
        <td style="text-align:left;font-size:25px"><b>Gripper:</b></td>
        <td colspan=2>
          <div class="slidecontainer">
            <input type="range" min="15" max="170" value="90" class="slider" id="Gripper" oninput='sendButtonInput("Gripper",value)'>
          </div>
        </td>
      </tr>
      <tr/><tr/>
      <tr>
        <td style="text-align:left;font-size:25px"><b>Elbow:</b></td>
        <td colspan=2>
          <div class="slidecontainer">
            <input type="range" min="50" max="180" value="90" class="slider" id="Elbow" oninput='sendButtonInput("Elbow",value)'>
          </div>
        </td>
      </tr>
      <tr/><tr/>
      <tr>
        <td style="text-align:left;font-size:25px"><b>Shoulder:</b></td>
        <td colspan=2>
          <div class="slidecontainer">
            <input type="range" min="0" max="180" value="90" class="slider" id="Shoulder" oninput='sendButtonInput("Shoulder",value)'>
          </div>
        </td>
      </tr>
      <tr/><tr/>
      <tr>
        <td style="text-align:left;font-size:25px"><b>Base:</b></td>
        <td colspan=2>
          <div class="slidecontainer">
            <input type="range" min="0" max="180" value="90" class="slider" style="transform: scaleX(-1);" id="Base" oninput='sendButtonInput("Base",value)'>
          </div>
        </td>
      </tr>
      <tr/><tr/>
      <tr>
        <td style="text-align:left;font-size:25px"><b>Record:</b></td>
        <td><input type="button" id="Record" value="OFF" onclick='onclickButton(this)'></td>
        <td></td>
      </tr>
      <tr/><tr/>
      <tr>
        <td style="text-align:left;font-size:25px"><b>Play:</b></td>
        <td><input type="button" id="Play" value="OFF" onclick='onclickButton(this)'></td>
        <td></td>
      </tr>
    </table>
    
    <div style="margin-top: 30px;">
      <a href="/"><button>Back to Home</button></a>
    </div>
  </div>

  <script>
    var webSocketRobotArmInputUrl = "ws:\/\/" + window.location.hostname + "/RobotArmInput";
    var websocketRobotArmInput;

    function initRobotArmInputWebSocket() {
      websocketRobotArmInput = new WebSocket(webSocketRobotArmInputUrl);
      websocketRobotArmInput.onopen = function(event) {};
      websocketRobotArmInput.onclose = function(event) {
        setTimeout(initRobotArmInputWebSocket, 2000);
      };
      websocketRobotArmInput.onmessage = function(event) {
        var keyValue = event.data.split(",");
        var button = document.getElementById(keyValue[0]);
        button.value = keyValue[1];
        if (button.id == "Record" || button.id == "Play") {
          button.style.backgroundColor = (button.value == "ON" ? "green" : "#6a1b9a");
          enableDisableButtonsSliders(button);
        }
      };
    }

    function sendButtonInput(key, value) {
      var data = key + "," + value;
      websocketRobotArmInput.send(data);
    }

    function onclickButton(button) {
      button.value = (button.value == "ON") ? "OFF" : "ON";
      button.style.backgroundColor = (button.value == "ON" ? "green" : "#6a1b9a");
      var value = (button.value == "ON") ? 1 : 0;
      sendButtonInput(button.id, value);
      enableDisableButtonsSliders(button);
    }

    function enableDisableButtonsSliders(button) {
      if (button.id == "Play") {
        var disabled = "auto";
        if (button.value == "ON") {
          disabled = "none";
        }
        document.getElementById("Gripper").style.pointerEvents = disabled;
        document.getElementById("Elbow").style.pointerEvents = disabled;
        document.getElementById("Shoulder").style.pointerEvents = disabled;
        document.getElementById("Base").style.pointerEvents = disabled;
        document.getElementById("Record").style.pointerEvents = disabled;
      }
      if (button.id == "Record") {
        var disabled = "auto";
        if (button.value == "ON") {
          disabled = "none";
        }
        document.getElementById("Play").style.pointerEvents = disabled;
      }
    }

    window.onload = function() {
      initRobotArmInputWebSocket();
    };
  </script>
</body>
</html>
)rawliteral";

// Crop Data Structure
struct Crop {
  String name;
  int soilMin;
  int soilMax;
  int tempMin;
  int tempMax;
  int humidityMin;
  int humidityMax;
};

Crop crops[] = {
  { "Apple", 20, 25, 15, 21, 50, 60 },
  { "Mango", 15, 20, 24, 30, 50, 60 },
  { "Guava", 15, 20, 23, 28, 65, 75 },
  { "Potato", 65, 80, 15, 20, 80, 90 },
  { "Tomato", 60, 70, 20, 25, 60, 70 },
  { "Onion", 50, 60, 20, 25, 70, 80 },
  // Note: "Flooded" for Rice is interpreted as a very high moisture percentage
  { "Rice (Paddy)", 90, 100, 25, 35, 70, 80 },
  { "Wheat", 60, 70, 15, 25, 50, 60 },
  { "Maize (Corn)", 50, 60, 18, 27, 50, 60 },
  { "Bajra (Pearl Millet)", 40, 50, 25, 35, 40, 55 },
  { "Sugarcane", 60, 80, 20, 35, 70, 80 },
  { "Cotton", 40, 50, 25, 35, 50, 60 },
  { "Soybean", 60, 70, 20, 30, 60, 70 },
  { "Groundnut", 50, 60, 25, 30, 50, 60 },
  { "Mustard", 50, 60, 10, 25, 50, 60 },
  { "Banana", 60, 70, 26, 30, 75, 85 },
  { "Coconut", 60, 70, 25, 32, 80, 90 }
};

String getRecommendationsHTML() {
  String recommendations = "";
  int matches = 0;

  for (Crop crop : crops) {
    bool soilMatch = (soilMoisture >= crop.soilMin && soilMoisture <= crop.soilMax);
    bool tempMatch = (temperature >= crop.tempMin && temperature <= crop.tempMax);
    bool humidityMatch = (humidity >= crop.humidityMin && humidity <= crop.humidityMax);

    if (soilMatch && tempMatch && humidityMatch) {
      recommendations += "<div class='recommendation perfect-match'><b>" + crop.name + "</b> - Perfect match (all conditions met)";
      recommendations += "<p>Soil: " + String(crop.soilMin) + "-" + String(crop.soilMax) + "%, Temp: " + String(crop.tempMin) + "-" + String(crop.tempMax) + "°C, Humidity: " + String(crop.humidityMin) + "-" + String(crop.humidityMax) + "%</p></div>";
      matches++;
    } else if (soilMatch && tempMatch) {
      recommendations += "<div class='recommendation good-match'><b>" + crop.name + "</b> - Good match (soil & temp)";
      recommendations += "<p>Soil: " + String(crop.soilMin) + "-" + String(crop.soilMax) + "%, Temp: " + String(crop.tempMin) + "-" + String(crop.tempMax) + "°C, Humidity: " + String(crop.humidityMin) + "-" + String(crop.humidityMax) + "%</p></div>";
      matches++;
    }
    if (matches >= 5) break;
  }

  if (matches == 0) {
    recommendations = "<div class='recommendation'>No matching crops found for current conditions. Try adjusting soil moisture or environmental conditions.</div>";
  }

  return recommendations;
}

// WebSocket handler for robot arm input
void onRobotArmInputWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      sendCurrentRobotArmState();
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      AwsFrameInfo *info;
      info = (AwsFrameInfo *)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        std::string myData = "";
        myData.assign((char *)data, len);
        std::istringstream ss(myData);
        std::string key, value;
        std::getline(ss, key, ',');
        std::getline(ss, value, ',');
        Serial.printf("Key [%s] Value[%s]\n", key.c_str(), value.c_str());
        int valueInt = atoi(value.c_str());
        if (key == "Record") {
          recordSteps = valueInt;
          if (recordSteps) {
            recordedSteps.clear();
            previousTimeInMilliClaw = millis();
          }
        } else if (key == "Play") {
          playRecordedSteps = valueInt;
        } else if (key == "Base") {
          writeServoValues(0, valueInt);
        } else if (key == "Shoulder") {
          writeServoValues(1, valueInt);
        } else if (key == "Elbow") {
          writeServoValues(2, valueInt);
        } else if (key == "Gripper") {
          writeServoValues(3, valueInt);
        }
      }
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
    default:
      break;
  }
}

// Claw Functions
void writeServoValues(int servoIndex, int value) {
  if (recordSteps) {
    RecordedStep recordedStep;
    if (recordedSteps.size() == 0) {
      for (int i = 0; i < servoPins.size(); i++) {
        recordedStep.servoIndex = i;
        recordedStep.value = servoPins[i].servo.read();
        recordedStep.delayInStep = 0;
        recordedSteps.push_back(recordedStep);
      }
    }
    unsigned long currentTime = millis();
    recordedStep.servoIndex = servoIndex;
    recordedStep.value = value;
    recordedStep.delayInStep = currentTime - previousTimeInMilliClaw;
    recordedSteps.push_back(recordedStep);
    previousTimeInMilliClaw = currentTime;
  }
  servoPins[servoIndex].servo.write(value);
}

void sendCurrentRobotArmState() {
  for (int i = 0; i < servoPins.size(); i++) {
    wsRobotArmInput.textAll(servoPins[i].servoName + "," + servoPins[i].servo.read());
  }
  wsRobotArmInput.textAll(String("Record,") + (recordSteps ? "ON" : "OFF"));
  wsRobotArmInput.textAll(String("Play,") + (playRecordedSteps ? "ON" : "OFF"));
}

void playRecordedRobotArmSteps() {
  if (recordedSteps.size() == 0) {
    return;
  }
  for (int i = 0; i < 4 && playRecordedSteps; i++) {
    RecordedStep &recordedStep = recordedSteps[i];
    int currentServoPosition = servoPins[recordedStep.servoIndex].servo.read();
    while (currentServoPosition != recordedStep.value && playRecordedSteps) {
      currentServoPosition = (currentServoPosition > recordedStep.value ? currentServoPosition - 1 : currentServoPosition + 1);
      servoPins[recordedStep.servoIndex].servo.write(currentServoPosition);
      wsRobotArmInput.textAll(servoPins[recordedStep.servoIndex].servoName + "," + currentServoPosition);
      delay(50);
    }
  }
  delay(2000);
  for (int i = 4; i < recordedSteps.size() && playRecordedSteps; i++) {
    RecordedStep &recordedStep = recordedSteps[i];
    delay(recordedStep.delayInStep);
    servoPins[recordedStep.servoIndex].servo.write(recordedStep.value);
    wsRobotArmInput.textAll(servoPins[recordedStep.servoIndex].servoName + "," + recordedStep.value);
  }
}

// Create main menu keyboard
String getMainMenuKeyboard() {
  String keyboard = "[[\"📊 Sensor Data\", \"💧 Pump Control\"], ";
  keyboard += "[\"🌱 Seed Dispenser\", \"🔬 Soil Test\"], ";
  keyboard += "[\"🧠 Farmer AI\", \"⚙️ Auto Mode\"], ";
  keyboard += "[\"🏠 Back to Home\"]]";
  return keyboard;
}

// Create pump control keyboard
String getPumpControlKeyboard() {
  String keyboard = "[[\"Turn ON Pump\", \"Turn OFF Pump\"], ";
  keyboard += "[\"🏠 Back to Main Menu\"]]";
  return keyboard;
}

// Create soil test keyboard
String getSoilTestKeyboard() {
  String keyboard = "[[\"Test Soil Moisture\", \"Auto Soil Moisture\"], ";
  keyboard += "[\"🏠 Back to Main Menu\"]]";
  return keyboard;
}

// Create auto mode keyboard
String getAutoModeKeyboard() {
  String status = autoSenseDataMode ? "ON" : "OFF";
  String keyboard = "[[\"Auto Sense Data: " + status + "\"], ";
  keyboard += "[\"🏠 Back to Main Menu\"]]";
  return keyboard;
}

// Create seed dispenser keyboard
String getSeedDispenserKeyboard() {
  String keyboard = "[[\"Dispense at 45°\", \"Dispense at 90°\", \"Dispense at 135°\"], ";
  keyboard += "[\"Dispense at 180°\", \"🏠 Back to Main Menu\"]]";
  return keyboard;
}

// Send sensor data to Telegram
void sendSensorData() {
  String message = "📊 *Current Sensor Readings* 📊\n";
  message += "------------------------------\n";
  message += "🌡 Temperature: " + String(temperature, 1) + " °C\n";
  message += "💧 Humidity: " + String(humidity, 1) + " %\n";
  message += "🌱 Soil Moisture: " + String(soilMoisture) + " %\n";
  message += "🔥 Gas Value: " + String(gasValue) + "\n";
  message += "💧 Pump Status: " + String(pumpStatus ? "ON" : "OFF") + "\n";

  bot.sendMessage(CHAT_ID, message, "Markdown");
}

// Send recommendations from Farmer AI
void sendFarmerAIRecommendations() {
  String message = "🧠 *Farmer AI Recommendations* 🧠\n";
  message += "------------------------------\n";
  message += "Current Conditions:\n";
  message += "🌱 Soil Moisture: " + String(soilMoisture) + " %\n";
  message += "🌡 Temperature: " + String(temperature, 1) + " °C\n";
  message += "💧 Humidity: " + String(humidity, 1) + " %\n\n";
  message += "Recommended Crops:\n";

  int matches = 0;
  for (Crop crop : crops) {
    bool soilMatch = (soilMoisture >= crop.soilMin && soilMoisture <= crop.soilMax);
    bool tempMatch = (temperature >= crop.tempMin && temperature <= crop.tempMax);
    bool humidityMatch = (humidity >= crop.humidityMin && humidity <= crop.humidityMax);

    if (soilMatch && tempMatch && humidityMatch) {
      message += "✅ *" + crop.name + "* - Perfect match!\n";
      message += "   Soil: " + String(crop.soilMin) + "-" + String(crop.soilMax) + "%, ";
      message += "Temp: " + String(crop.tempMin) + "-" + String(crop.tempMax) + "°C, ";
      message += "Humidity: " + String(crop.humidityMin) + "-" + String(crop.humidityMax) + "%\n\n";
      matches++;
    } else if (soilMatch && tempMatch) {
      message += "👍 *" + crop.name + "* - Good match\n";
      message += "   Soil: " + String(crop.soilMin) + "-" + String(crop.soilMax) + "%, ";
      message += "Temp: " + String(crop.tempMin) + "-" + String(crop.tempMax) + "°C, ";
      message += "Humidity: " + String(crop.humidityMin) + "-" + String(crop.humidityMax) + "%\n\n";
      matches++;
    }
    if (matches >= 5) break;
  }

  if (matches == 0) {
    message += "❌ No matching crops found for current conditions.\n";
    message += "Try adjusting soil moisture or environmental conditions.";
  }

  bot.sendMessage(CHAT_ID, message, "Markdown");
}

// Send notification about soil moisture
void checkAndNotifySoilMoisture() {
  static bool hasNotified = false;

  if (soilMoisture < 40) {
    if (!hasNotified) {
      String message = "⚠️ *Alert: Low Soil Moisture!* ⚠️\n";
      message += "Current soil moisture is " + String(soilMoisture) + "%\n";
      message += "Water is needed!";
      bot.sendMessage(CHAT_ID, message, "Markdown");
      hasNotified = true;
    }
  } else {
    hasNotified = false;
  }
}

// Send notification for pump status change
void notifyPumpStatus(bool isOn) {
  String message = isOn ? "💧 *Pump turned ON* 💧\nWatering started!" : "💧 *Pump turned OFF* 💧\nWatering completed!";
  bot.sendMessage(CHAT_ID, message, "Markdown");
}

// Send notification for seed dispenser
void notifySeedDispenser(bool isStart, int angle) {
  String message = isStart ? "🌱 *Seed dispenser activated* 🌱\nSeed started sowing at " + String(angle) + "° angle!" : "🌱 *Seed dispenser stopped* 🌱\nSeed sowing done!";
  bot.sendMessage(CHAT_ID, message, "Markdown");
}

// Handle new messages
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;

    if (from_name == "") from_name = "Guest";

    // Handle commands
    if (text == "/start" || text == "🏠 Back to Home" || text == "🏠 Back to Main Menu") {
      String welcome = "Welcome to *Agro Expert Robot*, " + from_name + "!\n";
      welcome += "Use the keyboard below to control your robot.";
      bot.sendMessageWithReplyKeyboard(chat_id, welcome, "Markdown", getMainMenuKeyboard(), true);
    }

    // Handle sensor data request
    else if (text == "📊 Sensor Data") {
      sendSensorData();
    }

    // Handle pump control
    else if (text == "💧 Pump Control") {
      String message = "💧 *Pump Control* 💧\n";
      message += "Current status: " + String(pumpStatus ? "ON" : "OFF");
      bot.sendMessageWithReplyKeyboard(chat_id, message, "Markdown", getPumpControlKeyboard(), true);
    } else if (text == "Turn ON Pump") {
      digitalWrite(RELAY_PIN, LOW);  // Turn ON (active low)
      pumpStatus = true;
      Serial.println("Pump turned ON via Telegram");
      notifyPumpStatus(true);
    } else if (text == "Turn OFF Pump") {
      digitalWrite(RELAY_PIN, HIGH);  // Turn OFF
      pumpStatus = false;
      Serial.println("Pump turned OFF via Telegram");
      notifyPumpStatus(false);
    }

    // Handle soil test
    else if (text == "🔬 Soil Test") {
      String message = "🔬 *Soil Test Control* 🔬\n";
      message += "Select an option:";
      bot.sendMessageWithReplyKeyboard(chat_id, message, "Markdown", getSoilTestKeyboard(), true);
    } else if (text == "Test Soil Moisture") {
      if (!servoTestInProgress) {
        soilServo.write(SOIL_SERVO_DIPPED_ANGLE);
        servoTestInProgress = true;
        servoTestStartTime = millis();
        Serial.println("Soil moisture test started via Telegram - servo dipped");
        bot.sendMessage(chat_id, "🔬 Soil moisture test started!");
      } else {
        soilServo.write(SOIL_SERVO_RETRACT_ANGLE);
        servoTestInProgress = false;
        Serial.println("Soil moisture test manually stopped via Telegram - servo retracted");
        bot.sendMessage(chat_id, "🔬 Soil moisture test stopped!");
      }
    } else if (text == "Auto Soil Moisture") {
      autoSoilMode = !autoSoilMode;
      String status = autoSoilMode ? "enabled" : "disabled";
      Serial.println("Auto soil mode " + status + " via Telegram");
      bot.sendMessage(chat_id, "🔄 Auto soil moisture mode " + status + "!");
    }

    // Handle seed dispenser
    else if (text == "🌱 Seed Dispenser") {
      String message = "🌱 *Seed Dispenser Control* 🌱\n";
      message += "Select an angle:";
      bot.sendMessageWithReplyKeyboard(chat_id, message, "Markdown", getSeedDispenserKeyboard(), true);
    } else if (text.indexOf("Dispense at") >= 0) {
      // Extract angle from message
      int angle = text.substring(text.indexOf("at") + 3, text.indexOf("°")).toInt();

      notifySeedDispenser(true, angle);
      seedServo.write(angle);
      delay(1000);  // Wait for servo to move

      // After a delay, consider the operation complete
      notifySeedDispenser(false, angle);
    }

    // Handle Farmer AI
    else if (text == "🧠 Farmer AI") {
      sendFarmerAIRecommendations();
    }

    // Handle Auto Mode
    else if (text == "⚙️ Auto Mode") {
      String message = "⚙️ *Auto Mode Settings* ⚙️\n";
      message += "Auto Sense Data Mode: " + String(autoSenseDataMode ? "ON" : "OFF") + "\n";
      message += "When enabled, you'll receive sensor data every 30 seconds.";
      bot.sendMessageWithReplyKeyboard(chat_id, message, "Markdown", getAutoModeKeyboard(), true);
    } else if (text.indexOf("Auto Sense Data:") >= 0) {
      autoSenseDataMode = !autoSenseDataMode;
      String message = "⚙️ Auto Sense Data Mode: " + String(autoSenseDataMode ? "ON" : "OFF");
      bot.sendMessageWithReplyKeyboard(chat_id, message, "Markdown", getAutoModeKeyboard(), true);

      if (autoSenseDataMode) {
        lastAutoSenseUpdate = millis() - AUTO_SENSE_INTERVAL + 5000;  // Send first update in 5 seconds
      }
    }
  }
}





void setup() {
  Serial.begin(115200);
  dht.begin();

  // Configure relay pin
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);  // Relay OFF (active low)
  pumpStatus = false;
  Serial.println("Relay initialized in OFF state");

  // Configure servo motors
  soilServo.attach(SOIL_SERVO_PIN);
  seedServo.attach(SEED_SERVO_PIN);
  soilServo.write(SOIL_SERVO_RETRACT_ANGLE);

  // Initialize the claw servos
  for (int i = 0; i < servoPins.size(); i++) {
    servoPins[i].servo.attach(servoPins[i].servoPin);
    servoPins[i].servo.write(servoPins[i].initialPosition);
  }

  // Connect to WiFi
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to WiFi! IP Address: ");
  Serial.println(WiFi.localIP());

  // Configure Telegram bot
  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);  // Add SSL certificate
  bot.sendMessage(CHAT_ID, "🤖 Agro Expert Robot is online! 🤖\nUse /start to begin.");

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });

  // Route for farmer AI page
  server.on("/farmer-ai", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", farmer_ai_html);
  });

  // Route to get current sensor data as JSON
  server.on("/sensor-data", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"temperature\":" + String(temperature, 1) + ",";
    json += "\"humidity\":" + String(humidity, 1) + ",";
    json += "\"soilMoisture\":" + String(soilMoisture) + ",";
    json += "\"gasValue\":" + String(gasValue) + ",";
    json += "\"pumpStatus\":\"" + String(getPumpStatus()) + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  // Route to get crop recommendations
  server.on("/recommendations", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", getRecommendationsHTML());
  });

  // Route to control the pump
  server.on("/pump", HTTP_GET, [](AsyncWebServerRequest *request) {
    String action;
    if (request->hasParam("action")) {
      action = request->getParam("action")->value();
      Serial.print("Pump action received: ");
      Serial.println(action);

      if (action == "on") {
        digitalWrite(RELAY_PIN, LOW);  // Turn ON (active low)
        pumpStatus = true;
        Serial.println("Pump turned ON");
        notifyPumpStatus(true);  // Add this line
      } else if (action == "off") {
        digitalWrite(RELAY_PIN, HIGH);  // Turn OFF
        pumpStatus = false;
        Serial.println("Pump turned OFF");
        notifyPumpStatus(false);  // Add this line
      }
    }
    request->send(200, "text/plain", "OK");
  });

  // Route to control the soil moisture servo
  server.on("/servo", HTTP_GET, [](AsyncWebServerRequest *request) {
    String mode = request->getParam("mode")->value();
    if (mode == "test") {
      if (!servoTestInProgress) {
        // Start a new test
        soilServo.write(SOIL_SERVO_DIPPED_ANGLE);
        servoTestInProgress = true;
        servoTestStartTime = millis();
        Serial.println("Soil moisture test started - servo dipped");
      } else {
        // If already testing, manually retract it
        soilServo.write(SOIL_SERVO_RETRACT_ANGLE);
        servoTestInProgress = false;
        Serial.println("Soil moisture test manually stopped - servo retracted");
      }
    } else if (mode == "auto") {
      autoSoilMode = !autoSoilMode;
      Serial.println(autoSoilMode ? "Auto mode enabled" : "Auto mode disabled");
    }
    request->send(200, "text/plain", "OK");
  });

  // Route to control the seed dispenser servo
  server.on("/seed", HTTP_GET, [](AsyncWebServerRequest *request) {
    int angle = request->getParam("angle")->value().toInt();
    angle = constrain(angle, 0, 180);

    notifySeedDispenser(true, angle);  // Add this line
    seedServo.write(angle);

    // After a delay, consider the operation complete
    // We'll do this asynchronously in the main loop
    // to avoid blocking the web server

    request->send(200, "text/plain", "OK");
  });

  // Route for claw control page
  server.on("/claw-control", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", claw_control_html);
  });

  // Register the WebSocket handler for the claw
  wsRobotArmInput.onEvent(onRobotArmInputWebSocketEvent);
  server.addHandler(&wsRobotArmInput);

  // Start server
  server.begin();
}

void loop() {
  // Read sensors every 2 seconds
  static unsigned long lastSensorRead = 0;
  if (millis() - lastSensorRead >= 2000) {
    // Read temperature and humidity with error handling
    float newTemp = dht.readTemperature();
    float newHumidity = dht.readHumidity();

    // Check if reads failed
    if (!isnan(newTemp)) temperature = newTemp;
    else Serial.println("Failed to read temperature!");

    if (!isnan(newHumidity)) humidity = newHumidity;
    else Serial.println("Failed to read humidity!");

    // Read soil moisture
    int rawSoilValue = analogRead(SOIL_MOISTURE_PIN);
    soilMoisture = map(rawSoilValue, 0, 4095, 100, 0);  // Inverted for most soil moisture sensors
    soilMoisture = constrain(soilMoisture, 0, 100);     // Ensure values are within 0-100 range

    // Read gas value
    gasValue = analogRead(MQ2_PIN);

    // Print sensor values to Serial Monitor
    Serial.println("------------- Sensor Readings -------------");
    Serial.printf("Temperature: %.2f°C\n", temperature);
    Serial.printf("Humidity: %.2f%%\n", humidity);
    Serial.printf("Soil Moisture: %d%%\n", soilMoisture);
    Serial.printf("Gas Value: %d\n", gasValue);
    Serial.println("-------------------------------------------");

    lastSensorRead = millis();
  }

  // Handle automatic soil servo
  if (autoSoilMode && millis() - previousMillis >= 5000) {
    isDipped = !isDipped;
    soilServo.write(isDipped ? SOIL_SERVO_DIPPED_ANGLE : SOIL_SERVO_RETRACT_ANGLE);
    previousMillis = millis();
  }

  // // Handle servo test timeout (auto-retract after 3 seconds)
  // if (servoTestInProgress && (millis() - servoTestStartTime >= 3000)) {
  //   soilServo.write(SOIL_SERVO_RETRACT_ANGLE);
  //   servoTestInProgress = false;
  //   Serial.println("Soil moisture test completed - servo auto-retracted");
  // }

  // Handle claw playback if active
  if (playRecordedSteps) {
    playRecordedRobotArmSteps();
  }
  // Clean up WebSocket clients
  wsRobotArmInput.cleanupClients();

  // Check for new Telegram messages
  if (millis() - lastBotCheck > BOT_CHECK_INTERVAL) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    if (numNewMessages) {
      Serial.println("Got " + String(numNewMessages) + " new messages");
      handleNewMessages(numNewMessages);
    }

    lastBotCheck = millis();
  }

  // Handle Auto Sense Data Mode
  if (autoSenseDataMode && millis() - lastAutoSenseUpdate >= AUTO_SENSE_INTERVAL) {
    sendSensorData();
    lastAutoSenseUpdate = millis();
  }

  // Check soil moisture and send notification if needed
  checkAndNotifySoilMoisture();
}