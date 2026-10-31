// Copy this file to "secrets.h" (same folder) and fill in your own values.
// "secrets.h" is listed in .gitignore. NEVER commit it, and never paste it into a spec or chat.
//
// Avoid double quotes and backslashes inside the strings below (they go into JSON as-is).
#pragma once

#define WIFI_SSID              "your-wifi-name"
#define WIFI_PASS              "your-wifi-password"

// Firebase Realtime Database host, WITHOUT "https://" and without a trailing slash.
// Looks like  your-project-default-rtdb.firebaseio.com
//         or  your-project-default-rtdb.us-central1.firebasedatabase.app
#define FIREBASE_DB_HOST       "your-project-default-rtdb.firebaseio.com"

// Firebase project settings > General > "Web API Key" (this identifies the project, it is not a secret password).
#define FIREBASE_API_KEY       "your-web-api-key"

// The one Firebase Authentication (Email/Password) user the ESP32 signs in as.
#define FIREBASE_USER_EMAIL    "esp32@example.com"
#define FIREBASE_USER_PASSWORD "a-long-random-password"
