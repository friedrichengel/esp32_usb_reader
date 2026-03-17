#pragma once

#include <Arduino.h>
#include <functional>

// Callback type: called with the complete scanned string when Enter is received
using ScanCallback = std::function<void(const String& data)>;

// ─── USBHIDHost ──────────────────────────────────────────────────────────────
// Runs the ESP-IDF USB Host Library in two FreeRTOS tasks:
//   1. usb_lib_task  – drives the USB Host Library event loop
//   2. hid_host_task – registers as a client, handles device attach/detach,
//                      claims the HID keyboard interface, polls reports
//
// Most USB QR-code scanners enumerate as USB HID boot-keyboard devices
// (class=3, subclass=1, protocol=1) and send keystrokes terminated by Enter.
class USBHIDHost {
public:
    USBHIDHost();

    /**
     * Start USB host processing.
     * @param cb Called on the Arduino task when a complete scan line arrives.
     */
    void begin(ScanCallback cb);

    /** Stop both tasks and uninstall the USB Host Library. */
    void stop();

    bool isDeviceConnected() const { return _deviceConnected; }

    // ── Internal use – do not call directly ──
    void _onLibEvent(void* event_msg);
    void _onClientEvent(void* event_msg);
    void _onTransferDone(void* transfer);

private:
    static void usb_lib_task(void* arg);
    static void hid_host_task(void* arg);

    ScanCallback _callback;
    TaskHandle_t _libTaskHandle  = nullptr;
    TaskHandle_t _hidTaskHandle  = nullptr;

    volatile bool _deviceConnected = false;
    volatile bool _running         = false;

    // Scan line buffer (filled character by character from HID reports)
    String   _scanBuffer;
    uint8_t  _prevKeys[6] = {};          // previous key state (for key-down detection)

    // Forward-declared opaque handles (ESP-IDF types)
    void* _clientHandle = nullptr;
    void* _devHandle    = nullptr;
    void* _transfer     = nullptr;
    uint8_t _epAddr     = 0;
    int     _ifaceNum   = -1;

    void _processReport(const uint8_t* data, size_t len);
    static char _keycodeToAscii(uint8_t keycode, bool shifted);

public:
    // Exposed for the free-function transfer_cb (cannot be private)
    static USBHIDHost* _instance;

private:
};

extern USBHIDHost UsbHid;
