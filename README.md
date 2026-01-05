# Custom HTTP/HTTPS Proxy Server (C/Winsock)

## Overview
A multi-threaded network proxy server capable of handling standard HTTP forwarding and HTTPS tunneling (via CONNECT). It includes features for domain filtering and activity logging.

## Directory Structure
* `src/`      - Source code and Makefile.
* `include/`  - Header files.
* `config/`   - Configuration files (blocked domains).
* `tests/`    - Automated test scripts.
* `docs/`     - Design documentation.
* `logs/`     - Generated log files (created at runtime).

## Requirements
* Windows OS (Uses Winsock2)
* GCC Compiler (MinGW recommended)

## Build Instructions

1. Open a terminal (PowerShell or CMD).
2. Navigate to the `src` directory:
   ``cd src``
3. Compile the project using Make:
   `mingw32-make` OR `make`
   
   (OR manually using GCC):
   `gcc main.c proxy.c parser.c logger.c -o proxy_server -lws2_32 -I../include`

## How to Run

1. Start the server from the `src` directory:
   `.\proxy_server.exe`

   Output: "Modular Proxy Server Active on Port 8888"

2. The server is now listening. Keep this window open.

## Configuration
* **Port:** Default is 8888 (Change in `include/proxy.h`).
* **Blocking:** Add domains to `config/blocked.txt`. 
    Example content:
    blocked.com
    badsite.org

## Testing & Demonstration

To verify functionality, open a separate terminal and use `curl`:

1. **Test Standard HTTP (Forwarding):**
   `curl.exe -v -x http://localhost:8888 http://example.com`

2. **Test HTTPS (Tunneling):**
   `curl.exe -v -x http://localhost:8888 https://www.google.com`

3. **Test Filtering (Blocking):**
   `curl.exe -v -x http://localhost:8888 http://blocked.com`
   (Should return "403 Forbidden")

4. **Automated Test Suite:**
   Navigate to the `tests` folder and run the batch script:
   `cd ../tests`
   `.\run_tests.bat`

## Logging
Check `src/logs/proxy.log` to see a record of all requests:

[2025-12-23 15:21:57] Client: 127.0.0.1 | Request: example.com | Status: 200

[2025-12-23 15:21:58] Client: 127.0.0.1 | Request: www.google.com | Status: 200

[2025-12-23 15:21:59] Client: 127.0.0.1 | Request: blocked.com | Status: 403

[2025-12-23 15:54:14] Client: 127.0.0.1 | Request: example.com | Status: 200

[2025-12-23 15:54:15] Client: 127.0.0.1 | Request: www.google.com | Status: 200

[2025-12-23 15:54:16] Client: 127.0.0.1 | Request: blocked.com | Status: 403
