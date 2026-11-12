
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>

// ======================================
// WIFI CONFIGURATION
// ======================================

// Keep your existing v0.4 credentials here.
const char* WIFI_SSID = "LUTTAPPI";
const char* WIFI_PASSWORD = "umarabdullaf";

// ======================================
// SD CARD CONFIGURATION
// ======================================

#define SD_CS   5
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23

const char* WEBSITE_ROOT = "/websites";
const char* STORAGE_ROOT = "/storage";

// ======================================
// WEB SERVER
// ======================================

WebServer server(80);

// ======================================
// UPLOAD STATE
// ======================================

File uploadFile;

String uploadPath = "";
String uploadError = "";

bool uploadFailed = false;
bool uploadSeen = false;
bool uploadCreatedFile = false;

int uploadErrorCode = 400;

// ======================================
// STORAGE WEB APPLICATION
// ======================================

const char STORAGE_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">

<head>
    <meta charset="UTF-8">

    <meta name="viewport"
          content="width=device-width, initial-scale=1">

    <title>PicoCloud Storage</title>

    <style>

        * {
            box-sizing: border-box;
        }

        body {
            margin: 0;
            background: #f5f6fa;
            color: #20232a;
            font-family: Arial, sans-serif;
        }

        .topbar {
            background: #172033;
            color: white;
            padding: 17px 22px;
            display: flex;
            align-items: center;
            justify-content: space-between;
        }

        .brand {
            font-size: 20px;
            font-weight: bold;
        }

        .topbar a {
            color: white;
            text-decoration: none;
            font-size: 14px;
        }

        main {
            max-width: 950px;
            margin: 35px auto;
            padding: 0 18px 100px;
        }

        .heading {
            display: flex;
            justify-content: space-between;
            align-items: center;
            gap: 10px;
        }

        h1 {
            font-size: 27px;
            margin: 0 0 8px;
        }

        .subtitle {
            color: #737987;
            font-size: 14px;
        }

        .breadcrumbs {
            margin: 25px 0 18px;
            display: flex;
            flex-wrap: wrap;
            align-items: center;
            gap: 7px;
            font-size: 14px;
        }

        .breadcrumbs button {
            border: none;
            background: none;
            color: #2563eb;
            cursor: pointer;
            padding: 3px;
            font-size: 14px;
        }

        .breadcrumbs button.active {
            color: #20232a;
            font-weight: bold;
            cursor: default;
        }

        .separator {
            color: #a0a5af;
        }

        .notice {
            display: none;
            padding: 12px 15px;
            border-radius: 8px;
            margin-bottom: 15px;
            font-size: 14px;
            overflow-wrap: anywhere;
        }

        .notice.info {
            display: block;
            background: #e8efff;
            color: #244c9a;
        }

        .notice.success {
            display: block;
            background: #e5f7eb;
            color: #176b36;
        }

        .notice.error {
            display: block;
            background: #ffebeb;
            color: #a32626;
        }

        .file-panel {
            background: white;
            border: 1px solid #e5e7eb;
            border-radius: 12px;
            overflow: hidden;
        }

        .panel-heading {
            padding: 15px 18px;
            border-bottom: 1px solid #eceef2;
            color: #737987;
            font-size: 13px;
            display: flex;
            justify-content: space-between;
        }

        .file-row {
            display: flex;
            align-items: center;
            min-height: 62px;
            padding: 10px 18px;
            border-bottom: 1px solid #f0f1f4;
            gap: 13px;
        }

        .file-row:last-child {
            border-bottom: none;
        }

        .file-icon {
            font-size: 25px;
            width: 34px;
            text-align: center;
            flex-shrink: 0;
        }

        .file-main {
            min-width: 0;
            flex: 1;
        }

        .file-action {
            border: none;
            padding: 0;
            background: none;
            color: #20232a;
            font-size: 15px;
            text-align: left;
            overflow-wrap: anywhere;
            cursor: pointer;
            text-decoration: none;
        }

        .file-action:hover {
            color: #2563eb;
        }

        .file-meta {
            color: #8b909a;
            font-size: 12px;
            margin-top: 5px;
        }

        .empty {
            padding: 45px 20px;
            text-align: center;
            color: #858b96;
            font-size: 14px;
        }

        .warning {
            margin-top: 22px;
            color: #92702b;
            font-size: 12px;
        }

        .upload-button {
            position: fixed;
            right: 27px;
            bottom: 27px;
            width: 60px;
            height: 60px;
            border: none;
            border-radius: 50%;
            background: #2563eb;
            color: white;
            font-size: 34px;
            line-height: 1;
            cursor: pointer;
            box-shadow: 0 5px 18px rgba(0,0,0,0.22);
        }

        .upload-button:hover {
            background: #174fc2;
        }

        #fileInput {
            display: none;
        }

        @media (max-width: 600px) {

            main {
                margin-top: 24px;
            }

            .heading h1 {
                font-size: 23px;
            }

            .file-row {
                padding: 12px;
            }

            .upload-button {
                right: 20px;
                bottom: 20px;
            }
        }

    </style>
</head>

<body>

    <header class="topbar">

        <div class="brand">
            PicoCloud
        </div>

        <a href="/">
            Home
        </a>

    </header>

    <main>

        <div class="heading">

            <div>
                <h1>Storage</h1>

                <div class="subtitle">
                    Browse and manage your stored files
                </div>
            </div>

            <div id="itemCount"></div>

        </div>

        <nav class="breadcrumbs" id="breadcrumbs"></nav>

        <div id="notice" class="notice"></div>

        <section class="file-panel">

            <div class="panel-heading">

                <span>Name</span>

                <span>Size</span>

            </div>

            <div id="fileList">

                <div class="empty">
                    Loading storage...
                </div>

            </div>

        </section>

        <div class="warning">
            Prototype storage: authentication is not enabled yet.
        </div>

    </main>

    <input
        type="file"
        id="fileInput"
        multiple
    >

    <button
        class="upload-button"
        id="uploadButton"
        title="Upload files"
        aria-label="Upload files"
    >
        +
    </button>

<script>

    const fileList = document.getElementById("fileList");
    const breadcrumbs = document.getElementById("breadcrumbs");
    const itemCount = document.getElementById("itemCount");
    const notice = document.getElementById("notice");
    const fileInput = document.getElementById("fileInput");

    let currentPath = pathFromLocation();

    // ==================================
    // URL AND PATH HANDLING
    // ==================================

    function pathFromLocation() {

        let raw = window.location.pathname.substring(
            "/storage".length
        );

        try {
            return raw
                .split("/")
                .filter(Boolean)
                .map(part => decodeURIComponent(part))
                .join("/");
        }
        catch (error) {
            return "";
        }
    }

    function storageUrl(path) {

        if (!path) {
            return "/storage/";
        }

        const encoded = path
            .split("/")
            .map(part => encodeURIComponent(part))
            .join("/");

        return "/storage/" + encoded + "/";
    }

    function joinPath(parent, child) {

        return parent
            ? parent + "/" + child
            : child;
    }

    // ==================================
    // NOTIFICATION
    // ==================================

    function showNotice(message, type) {

        notice.textContent = message;

        notice.className = "notice " + type;
    }

    function clearNotice() {

        notice.textContent = "";
        notice.className = "notice";
    }

    // ==================================
    // FILE SIZE FORMATTING
    // ==================================

    function formatSize(bytes) {

        if (bytes < 1024) {
            return bytes + " B";
        }

        if (bytes < 1024 * 1024) {
            return (bytes / 1024).toFixed(1) + " KB";
        }

        if (bytes < 1024 * 1024 * 1024) {
            return (bytes / (1024 * 1024)).toFixed(1) + " MB";
        }

        return (
            bytes / (1024 * 1024 * 1024)
        ).toFixed(2) + " GB";
    }

    function fileIcon(filename) {

        const extension = filename
            .split(".")
            .pop()
            .toLowerCase();

        if (["jpg", "jpeg", "png", "gif", "webp", "svg"].includes(extension)) {
            return "🖼️";
        }

        if (extension === "pdf") {
            return "📕";
        }

        if (["doc", "docx", "txt", "odt"].includes(extension)) {
            return "📄";
        }

        if (["zip", "rar", "7z", "tar"].includes(extension)) {
            return "📦";
        }

        if (["mp3", "wav", "ogg"].includes(extension)) {
            return "🎵";
        }

        if (["mp4", "mkv", "avi"].includes(extension)) {
            return "🎬";
        }

        return "📄";
    }

    // ==================================
    // BREADCRUMB NAVIGATION
    // ==================================

    function renderBreadcrumbs() {

        breadcrumbs.textContent = "";

        const parts = currentPath
            ? currentPath.split("/")
            : [];

        const crumbs = [
            {
                name: "Storage",
                path: ""
            }
        ];

        let accumulated = "";

        for (const part of parts) {

            accumulated = joinPath(accumulated, part);

            crumbs.push({
                name: part,
                path: accumulated
            });
        }

        crumbs.forEach((crumb, index) => {

            if (index > 0) {

                const separator = document.createElement("span");

                separator.className = "separator";
                separator.textContent = " / ";

                breadcrumbs.appendChild(separator);
            }

            const button = document.createElement("button");

            button.textContent = crumb.name;

            if (index === crumbs.length - 1) {

                button.className = "active";

            } else {

                button.addEventListener("click", () => {

                    navigateTo(crumb.path);

                });
            }

            breadcrumbs.appendChild(button);

        });
    }

    // ==================================
    // NAVIGATION
    // ==================================

    function navigateTo(path) {

        currentPath = path;

        window.history.pushState(
            {},
            "",
            storageUrl(currentPath)
        );

        loadDirectory();
    }

    window.addEventListener("popstate", () => {

        currentPath = pathFromLocation();

        loadDirectory();

    });

    // ==================================
    // RENDER DIRECTORY CONTENTS
    // ==================================

    function renderItems(items) {

        fileList.textContent = "";

        items.sort((a, b) => {

            if (a.type !== b.type) {
                return a.type === "directory" ? -1 : 1;
            }

            return a.name.localeCompare(b.name);
        });

        itemCount.textContent = items.length + " items";

        if (items.length === 0) {

            const empty = document.createElement("div");

            empty.className = "empty";
            empty.textContent = "This folder is empty.";

            fileList.appendChild(empty);

            return;
        }

        items.forEach(item => {

            const row = document.createElement("div");

            row.className = "file-row";

            const icon = document.createElement("div");

            icon.className = "file-icon";

            icon.textContent =
                item.type === "directory"
                    ? "📁"
                    : fileIcon(item.name);

            const main = document.createElement("div");

            main.className = "file-main";

            let action;

            if (item.type === "directory") {

                action = document.createElement("button");

                action.className = "file-action";
                action.textContent = item.name;

                action.addEventListener("click", () => {

                    navigateTo(
                        joinPath(currentPath, item.name)
                    );

                });

            } else {

                action = document.createElement("a");

                action.className = "file-action";
                action.textContent = item.name;

                const fullPath = joinPath(
                    currentPath,
                    item.name
                );

                action.href =
                    "/api/storage/download?path=" +
                    encodeURIComponent(fullPath);

                action.setAttribute("download", "");

            }

            const meta = document.createElement("div");

            meta.className = "file-meta";

            meta.textContent =
                item.type === "directory"
                    ? "Folder"
                    : formatSize(item.size);

            main.appendChild(action);
            main.appendChild(meta);

            const size = document.createElement("span");

            size.style.color = "#858b96";
            size.style.fontSize = "12px";

            size.textContent =
                item.type === "directory"
                    ? "—"
                    : formatSize(item.size);

            row.appendChild(icon);
            row.appendChild(main);
            row.appendChild(size);

            fileList.appendChild(row);

        });
    }

    // ==================================
    // LOAD DIRECTORY FROM ESP32
    // ==================================

    async function loadDirectory() {

        clearNotice();

        renderBreadcrumbs();

        fileList.innerHTML =
            '<div class="empty">Loading...</div>';

        try {

            const response = await fetch(
                "/api/storage/list?path=" +
                encodeURIComponent(currentPath),
                {
                    cache: "no-store"
                }
            );

            const data = await response.json();

            if (!response.ok) {

                throw new Error(
                    data.error || "Unable to read directory"
                );
            }

            renderItems(data.items || []);

        }
        catch (error) {

            itemCount.textContent = "";

            fileList.innerHTML =
                '<div class="empty"></div>';

            fileList.firstChild.textContent =
                error.message;

            showNotice(error.message, "error");

            const rootButton = document.createElement("button");

            rootButton.textContent = "Return to Storage";

            rootButton.style.margin = "15px";
            rootButton.style.padding = "10px";
            rootButton.style.cursor = "pointer";

            rootButton.addEventListener("click", () => {

                navigateTo("");

            });

            fileList.appendChild(rootButton);

        }
    }

    // ==================================
    // FILE UPLOAD
    // ==================================

    function uploadOneFile(file) {

        return new Promise((resolve, reject) => {

            const formData = new FormData();

            formData.append(
                "file",
                file,
                file.name
            );

            const xhr = new XMLHttpRequest();

            const url =
                "/api/storage/upload?path=" +
                encodeURIComponent(currentPath);

            xhr.open("POST", url);

            xhr.upload.addEventListener("progress", event => {

                if (event.lengthComputable) {

                    const percentage = Math.round(
                        (event.loaded / event.total) * 100
                    );

                    showNotice(
                        "Uploading " + file.name +
                        " — " + percentage + "%",
                        "info"
                    );
                }

            });

            xhr.onload = () => {

                if (xhr.status >= 200 && xhr.status < 300) {

                    resolve();

                } else {

                    let message = xhr.responseText;

                    try {

                        message = JSON.parse(
                            xhr.responseText
                        ).error || message;

                    } catch (error) {}

                    reject(new Error(message));
                }

            };

            xhr.onerror = () => {

                reject(new Error("Network error during upload"));

            };

            xhr.send(formData);

        });
    }

    async function uploadFiles(files) {

        const failures = [];

        for (let i = 0; i < files.length; i++) {

            const file = files[i];

            showNotice(
                "Uploading " + file.name +
                " (" + (i + 1) + "/" + files.length + ")",
                "info"
            );

            try {

                await uploadOneFile(file);

            }
            catch (error) {

                failures.push(
                    file.name + ": " + error.message
                );

            }

        }

        await loadDirectory();

        if (failures.length > 0) {

            showNotice(
                "Upload errors: " + failures.join(" | "),
                "error"
            );

        } else {

            showNotice(
                "Upload completed successfully.",
                "success"
            );

        }
    }

    // ==================================
    // PLUS BUTTON
    // ==================================

    document.getElementById("uploadButton")
        .addEventListener("click", () => {

            fileInput.click();

        });

    fileInput.addEventListener("change", async () => {

        const files = Array.from(fileInput.files);

        if (files.length > 0) {

            await uploadFiles(files);

        }

        fileInput.value = "";

    });

    // ==================================
    // INITIALIZE APPLICATION
    // ==================================

    loadDirectory();

</script>

</body>
</html>
)HTML";

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
    if (filename.endsWith(".pdf"))  return "application/pdf";
    if (filename.endsWith(".webp")) return "image/webp";
    if (filename.endsWith(".mp4"))  return "video/mp4";
    if (filename.endsWith(".mp3"))  return "audio/mpeg";

    return "application/octet-stream";
}

// ======================================
// JSON ESCAPING
// ======================================

String jsonEscape(const String& input) {

    String output = "";

    const char* hex = "0123456789abcdef";

    for (size_t i = 0; i < input.length(); i++) {

        uint8_t c = (uint8_t)input[i];

        switch (c) {

            case '"':
                output += "\\\"";
                break;

            case '\\':
                output += "\\\\";
                break;

            case '\b':
                output += "\\b";
                break;

            case '\f':
                output += "\\f";
                break;

            case '\n':
                output += "\\n";
                break;

            case '\r':
                output += "\\r";
                break;

            case '\t':
                output += "\\t";
                break;

            default:

                if (c < 0x20) {

                    output += "\\u00";
                    output += hex[(c >> 4) & 0x0F];
                    output += hex[c & 0x0F];

                } else {

                    output += (char)c;
                }

                break;
        }
    }

    return output;
}

// ======================================
// DIRECTORY NAME EXTRACTION
// ======================================

String getEntryName(String name) {

    while (name.endsWith("/") && name.length() > 1) {
        name.remove(name.length() - 1);
    }

    int lastSlash = name.lastIndexOf('/');

    if (lastSlash >= 0) {
        name = name.substring(lastSlash + 1);
    }

    return name;
}

// ======================================
// SAFE STORAGE PATH VALIDATION
// ======================================

// All storage API paths are relative to /storage.
// Absolute paths and directory traversal are rejected.

bool normalizeRelativePath(
    String input,
    String& normalized
) {

    normalized = "";

    if (input.length() > 240) {
        return false;
    }

    if (input.startsWith("/") ||
        input.indexOf('\\') >= 0) {

        return false;
    }

    int start = 0;

    while (start < input.length()) {

        int slash = input.indexOf('/', start);

        int end = (slash == -1)
                    ? input.length()
                    : slash;

        String segment = input.substring(start, end);

        if (segment.length() > 0) {

            if (segment == "." || segment == "..") {
                return false;
            }

            for (size_t i = 0; i < segment.length(); i++) {

                uint8_t c = (uint8_t)segment[i];

                if (c < 0x20 || c == 0x7F || c == ':') {
                    return false;
                }
            }

            if (normalized.length() > 0) {
                normalized += "/";
            }

            normalized += segment;
        }

        if (slash == -1) {
            break;
        }

        start = slash + 1;
    }

    return true;
}

// ======================================
// RESOLVE STORAGE DIRECTORY
// ======================================

bool resolveStorageDirectory(
    const String& requestedPath,
    String& normalizedPath,
    String& fullPath
) {

    if (!normalizeRelativePath(
            requestedPath,
            normalizedPath)) {

        return false;
    }

    fullPath = STORAGE_ROOT;

    if (normalizedPath.length() > 0) {

        fullPath += "/";
        fullPath += normalizedPath;
    }

    File dir = SD.open(fullPath);

    bool valid = dir && dir.isDirectory();

    if (dir) {
        dir.close();
    }

    return valid;
}

// ======================================
// STORAGE API ERROR RESPONSE
// ======================================

void sendStorageError(
    int statusCode,
    const String& message
) {

    String json = "{\"error\":\"";

    json += jsonEscape(message);

    json += "\"}";

    server.send(
        statusCode,
        "application/json",
        json
    );
}

// ======================================
// STORAGE DIRECTORY LISTING API
// ======================================

void handleStorageList() {

    String requestedPath = server.arg("path");

    String normalizedPath;
    String fullPath;

    if (!normalizeRelativePath(
            requestedPath,
            normalizedPath)) {

        sendStorageError(400, "Invalid storage path");
        return;
    }

    if (!resolveStorageDirectory(
            requestedPath,
            normalizedPath,
            fullPath)) {

        sendStorageError(404, "Directory not found");
        return;
    }

    File dir = SD.open(fullPath);

    if (!dir || !dir.isDirectory()) {

        if (dir) dir.close();

        sendStorageError(404, "Directory not found");
        return;
    }

    String json = "{\"path\":\"";

    json += jsonEscape(normalizedPath);
    json += "\",\"items\":[";

    bool firstItem = true;

    File entry = dir.openNextFile();

    while (entry) {

        String name = getEntryName(entry.name());

        if (name.length() > 0) {

            if (!firstItem) {
                json += ",";
            }

            firstItem = false;

            bool isDirectory = entry.isDirectory();

            json += "{\"name\":\"";
            json += jsonEscape(name);
            json += "\",\"type\":\"";

            json += isDirectory ? "directory" : "file";

            json += "\",\"size\":";
            json += String(
                isDirectory ? 0UL : (unsigned long)entry.size()
            );

            json += "}";
        }

        entry.close();

        entry = dir.openNextFile();
    }

    dir.close();

    json += "]}";

    server.send(
        200,
        "application/json",
        json
    );
}

// ======================================
// STORAGE FILE DOWNLOAD API
// ======================================

void handleStorageDownload() {

    String requestedPath = server.arg("path");

    String normalizedPath;

    if (!normalizeRelativePath(
            requestedPath,
            normalizedPath)) {

        sendStorageError(400, "Invalid file path");
        return;
    }

    if (normalizedPath.length() == 0) {

        sendStorageError(400, "No file specified");
        return;
    }

    String fullPath = String(STORAGE_ROOT) +
                      "/" +
                      normalizedPath;

    File file = SD.open(fullPath, FILE_READ);

    if (!file) {

        sendStorageError(404, "File not found");
        return;
    }

    if (file.isDirectory()) {

        file.close();

        sendStorageError(400, "Cannot download a directory");
        return;
    }

    String filename = getEntryName(fullPath);

    // Remove characters that can interfere with this header.
    filename.replace("\"", "_");
    filename.replace("\r", "_");
    filename.replace("\n", "_");

    server.sendHeader(
        "Content-Disposition",
        "attachment; filename=\"" + filename + "\""
    );

    server.streamFile(
        file,
        "application/octet-stream"
    );

    file.close();
}

// ======================================
// UPLOAD FILENAME VALIDATION
// ======================================

bool validUploadFilename(const String& filename) {

    if (filename.length() == 0 ||
        filename.length() > 120) {

        return false;
    }

    if (filename == "." ||
        filename == ".." ||
        filename.indexOf('/') >= 0 ||
        filename.indexOf('\\') >= 0 ||
        filename.indexOf(':') >= 0) {

        return false;
    }

    for (size_t i = 0; i < filename.length(); i++) {

        uint8_t c = (uint8_t)filename[i];

        if (c < 0x20 || c == 0x7F) {
            return false;
        }
    }

    return true;
}

// ======================================
// UPLOAD FAILURE HANDLING
// ======================================

void markUploadFailed(
    int statusCode,
    const String& message
) {

    if (!uploadFailed) {

        uploadFailed = true;
        uploadErrorCode = statusCode;
        uploadError = message;
    }

    if (uploadFile) {
        uploadFile.close();
    }

    // Delete only a file created by this upload.
    if (uploadCreatedFile &&
        uploadPath.length() > 0 &&
        SD.exists(uploadPath)) {

        SD.remove(uploadPath);
    }

    uploadCreatedFile = false;
}

// ======================================
// FILE UPLOAD DATA HANDLER
// ======================================

void handleStorageUploadData() {

    HTTPUpload& upload = server.upload();

    // ----------------------------------
    // Upload begins
    // ----------------------------------

    if (upload.status == UPLOAD_FILE_START) {

        uploadSeen = true;

        uploadFailed = false;
        uploadError = "";
        uploadErrorCode = 400;

        uploadPath = "";
        uploadCreatedFile = false;

        if (uploadFile) {
            uploadFile.close();
        }

        // Get the active directory from the request.
        String requestedDirectory = server.arg("path");

        String normalizedDirectory;
        String fullDirectory;

        if (!resolveStorageDirectory(
                requestedDirectory,
                normalizedDirectory,
                fullDirectory)) {

            markUploadFailed(
                404,
                "Target directory does not exist"
            );

            return;
        }

        // Validate the uploaded filename.
        String filename = upload.filename;

        if (!validUploadFilename(filename)) {

            markUploadFailed(
                400,
                "Invalid filename"
            );

            return;
        }

        String destination = fullDirectory;

        destination += "/";
        destination += filename;

        // Never overwrite an existing file.
        if (SD.exists(destination)) {

            markUploadFailed(
                409,
                "A file with this name already exists"
            );

            return;
        }

        uploadFile = SD.open(
            destination,
            FILE_WRITE
        );

        if (!uploadFile) {

            markUploadFailed(
                507,
                "Could not create file on SD card"
            );

            return;
        }

        uploadPath = destination;
        uploadCreatedFile = true;

        Serial.print("Upload started: ");
        Serial.println(destination);
    }

    // ----------------------------------
    // Receive file data in chunks
    // ----------------------------------

    else if (upload.status == UPLOAD_FILE_WRITE) {

        if (!uploadFailed && uploadFile) {

            size_t written = uploadFile.write(
                upload.buf,
                upload.currentSize
            );

            if (written != upload.currentSize) {

                markUploadFailed(
                    507,
                    "SD card write failed or storage is full"
                );

                return;
            }
        }
    }

    // ----------------------------------
    // Upload completed
    // ----------------------------------

    else if (upload.status == UPLOAD_FILE_END) {

        if (uploadFile) {
            uploadFile.close();
        }

        if (!uploadFailed) {

            Serial.print("Upload completed: ");
            Serial.println(uploadPath);
        }
    }

    // ----------------------------------
    // Upload aborted
    // ----------------------------------

    else if (upload.status == UPLOAD_FILE_ABORTED) {

        markUploadFailed(
            400,
            "Upload was cancelled"
        );
    }
}

// ======================================
// UPLOAD REQUEST RESPONSE
// ======================================

void handleStorageUploadRequest() {

    if (!uploadSeen) {

        sendStorageError(
            400,
            "No file was received"
        );

        return;
    }

    if (uploadFailed) {

        sendStorageError(
            uploadErrorCode,
            uploadError
        );

    } else {

        server.send(
            201,
            "application/json",
            "{\"ok\":true,\"message\":\"Upload successful\"}"
        );
    }

    // Reset state for the next upload request.
    uploadSeen = false;
    uploadFailed = false;
    uploadCreatedFile = false;

    uploadPath = "";
    uploadError = "";
}

// ======================================
// STORAGE WEB APPLICATION ROUTE
// ======================================

void handleStoragePage() {

    String uri = server.uri();

    if (uri == "/storage") {

        server.sendHeader(
            "Location",
            "/storage/"
        );

        server.send(
            302,
            "text/plain",
            "Redirecting to storage..."
        );

        return;
    }

    server.send_P(
        200,
        "text/html",
        STORAGE_HTML
    );
}

// ======================================
// WEBSITE MIME TYPE / ROUTING
// ======================================

void handleWebsite() {

    String uri = server.uri();

    String lowerUri = uri;
    lowerUri.toLowerCase();

    // Reject unsafe paths.
    if (uri.indexOf("..") >= 0 ||
        uri.indexOf('\\') >= 0 ||
        lowerUri.indexOf("%2e") >= 0 ||
        lowerUri.indexOf("%2f") >= 0 ||
        lowerUri.indexOf("%5c") >= 0) {

        server.send(
            400,
            "text/plain",
            "Invalid path"
        );

        return;
    }

    // Extract website name.
    String requestedPath = uri.substring(1);

    int slashPosition = requestedPath.indexOf('/');

    String websiteName;

    if (slashPosition == -1) {

        websiteName = requestedPath;

    } else {

        websiteName = requestedPath.substring(
            0,
            slashPosition
        );
    }

    if (websiteName.length() == 0) {

        server.send(
            404,
            "text/plain",
            "Website not found"
        );

        return;
    }

    // Find website directory.
    String basePath = String(WEBSITE_ROOT) +
                      "/" +
                      websiteName;

    File websiteDir = SD.open(basePath);

    if (!websiteDir || !websiteDir.isDirectory()) {

        if (websiteDir) {
            websiteDir.close();
        }

        server.send(
            404,
            "text/plain",
            "Website not found"
        );

        return;
    }

    websiteDir.close();

    // Redirect /website to /website/
    if (slashPosition == -1) {

        server.sendHeader(
            "Location",
            uri + "/"
        );

        server.send(
            302,
            "text/plain",
            "Redirecting..."
        );

        return;
    }

    // Determine file inside website.
    String relativePath = requestedPath.substring(
        slashPosition
    );

    if (relativePath.length() == 0 ||
        relativePath == "/") {

        relativePath = "/index.html";
    }

    String filePath = basePath + relativePath;

    Serial.print("Requested website file: ");
    Serial.println(filePath);

    if (!SD.exists(filePath)) {

        server.send(
            404,
            "text/plain",
            "File not found"
        );

        return;
    }

    File file = SD.open(
        filePath,
        FILE_READ
    );

    if (!file || file.isDirectory()) {

        if (file) {
            file.close();
        }

        server.send(
            404,
            "text/plain",
            "Invalid file"
        );

        return;
    }

    server.streamFile(
        file,
        getContentType(filePath)
    );

    file.close();
}

// ======================================
// WEBSITE DIRECTORY DISCOVERY
// ======================================

void listWebsites() {

    Serial.println();
    Serial.println("Available websites:");

    File root = SD.open(WEBSITE_ROOT);

    if (!root || !root.isDirectory()) {

        Serial.println("No websites directory found.");

        if (root) {
            root.close();
        }

        return;
    }

    int count = 0;

    File entry = root.openNextFile();

    while (entry) {

        if (entry.isDirectory()) {

            count++;

            Serial.print(count);
            Serial.print(". ");

            Serial.println(
                getEntryName(entry.name())
            );
        }

        entry.close();

        entry = root.openNextFile();
    }

    root.close();

    Serial.print("\nTotal websites: ");
    Serial.println(count);

    Serial.println();
}

// ======================================
// ROOT HOMEPAGE
// ======================================

void handleRoot() {

    String html = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport"
          content="width=device-width, initial-scale=1">

    <title>PicoCloud</title>

    <style>
        body {
            font-family: Arial, sans-serif;
            background: #f4f4f4;
            padding: 25px;
        }

        .container {
            max-width: 650px;
            margin: auto;
            background: white;
            padding: 25px;
            border-radius: 10px;
        }

        a {
            display: block;
            padding: 13px;
            margin: 10px 0;
            background: #eeeeee;
            color: #222;
            text-decoration: none;
            border-radius: 5px;
        }

        a:hover {
            background: #dddddd;
        }

        .storage {
            background: #dce9ff;
        }
    </style>
</head>

<body>

<div class="container">

    <h1>PicoCloud</h1>

    <h3>Hosted Websites</h3>
)HTML";

    File root = SD.open(WEBSITE_ROOT);

    if (!root || !root.isDirectory()) {

        html += "<p>No websites available.</p>";

        if (root) {
            root.close();
        }

    } else {

        int count = 0;

        File entry = root.openNextFile();

        while (entry) {

            if (entry.isDirectory()) {

                String name = getEntryName(entry.name());

                html += "<a href=\"/";
                html += name;
                html += "/\">";
                html += name;
                html += "</a>";

                count++;
            }

            entry.close();

            entry = root.openNextFile();
        }

        root.close();

        if (count == 0) {
            html += "<p>No websites available.</p>";
        }
    }

    html += R"HTML(

    <h3>Personal Storage</h3>

    <a class="storage" href="/storage/">
        Open PicoCloud Storage
    </a>

</div>

</body>
</html>
)HTML";

    server.send(
        200,
        "text/html",
        html
    );
}

// ======================================
// WIFI CONNECTION
// ======================================

void connectWiFi() {

    WiFi.mode(WIFI_STA);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    Serial.print("Connecting to WiFi");

    while (WiFi.status() != WL_CONNECTED) {

        delay(500);

        Serial.print(".");
    }

    Serial.println("\nWiFi connected!");

    Serial.print("ESP32 IP Address: ");

    Serial.println(
        WiFi.localIP()
    );
}

// ======================================
// NOT FOUND ROUTER
// ======================================

void handleNotFound() {

    String uri = server.uri();

    // Nested storage URLs load the same web application.
    if (uri == "/storage" ||
        uri.startsWith("/storage/")) {

        if (server.method() == HTTP_GET) {

            handleStoragePage();

        } else {

            server.send(
                405,
                "text/plain",
                "Method not allowed"
            );
        }

        return;
    }

    // All remaining website paths are handled dynamically.
    handleWebsite();
}

// ======================================
// SETUP
// ======================================

void setup() {

    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println("================================");
    Serial.println("         PICOCLOUD v0.5");
    Serial.println("================================");

    // Initialize SPI.
    SPI.begin(
        SD_SCK,
        SD_MISO,
        SD_MOSI,
        SD_CS
    );

    // Initialize SD card.
    Serial.println("Initializing SD card...");

    if (!SD.begin(
            SD_CS,
            SPI,
            4000000)) {

        Serial.println(
            "SD CARD INITIALIZATION FAILED!"
        );

        return;
    }

    Serial.println(
        "SD card initialized successfully!"
    );

    // Connect WiFi.
    connectWiFi();

    // Discover hosted websites.
    listWebsites();

    // ----------------------------------
    // WEBSITE ROUTES
    // ----------------------------------

    server.on(
        "/",
        HTTP_GET,
        handleRoot
    );

    // ----------------------------------
    // STORAGE WEB APP ROUTES
    // ----------------------------------

    server.on(
        "/storage",
        HTTP_GET,
        handleStoragePage
    );

    server.on(
        "/storage/",
        HTTP_GET,
        handleStoragePage
    );

    // ----------------------------------
    // STORAGE API ROUTES
    // ----------------------------------

    server.on(
        "/api/storage/list",
        HTTP_GET,
        handleStorageList
    );

    server.on(
        "/api/storage/download",
        HTTP_GET,
        handleStorageDownload
    );

    server.on(
        "/api/storage/upload",
        HTTP_POST,
        handleStorageUploadRequest,
        handleStorageUploadData
    );

    // ----------------------------------
    // DYNAMIC WEBSITE ROUTING
    // ----------------------------------

    server.onNotFound(
        handleNotFound
    );

    // Start HTTP server.
    server.begin();

    Serial.println();
    Serial.println("HTTP server started!");

    Serial.println();
    Serial.println("Storage interface:");
    Serial.println("/storage/");

    Serial.println();
    Serial.println("Storage API:");
    Serial.println("/api/storage/list");
    Serial.println("/api/storage/download");
    Serial.println("/api/storage/upload");

    Serial.println();
    Serial.println("================================");
}

// ======================================
// MAIN LOOP
// ======================================

void loop() {

    server.handleClient();
}
