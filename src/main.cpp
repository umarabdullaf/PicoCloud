
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

    filename.toLowerCase();

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
// SD CARD DIRECTORY TREE
// ======================================

void printDirectoryTree(File dir, String indent = "") {

    while (true) {

        File entry = dir.openNextFile();

        if (!entry) {
            break;
        }

        Serial.print(indent);

        if (entry.isDirectory()) {

            Serial.print("|-- ");
            Serial.print(entry.name());
            Serial.println("/");

            printDirectoryTree(entry, indent + "|   ");

        } else {

            Serial.print("|-- ");
            Serial.print(entry.name());

            Serial.print(" (");
            Serial.print(entry.size());
            Serial.println(" bytes)");
        }

        entry.close();
    }
}

// ======================================
// VALIDATE SD CARD STRUCTURE
// ======================================

void checkSDStructure() {

    Serial.println("\n========== SD CARD STRUCTURE ==========");

    File root = SD.open("/");

    if (!root || !root.isDirectory()) {

        Serial.println("ERROR: Cannot open SD root!");
        return;
    }

    Serial.println("SD CARD/");
    printDirectoryTree(root);

    root.close();

    Serial.println("\n========== STRUCTURE VALIDATION ==========");

    const char* requiredPaths[] = {

        "/websites",
        "/websites/site1",
        "/websites/site1/index.html",
        "/websites/site2",
        "/websites/site2/index.html",
        "/storage",
        "/storage/Documents",
        "/storage/Files",
        "/config"
    };

    for (const char* path : requiredPaths) {

        Serial.print(path);
        Serial.print(" : ");

        if (SD.exists(path)) {
            Serial.println("FOUND");
        } else {
            Serial.println("MISSING");
        }
    }

    Serial.println("========================================\n");
}

// ======================================
// SERVE WEBSITE FILES
// ======================================

void handleWebsite() {

    String uri = server.uri();

    String basePath;
    String relativePath;

    // Select website

    if (uri == "/" ||
        uri == "/site1" ||
        uri.startsWith("/site1/")) {

        basePath = "/websites/site1";

        relativePath = uri.substring(6);

    }

    else if (uri == "/site2" ||
             uri.startsWith("/site2/")) {

        basePath = "/websites/site2";

        relativePath = uri.substring(6);

    }

    else {

        server.send(404, "text/plain", "Website not found");
        return;
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

    Serial.println("\n========== HTTP REQUEST ==========");

    Serial.print("URL requested: ");
    Serial.println(uri);

    Serial.print("SD file requested: ");
    Serial.println(filePath);

    // Check whether file exists

    if (!SD.exists(filePath)) {

        Serial.println("ERROR: FILE NOT FOUND!");

        server.send(404, "text/plain", "File not found on SD card");

        return;
    }

    // Open file

    File file = SD.open(filePath, FILE_READ);

    if (!file || file.isDirectory()) {

        Serial.println("ERROR: Cannot open requested file!");

        server.send(404, "text/plain", "Invalid file");

        return;
    }

    Serial.print("File size: ");
    Serial.println(file.size());

    String contentType = getContentType(filePath);

    Serial.print("Content type: ");
    Serial.println(contentType);

    server.streamFile(file, contentType);

    file.close();

    Serial.println("File served successfully!");
    Serial.println("==================================");
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
    Serial.println("       PICOCLOUD v0.3");
    Serial.println("==============================");

    // Initialize SPI

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

    // Initialize SD card

    Serial.println("\nInitializing SD card...");

    if (!SD.begin(SD_CS, SPI, 4000000)) {

        Serial.println("SD CARD INITIALIZATION FAILED!");

        return;
    }

    Serial.println("SD card initialized successfully!");

    // Display SD card size

    Serial.print("SD Card Capacity: ");

    Serial.print(SD.cardSize() / (1024 * 1024));

    Serial.println(" MB");

    // Display directory structure

    checkSDStructure();

    // Connect WiFi

    connectWiFi();

    // Website routing

    server.on("/", HTTP_GET, handleWebsite);

    server.onNotFound(handleWebsite);

    // Start HTTP server

    server.begin();

    Serial.println("\nHTTP server started!");

    Serial.println("Website 1: /site1/");
    Serial.println("Website 2: /site2/");

    Serial.println("\nPicoCloud is ready!");
}

// ======================================
// MAIN LOOP
// ======================================

void loop() {

    server.handleClient();
}
