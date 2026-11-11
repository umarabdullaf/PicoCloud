
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>

// ===================================
// WIFI CONFIGURATION
// ===================================

const char* WIFI_SSID = "YOUR_HOTSPOT_NAME";
const char* WIFI_PASSWORD = "YOUR_HOTSPOT_PASSWORD";

// ===================================
// SD CARD SPI CONFIGURATION
// ===================================

#define SD_CS    5
#define SD_SCK   18
#define SD_MISO  19
#define SD_MOSI  23

// ===================================
// SERVER
// ===================================

WebServer server(80);

// ===================================
// FILE CONTENT TYPES
// ===================================

String getContentType(String filename) {

  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".css"))  return "text/css";
  if (filename.endsWith(".js"))   return "application/javascript";
  if (filename.endsWith(".json")) return "application/json";
  if (filename.endsWith(".png"))  return "image/png";
  if (filename.endsWith(".jpg"))  return "image/jpeg";
  if (filename.endsWith(".jpeg")) return "image/jpeg";
  if (filename.endsWith(".svg"))  return "image/svg+xml";
  if (filename.endsWith(".ico"))  return "image/x-icon";
  if (filename.endsWith(".txt"))  return "text/plain";
  if (filename.endsWith(".pdf"))  return "application/pdf";

  return "application/octet-stream";
}

// ===================================
// SERVE WEBSITE FILES
// ===================================

void handleRequest() {

  String path = server.uri();

  // Prevent directory traversal
  if (path.indexOf("..") >= 0 ||
      path.indexOf('\\') >= 0 ||
      path.indexOf('\0') >= 0) {

    server.send(403, "text/plain", "Forbidden");
    return;
  }

  // Website paths must start with /site1 or /site2
  if (!(path.startsWith("/site1") ||
        path.startsWith("/site2"))) {

    server.send(404, "text/plain", "Website not found");
    return;
  }

  // Redirect directory URLs to trailing slash
  if (path == "/site1" || path == "/site2") {

    server.sendHeader("Location", path + "/");
    server.send(301, "text/plain", "");
    return;
  }

  // Serve index.html for website directories
  if (path.endsWith("/")) {
    path += "index.html";
  }

  // Open file from microSD
  File file = SD.open(path, FILE_READ);

  if (!file || file.isDirectory()) {

    if (file) file.close();

    server.send(404, "text/plain", "File not found");
    return;
  }

  String contentType = getContentType(path);

  server.streamFile(file, contentType);

  file.close();
}

// ===================================
// SETUP
// ===================================

void setup() {

  Serial.begin(115200);

  Serial.println();
  Serial.println("================================");
  Serial.println("       PICOCLOUD SERVER");
  Serial.println("================================");

  // Initialize SPI
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  // Initialize SD card
  Serial.println("Initializing SD card...");

  if (!SD.begin(SD_CS, SPI, 20000000)) {

    Serial.println("SD CARD INITIALIZATION FAILED!");

    while (true) {
      delay(1000);
    }
  }

  Serial.println("SD card initialized.");

  // Connect to WiFi
  Serial.println("Connecting to WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");

  Serial.print("ESP32 IP ADDRESS: ");
  Serial.println(WiFi.localIP());

  // Start HTTP server
  server.onNotFound(handleRequest);

  server.begin();

  Serial.println("HTTP server started!");
  Serial.println("PicoCloud is online.");
}

// ===================================
// MAIN LOOP
// ===================================

void loop() {

  server.handleClient();

}
