#include "usb_hid_host.h"
#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "esp_log.h"

static const char* TAG = "USB_HID";

// ─── HID Boot-Keyboard report layout (8 bytes) ───────────────────────────────
#pragma pack(push, 1)
struct HidKeyboardReport {
    uint8_t modifier;    // bit1=LShift, bit5=RShift, etc.
    uint8_t reserved;
    uint8_t keycodes[6]; // up to 6 simultaneous keys
};
#pragma pack(pop)

// ─── Keycode → ASCII table (US layout, index = HID usage ID) ─────────────────
// [keycode][0] = unshifted,  [keycode][1] = shifted
static const char kKeyTable[128][2] = {
    {0,0},{0,0},{0,0},{0,0},   // 0x00-0x03
    {'a','A'},{'b','B'},{'c','C'},{'d','D'}, // 0x04-0x07
    {'e','E'},{'f','F'},{'g','G'},{'h','H'}, // 0x08-0x0B
    {'i','I'},{'j','J'},{'k','K'},{'l','L'}, // 0x0C-0x0F
    {'m','M'},{'n','N'},{'o','O'},{'p','P'}, // 0x10-0x13
    {'q','Q'},{'r','R'},{'s','S'},{'t','T'}, // 0x14-0x17
    {'u','U'},{'v','V'},{'w','W'},{'x','X'}, // 0x18-0x1B
    {'y','Y'},{'z','Z'},                     // 0x1C-0x1D
    {'1','!'},{'2','@'},{'3','#'},{'4','$'}, // 0x1E-0x21
    {'5','%'},{'6','^'},{'7','&'},{'8','*'}, // 0x22-0x25
    {'9','('},{'0',')'},                     // 0x26-0x27
    {'\n','\n'},{0,0},{'\b','\b'},{'\t','\t'}, // 0x28 Enter,0x29 Esc,0x2A BS,0x2B Tab
    {' ',' '},{'-','_'},{'=','+'},{'[','{'}, // 0x2C-0x2F
    {']','}'},{'\\','|'},{'#','~'},{';',':'}, // 0x30-0x33
    {'\'','"'},{'`','~'},{',','<'},{'.','>'}, // 0x34-0x37
    {'/','?'},                               // 0x38
};

// ─── Singleton ────────────────────────────────────────────────────────────────
USBHIDHost* USBHIDHost::_instance = nullptr;
USBHIDHost  UsbHid;

// ─── Constructor ──────────────────────────────────────────────────────────────
USBHIDHost::USBHIDHost() {
    _instance = this;
}

// ─── _keycodeToAscii() ────────────────────────────────────────────────────────
char USBHIDHost::_keycodeToAscii(uint8_t keycode, bool shifted) {
    if (keycode >= 128) return 0;
    return kKeyTable[keycode][shifted ? 1 : 0];
}

// ─── Transfer callback (ISR-like context) ─────────────────────────────────────
static void transfer_cb(usb_transfer_t* transfer) {
    if (!USBHIDHost::_instance) return;
    USBHIDHost::_instance->_onTransferDone(transfer);
}

void USBHIDHost::_onTransferDone(void* t) {
    auto* transfer = static_cast<usb_transfer_t*>(t);

    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED &&
        transfer->actual_num_bytes >= (int)sizeof(HidKeyboardReport)) {
        _processReport(transfer->data_buffer, transfer->actual_num_bytes);
    }

    // Resubmit for next report (only if still connected)
    if (_deviceConnected && _running) {
        usb_host_transfer_submit(transfer);
    }
}

// ─── _processReport() ────────────────────────────────────────────────────────
void USBHIDHost::_processReport(const uint8_t* data, size_t /*len*/) {
    const auto* report = reinterpret_cast<const HidKeyboardReport*>(data);
    bool shifted = (report->modifier & 0x02) || (report->modifier & 0x20);

    for (int i = 0; i < 6; i++) {
        uint8_t kc = report->keycodes[i];
        if (kc == 0) continue;

        // Only act on key-down (not seen in previous report)
        bool wasDown = false;
        for (int j = 0; j < 6; j++) {
            if (_prevKeys[j] == kc) { wasDown = true; break; }
        }
        if (wasDown) continue;

        char c = _keycodeToAscii(kc, shifted);

        // Determine configured terminator character
        // "crlf" and "cr" both fire on Enter ('\n'); for "crlf" the scanner sends
        // Enter twice – the second arrives with an empty buffer and is ignored.
        const String& term = Config.scanTerminator;
        char termChar = (term == "tab") ? '\t' : '\n'; // unused when term == "none"

        if (c == '\b') {
            if (_scanBuffer.length() > 0) {
                _scanBuffer.remove(_scanBuffer.length() - 1);
            }
        } else if (c == '\n' || c == '\t') {
            // Control characters: fire callback if this matches the terminator
            if (term != "none" && c == termChar) {
                String scanned = _scanBuffer;
                _scanBuffer    = "";
                _lastCharTime  = 0;
                if (scanned.length() > 0 && _callback) {
                    _callback(scanned);
                }
            }
            // otherwise ignore (e.g. Tab chars when mode is "lf")
        } else if (c != 0) {
            _scanBuffer   += c;
            _lastCharTime  = millis();
        }
    }

    memcpy(_prevKeys, report->keycodes, 6);
}

// ─── USB Library Task (handles library-level events) ─────────────────────────
void USBHIDHost::usb_lib_task(void* arg) {
    auto* self = static_cast<USBHIDHost*>(arg);
    ESP_LOGI(TAG, "USB lib task started");

    usb_host_config_t cfg = {
        .skip_phy_setup = false,
        .intr_flags     = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&cfg));
    ESP_LOGI(TAG, "USB Host installed");

    // Signal HID task that host is ready
    xTaskNotifyGive(self->_hidTaskHandle);

    uint32_t eventFlags = 0;
    while (self->_running) {
        usb_host_lib_handle_events(portMAX_DELAY, &eventFlags);
        if (eventFlags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "No USB clients – uninstalling");
            usb_host_device_free_all();
        }
        if (eventFlags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "All USB devices freed");
            break;
        }
    }

    usb_host_uninstall();
    ESP_LOGI(TAG, "USB lib task ended");
    vTaskDelete(nullptr);
}

// ─── HID Host Task (client: handles device connect/disconnect + reads) ────────
void USBHIDHost::hid_host_task(void* arg) {
    auto* self = static_cast<USBHIDHost*>(arg);
    ESP_LOGI(TAG, "HID host task waiting for USB lib...");

    // Wait for lib task to signal readiness
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "HID host task started");

    usb_host_client_config_t client_cfg = {
        .is_synchronous    = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = [](const usb_host_client_event_msg_t* msg, void* ctx) {
                auto* s = static_cast<USBHIDHost*>(ctx);
                s->_onClientEvent((void*)msg);
            },
            .callback_arg = self,
        },
    };

    usb_host_client_handle_t client;
    ESP_ERROR_CHECK(usb_host_client_register(&client_cfg, &client));
    self->_clientHandle = client;

    while (self->_running) {
        usb_host_client_handle_events(client, pdMS_TO_TICKS(50));
    }

    // Clean up
    if (self->_devHandle) {
        if (self->_ifaceNum >= 0) {
            usb_host_interface_release(client,
                static_cast<usb_device_handle_t>(self->_devHandle),
                self->_ifaceNum);
        }
        usb_host_device_close(client,
            static_cast<usb_device_handle_t>(self->_devHandle));
        self->_devHandle = nullptr;
    }
    usb_host_client_deregister(client);
    self->_clientHandle = nullptr;

    ESP_LOGI(TAG, "HID host task ended");
    vTaskDelete(nullptr);
}

// ─── Client event handler ─────────────────────────────────────────────────────
void USBHIDHost::_onClientEvent(void* ev) {
    const auto* msg = static_cast<const usb_host_client_event_msg_t*>(ev);
    auto client = static_cast<usb_host_client_handle_t>(_clientHandle);

    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        uint8_t devAddr = msg->new_dev.address;
        ESP_LOGI(TAG, "New USB device at address %d", devAddr);

        usb_device_handle_t devHdl;
        if (usb_host_device_open(client, devAddr, &devHdl) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open device");
            return;
        }
        _devHandle = devHdl;

        // Walk config descriptor to find HID interrupt-IN endpoint
        const usb_config_desc_t* cfgDesc;
        if (usb_host_get_active_config_descriptor(devHdl, &cfgDesc) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get config descriptor");
            usb_host_device_close(client, devHdl);
            _devHandle = nullptr;
            return;
        }

        int    ifaceNum = -1;
        uint8_t epAddr  = 0;
        int     offset  = 0;

        // Iterate through interface and endpoint descriptors
        while (offset < cfgDesc->wTotalLength) {
            const usb_standard_desc_t* desc =
                reinterpret_cast<const usb_standard_desc_t*>(
                    reinterpret_cast<const uint8_t*>(cfgDesc) + offset);

            if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                const auto* iface =
                    reinterpret_cast<const usb_intf_desc_t*>(desc);
                // HID class (3), accept any subclass/protocol (covers scanners)
                if (iface->bInterfaceClass == USB_CLASS_HID) {
                    ifaceNum = iface->bInterfaceNumber;
                    ESP_LOGI(TAG, "HID interface found: %d", ifaceNum);
                }
            }

            if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT &&
                ifaceNum >= 0 && epAddr == 0) {
                const auto* ep =
                    reinterpret_cast<const usb_ep_desc_t*>(desc);
                // Interrupt IN endpoint
                if ((ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) &&
                    (ep->bmAttributes & 0x03) == USB_BM_ATTRIBUTES_XFER_INT) {
                    epAddr = ep->bEndpointAddress;
                    ESP_LOGI(TAG, "Interrupt IN endpoint: 0x%02X  maxPkt=%d",
                             epAddr, ep->wMaxPacketSize);
                }
            }

            // Stop as soon as we have a complete HID interface + interrupt-IN endpoint
            if (ifaceNum >= 0 && epAddr != 0) break;

            if (desc->bLength == 0) break;
            offset += desc->bLength;
        }

        if (ifaceNum < 0 || epAddr == 0) {
            ESP_LOGW(TAG, "No suitable HID interface found");
            usb_host_device_close(client, devHdl);
            _devHandle = nullptr;
            return;
        }

        if (usb_host_interface_claim(client, devHdl, ifaceNum, 0) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to claim interface");
            usb_host_device_close(client, devHdl);
            _devHandle = nullptr;
            return;
        }

        _ifaceNum = ifaceNum;
        _epAddr   = epAddr;

        // Allocate and submit first transfer
        usb_transfer_t* xfer;
        if (usb_host_transfer_alloc(8, 0, &xfer) != ESP_OK) {
            ESP_LOGE(TAG, "Transfer alloc failed");
            return;
        }
        xfer->device_handle    = devHdl;
        xfer->bEndpointAddress = epAddr;
        xfer->num_bytes        = 8;
        xfer->callback         = transfer_cb;
        xfer->context          = this;
        _transfer              = xfer;

        usb_host_transfer_submit(xfer);
        _deviceConnected = true;
        ESP_LOGI(TAG, "HID device ready, polling reports");

    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        ESP_LOGI(TAG, "USB device disconnected");
        _deviceConnected = false;

        if (_transfer) {
            usb_host_transfer_free(static_cast<usb_transfer_t*>(_transfer));
            _transfer = nullptr;
        }
        if (_devHandle && _ifaceNum >= 0) {
            usb_host_interface_release(client,
                static_cast<usb_device_handle_t>(_devHandle), _ifaceNum);
            _ifaceNum = -1;
        }
        if (_devHandle) {
            usb_host_device_close(client,
                static_cast<usb_device_handle_t>(_devHandle));
            _devHandle = nullptr;
        }
        _scanBuffer = "";
        memset(_prevKeys, 0, 6);
    }
}

// ─── begin() ─────────────────────────────────────────────────────────────────
void USBHIDHost::begin(ScanCallback cb) {
    _callback = cb;
    _running  = true;

    // HID task must be created first so the lib task can notify it
    xTaskCreatePinnedToCore(hid_host_task, "usb_hid", 4096, this, 4,
                            &_hidTaskHandle, 0);
    xTaskCreatePinnedToCore(usb_lib_task,  "usb_lib", 4096, this, 5,
                            &_libTaskHandle, 0);
}

// ─── checkTimeout() ──────────────────────────────────────────────────────────
void USBHIDHost::checkTimeout() {
    if (Config.scanTerminator != "none") return;
    if (_scanBuffer.isEmpty() || _lastCharTime == 0) return;
    if (millis() - _lastCharTime >= 300) {
        String scanned = _scanBuffer;
        _scanBuffer   = "";
        _lastCharTime = 0;
        if (_callback) _callback(scanned);
    }
}

// ─── stop() ──────────────────────────────────────────────────────────────────
void USBHIDHost::stop() {
    _running = false;
    // Tasks will exit their loops and self-delete
}
