# Webserver Endpoints

This document describes all HTTP and WebSocket endpoints available on the CrossPoint Reader webserver.

- [Webserver Endpoints](#webserver-endpoints)
  - [Overview](#overview)
  - [HTTP Endpoints](#http-endpoints)
    - [GET `/` - Home Page](#get----home-page)
    - [GET `/files` - File Browser Page](#get-files---file-browser-page)
    - [GET `/api/status` - Device Status](#get-apistatus---device-status)
    - [GET `/api/files` - List Files](#get-apifiles---list-files)
    - [POST `/upload` - Upload File](#post-upload---upload-file)
    - [POST `/mkdir` - Create Folder](#post-mkdir---create-folder)
    - [POST `/delete` - Delete File or Folder](#post-delete---delete-file-or-folder)
    - [GET `/weread` - WeRead API Key Page (Chinese build only)](#get-weread---weread-api-key-page-chinese-build-only)
    - [POST `/api/weread-key` - Save WeRead API Key (Chinese build only)](#post-apiweread-key---save-weread-api-key-chinese-build-only)
  - [WebSocket Endpoint](#websocket-endpoint)
    - [Port 81 - Fast Binary Upload](#port-81---fast-binary-upload)
  - [Network Modes](#network-modes)
    - [Station Mode (STA)](#station-mode-sta)
    - [Access Point Mode (AP)](#access-point-mode-ap)
  - [Notes](#notes)


## Overview

The CrossPoint Reader exposes a webserver for file management and device monitoring:

- **HTTP Server**: Port 80
- **WebSocket Server**: Port 81 (for fast binary uploads)

---

## HTTP Endpoints

### GET `/` - Home Page

Serves the home page HTML interface.

**Request:**
```bash
curl http://crosspoint.local/
```

**Response:** HTML page (200 OK)

---

### GET `/files` - File Browser Page

Serves the file browser HTML interface.

**Request:**
```bash
curl http://crosspoint.local/files
```

**Response:** HTML page (200 OK)

---

### GET `/api/status` - Device Status

Returns JSON with device status information.

**Request:**
```bash
curl http://crosspoint.local/api/status
```

**Response (200 OK):**
```json
{
  "version": "1.0.0",
  "ip": "192.168.1.100",
  "mode": "STA",
  "rssi": -45,
  "freeHeap": 123456,
  "uptime": 3600
}
```

| Field      | Type   | Description                                               |
| ---------- | ------ | --------------------------------------------------------- |
| `version`  | string | CrossPoint firmware version                               |
| `ip`       | string | Device IP address                                         |
| `mode`     | string | `"STA"` (connected to WiFi) or `"AP"` (access point mode) |
| `rssi`     | number | WiFi signal strength in dBm (0 in AP mode)                |
| `freeHeap` | number | Free heap memory in bytes                                 |
| `uptime`   | number | Seconds since device boot                                 |

---

### GET `/api/files` - List Files

Returns a JSON array of files and folders in the specified directory.

**Request:**
```bash
# List root directory
curl http://crosspoint.local/api/files

# List specific directory
curl "http://crosspoint.local/api/files?path=/Books"
```

**Query Parameters:**

| Parameter | Required | Default | Description            |
| --------- | -------- | ------- | ---------------------- |
| `path`    | No       | `/`     | Directory path to list |

**Response (200 OK):**
```json
[
  {"name": "MyBook.epub", "size": 1234567, "isDirectory": false, "isEpub": true},
  {"name": "Notes", "size": 0, "isDirectory": true, "isEpub": false},
  {"name": "document.pdf", "size": 54321, "isDirectory": false, "isEpub": false}
]
```

| Field         | Type    | Description                              |
| ------------- | ------- | ---------------------------------------- |
| `name`        | string  | File or folder name                      |
| `size`        | number  | Size in bytes (0 for directories)        |
| `isDirectory` | boolean | `true` if the item is a folder           |
| `isEpub`      | boolean | `true` if the file has `.epub` extension |

**Notes:**
- Hidden files (starting with `.`) are automatically filtered out
- System folders (`System Volume Information`, `XTCache`) are hidden

---

### POST `/upload` - Upload File

Uploads a file to the SD card via multipart form data.

**Request:**
```bash
# Upload to root directory
curl -X POST -F "file=@mybook.epub" http://crosspoint.local/upload

# Upload to specific directory
curl -X POST -F "file=@mybook.epub" "http://crosspoint.local/upload?path=/Books"
```

**Query Parameters:**

| Parameter | Required | Default | Description                     |
| --------- | -------- | ------- | ------------------------------- |
| `path`    | No       | `/`     | Target directory for the upload |

**Response (200 OK):**
```
File uploaded successfully: mybook.epub
```

**Error Responses:**

| Status | Body                                            | Cause                       |
| ------ | ----------------------------------------------- | --------------------------- |
| 400    | `Failed to create file on SD card`              | Cannot create file          |
| 400    | `Failed to write to SD card - disk may be full` | Write error during upload   |
| 400    | `Failed to write final data to SD card`         | Error flushing final buffer |
| 400    | `Upload aborted`                                | Client aborted the upload   |
| 400    | `Unknown error during upload`                   | Unspecified error           |

**Notes:**
- Existing files with the same name will be overwritten
- Uses a 4KB buffer for efficient SD card writes

---

### POST `/mkdir` - Create Folder

Creates a new folder on the SD card.

**Request:**
```bash
curl -X POST -d "name=NewFolder&path=/" http://crosspoint.local/mkdir
```

**Form Parameters:**

| Parameter | Required | Default | Description                  |
| --------- | -------- | ------- | ---------------------------- |
| `name`    | Yes      | -       | Name of the folder to create |
| `path`    | No       | `/`     | Parent directory path        |

**Response (200 OK):**
```
Folder created: NewFolder
```

**Error Responses:**

| Status | Body                          | Cause                         |
| ------ | ----------------------------- | ----------------------------- |
| 400    | `Missing folder name`         | `name` parameter not provided |
| 400    | `Folder name cannot be empty` | Empty folder name             |
| 400    | `Folder already exists`       | Folder with same name exists  |
| 500    | `Failed to create folder`     | SD card error                 |

---

### POST `/delete` - Delete File or Folder

Deletes one or more files or empty folders from the SD card.

**Request:**
```bash
# Delete a file
curl -X POST -d "path=/Books/mybook.epub" http://crosspoint.local/delete

# Delete an empty folder
curl -X POST -d "path=/OldFolder" http://crosspoint.local/delete

# Delete multiple items
curl -X POST -d 'paths=["/Books/old.epub","/OldFolder"]' http://crosspoint.local/delete
```

**Form Parameters:**

| Parameter | Required | Default | Description |
| --------- | -------- | ------- | ----------- |
| `path`    | Yes, unless `paths` is provided | - | Path to one item to delete |
| `paths`   | Yes, unless `path` is provided | - | JSON array of paths to delete |

**Response (200 OK):**
```text
All items deleted successfully
```

**Error Responses:**

| Status | Body                                        | Cause                              |
| ------ | ------------------------------------------- | ---------------------------------- |
| 400    | `Missing "path" or "paths" argument`        | Neither parameter was provided     |
| 400    | `Provide either 'path' or 'paths', not both` | Both delete parameters were sent   |
| 400    | `Invalid paths format`                      | `paths` was not valid JSON         |
| 400    | `No paths provided`                         | `paths` was an empty JSON array    |
| 500    | `Failed to delete some items: ...`          | One or more paths could not be deleted |

**Protected Items:**
- Files/folders starting with `.`
- `System Volume Information`
- `XTCache`

---

### GET `/weread` - WeRead API Key Page (Chinese build only)

Serves the standalone form used to bind a WeRead Companion API key from a phone browser. Available **only in the `gh_release_cn` build** (the same build that ships the WeRead Companion feature).

This endpoint is intended to be reached via the QR code displayed on the device after entering *微信读书 → API Key → 通过浏览器提交*. The web server registers no link to it from `/`, and `/` registers no link back: the two entry points (file transfer vs WeRead key setup) are kept visually separate.

**Request:**
```bash
curl http://crosspoint.local/weread
```

**Response:** HTML page (200 OK) — a single paste form for `wrk-…` keys, plus a hint linking to <https://weread.qq.com/r/weread-skills>.

---

### POST `/api/weread-key` - Save WeRead API Key (Chinese build only)

Validates and persists a WeRead Companion API key. Used by the `/weread` page's save button. Available **only in the `gh_release_cn` build**.

**Request:**
```bash
curl -X POST -H 'Content-Type: application/json' \
  -d '{"key":"wrk-xxxxxxxxxxxxxxxxxxxxxxxxxxxx"}' \
  http://crosspoint.local/api/weread-key
```

**Body (JSON):**

| Field | Type   | Required | Description                              |
| ----- | ------ | -------- | ---------------------------------------- |
| `key` | string | Yes      | API key, must start with `wrk-`, 8-256 chars |

**Response (200 OK):**
```json
{"ok": true}
```

The device persists the key (XOR-obfuscated with the eFuse MAC) to `/.crosspoint/weread_apikey.txt` on the SD card and updates an in-memory cache so subsequent WeRead requests pick it up without a reboot.

**Error Responses:**

| Status | Body                                                  | Cause                                  |
| ------ | ----------------------------------------------------- | -------------------------------------- |
| 400    | `{"ok":false,"error":"请求不含 JSON 体"}`              | Missing request body                   |
| 400    | `{"ok":false,"error":"JSON 解析失败: …"}`              | Malformed JSON                         |
| 400    | `{"ok":false,"error":"Key 必须以 wrk- 开头,长度 8-256"}` | Failed `WeReadKeyStore::isWellFormed` |
| 500    | `{"ok":false,"error":"保存失败"}`                       | SD card write error                    |

Error strings are intentionally Chinese to match the locale of the only build that exposes this endpoint.

---

## WebSocket Endpoint

### Port 81 - Fast Binary Upload

A WebSocket endpoint for high-speed binary file uploads. More efficient than HTTP multipart for large files.

**Connection:**
```
ws://crosspoint.local:81/
```

**Protocol:**

1. **Client** sends TEXT message: `START:<filename>:<size>:<path>`
2. **Server** responds with TEXT: `READY`
3. **Client** sends BINARY messages with file data chunks
4. **Server** sends TEXT progress updates: `PROGRESS:<received>:<total>`
5. **Server** sends TEXT when complete: `DONE` or `ERROR:<message>`

**Example Session:**

```
Client -> "START:mybook.epub:1234567:/Books"
Server -> "READY"
Client -> [binary chunk 1]
Client -> [binary chunk 2]
Server -> "PROGRESS:65536:1234567"
Client -> [binary chunk 3]
...
Server -> "PROGRESS:1234567:1234567"
Server -> "DONE"
```

**Error Messages:**

| Message                           | Cause                              |
| --------------------------------- | ---------------------------------- |
| `ERROR:Failed to create file`     | Cannot create file on SD card      |
| `ERROR:Invalid START format`      | Malformed START message            |
| `ERROR:No upload in progress`     | Binary data received without START |
| `ERROR:Write failed - disk full?` | SD card write error                |

**Example with `websocat`:**
```bash
# Interactive session
websocat ws://crosspoint.local:81

# Then type:
START:mybook.epub:1234567:/Books
# Wait for READY, then send binary data
```

**Notes:**
- Progress updates are sent every 64KB or at completion
- Disconnection during upload will delete the incomplete file
- Existing files with the same name will be overwritten

---

## Network Modes

The device can operate in two network modes:

### Station Mode (STA)
- Device connects to an existing WiFi network
- IP address assigned by router/DHCP
- `mode` field in `/api/status` returns `"STA"`
- `rssi` field shows signal strength

### Access Point Mode (AP)
- Device creates its own WiFi hotspot
- Default IP is typically `192.168.4.1`
- `mode` field in `/api/status` returns `"AP"`
- `rssi` field returns `0`

---

## Notes

- These examples use `crosspoint.local`. If your network does not support mDNS or the address does not resolve, replace it with the specific **IP Address** displayed on your device screen (e.g., `http://192.168.1.102/`).
- All paths on the SD card start with `/`
- Trailing slashes are automatically stripped (except for root `/`)
- The webserver uses chunked transfer encoding for file listings
