
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>

// ======================================
// WIFI CONFIGURATION
// ======================================

const char* WIFI_SSID = "LUTTAPPI";
const char* WIFI_PASSWORD = "umarabdullaf";

// ======================================
// SD CARD CONFIGURATION
// ======================================

#define SD_CS   5
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23

// ======================================
// SERVER
// ======================================

WebServer server(80);

// ======================================
// MIME TYPE DETECTION
// ======================================

String getContentType(String filename) {

    if (filename.endsWith(".html")) return "text/html";
    if (filename.endsWith(".css"))  return "text/css";
    if (filename.endsWith(".js"))   return "application/javascript";
    if (filename.endsWith(".png"))  return "image/png";
    if (filename.endsWith(".jpg"))  return "image/jpeg";
    if (filename.endsWith(".jpeg")) return "image/jpeg";
    if (filename.endsWith(".gif"))  return "image/gif";
    if (filename.endsWith(".svg"))  return "image/svg+xml";
    if (filename.endsWith(".ico"))  return "image/x-icon";
    if (filename.endsWith(".json")) return "application/json";
    if (filename.endsWith(".txt"))  return "text/plain";

    return "application/octet-stream";
}

// ======================================
// SERVE WEBSITE FILES
// ======================================

void handleWebsite() {

    String uri = server.uri();

    // Allow only the two configured websites
    String basePath;

    if (uri == "/" || uri.startsWith("/site1/") ||
        uri == "/site1") {

        basePath = "/websites/site1";
    }
    else if (uri.startsWith("/site2/") || uri == "/site2") {

        basePath = "/websites/site2";
    }
    else {
        server.send(404, "text/plain", "Website not found");
        return;
    }

    // Remove the website prefix from the requested URL
    String relativePath = "";

    if (uri.startsWith("/site1")) {
        relativePath = uri.substring(6);
    }
    else if (uri.startsWith("/site2")) {
        relativePath = uri.substring(6);
    }

    // Default homepage
    if (relativePath.length() == 0 ||
        relativePath == "/") {

        relativePath = "/index.html";
    }

    // Prevent directory traversal
    if (relativePath.indexOf("..") >= 0 ||
        relativePath.indexOf('\\') >= 0) {

        server.send(400, "text/plain", "Invalid path");
        return;
    }

    String filePath = basePath + relativePath;

    Serial.print("Requested file: ");
    Serial.println(filePath);

    if (!SD.exists(filePath)) {

        server.send(404, "text/plain", "File not found");
        return;
    }

    File file = SD.open(filePath, FILE_READ);

    if (!file || file.isDirectory()) {

        server.send(404, "text/plain", "Invalid file");
        return;
    }

    String contentType = getContentType(filePath);

    server.streamFile(file, contentType);

    file.close();
}

// ======================================
// WIFI CONNECTION
// ======================================

void connectWiFi() {

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Connecting to WiFi");

    while (WiFi.status() != WL_CONNECTED) {

        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected!");

    Serial.print("ESP32 IP Address: ");
    Serial.println(WiFi.localIP());
}

// ======================================
// SETUP
// ======================================

void setup() {

    Serial.begin(115200);
    delay(1000);

    Serial.println("\n==============================");
    Serial.println("       PICOCLOUD v0.2");
    Serial.println("==============================");

    // Initialize SPI
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

    // Initialize SD card
    Serial.println("Initializing SD card...");

    if (!SD.begin(SD_CS, SPI, 4000000)) {

        Serial.println("SD CARD INITIALIZATION FAILED!");
        return;
    }

    Serial.println("SD card initialized successfully!");

    // Connect WiFi
    connectWiFi();

    // Website routing
    server.on("/", HTTP_GET, handleWebsite);
    server.onNotFound(handleWebsite);

    // Start server
    server.begin();

    Serial.println("HTTP server started!");

    Serial.println("Website 1: /site1/");
    Serial.println("Website 2: /site2/");
}

// ======================================
// MAIN LOOP
// ======================================

void loop() {

    server.handleClient();
}
