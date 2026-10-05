/*
 * TEMPORARY TEST WI-FI
 * Copy this file to wifi_test_credentials.h (same folder) and fill in your network.
 * net_time.c uses it only if wifi_test_credentials.h exists; otherwise the menuconfig
 * settings (Wi-Fi SSID / password) are used. wifi_test_credentials.h is in .gitignore,
 * so your password never goes to GitHub.
 */
#pragma once
#define TEST_WIFI_SSID     "your-network"
#define TEST_WIFI_PASSWORD "your-password"
