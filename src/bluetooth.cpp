#include "bluetooth.h"
#include "bluetooth_platforms.h"
#include "interface/uart.h"
#include "util/log.h"
#include "interface/uart.h"
#include "atcommander.h"
#include "util/timer.h"
#include "gpio.h"
#include <string.h>

#define BLUETOOTH_DEVICE_NAME "OpenXC-VI"
#define BLUETOOTH_SLAVE_MODE 0
#define BLUETOOTH_PAIRING_MODE 6
#define BLUETOOTH_AUTO_MASTER_MODE 3

namespace gpio = openxc::gpio;
namespace uart = openxc::interface::uart;

using openxc::interface::uart::UartDevice;
using openxc::gpio::GpioValue;
using openxc::gpio::GPIO_DIRECTION_OUTPUT;
using openxc::gpio::GPIO_DIRECTION_INPUT;
using openxc::gpio::GPIO_VALUE_HIGH;
using openxc::gpio::GPIO_VALUE_LOW;
using openxc::util::time::delayMs;
using openxc::util::log::debug;

extern const AtCommanderPlatform AT_PLATFORM_RN42;

static bool BLUETOOTH_STORE_REMOTE_ADDRESS = false;

void changeBaudRate(void* device, int baud) {
    uart::changeBaudRate((UartDevice*)device, baud);
}

int readByte(void* device) {
    return uart::readByte((UartDevice*)device);
}

void writeByte(void* device, uint8_t byte) {
    uart::writeByte((UartDevice*)device, byte);
}

void openxc::bluetooth::configureExternalModule(UartDevice* device) {
    AtCommanderConfig config = {AT_PLATFORM_RN42};

    config.baud_rate_initializer = changeBaudRate;
    config.device = device;
    config.write_function = writeByte;
    config.read_function = readByte;
    config.delay_function = delayMs;
    config.log_function = debug;

    // we most likely just power cycled the RN-42 to make sure it was on, so
    // wait for it to boot up
    delayMs(500);
    if(at_commander_set_baud(&config, device->baudRate)) {
        debug("Successfully set baud rate");
        if(at_commander_set_name(&config, BLUETOOTH_DEVICE_NAME, true)) {
            debug("Successfully set Bluetooth device name");
        } else {
            debug("Unable to set Bluetooth device name");
        }

        if(at_commander_get_device_id(&config, device->deviceId,
                    sizeof(device->deviceId)) > 0) {
            debug("Bluetooth MAC is %s", device->deviceId);
        } else {
            debug("Unable to get Bluetooth MAC");
            device->deviceId[0] = '\0';
        }

        if(at_commander_set_configuration_timer(&config, 0)) {
            debug("Successfully disabled remote Bluetooth configuration");
        } else {
            debug("Unable to disable remote Bluetooth configuration");
        }

        AtCommand inquiryCommand = {
            request_format: "SI,%s\r",
            expected_response: "AOK",
            error_response: "ERR"
        };

        if(at_commander_set(&config, &inquiryCommand, "0200")) {
            debug("Changed Bluetooth inquiry window to 0200");
        } else {
            debug("Unable to change Bluetooth inquiry window.");
        }

        AtCommand pagingCommand = {
            request_format: "SJ,%s\r",
            expected_response: "AOK",
            error_response: "ERR"
        };

        if(at_commander_set(&config, &pagingCommand, "0200")) {
            debug("Changed Bluetooth page scan window to 0200");
        } else {
            debug("Unable to change Bluetooth page scan window.");
        }

        AtCommand firmwareVersionCommand = {
            request_format: "V\r",
            expected_response: NULL,
            error_response: "ERR"
        };

        char versionString[64];
        if(!at_commander_get(&config, &firmwareVersionCommand, versionString, sizeof(versionString))) {
            debug("Unable to determine Bluetooth module firmware version");
        } else {
            debug("Bluetooth module is running firmware %s", versionString);

            AtCommand modeCommand = {
                request_format: "SM,%d\r",
                expected_response: "AOK",
                error_response: "ERR"
            };

            int desiredMode = BLUETOOTH_SLAVE_MODE;
            if(strstr(versionString, "6.") != NULL) {
                debug("Bluetooth device is on 6.x firmware - switching to pairing mode");
                desiredMode = BLUETOOTH_PAIRING_MODE;
            } else {
                debug("Bluetooth device is on 4.x firmware");

                AtCommand remoteAddressQuery = {
                    request_format: "GR\r",
                    expected_response: NULL,
                    error_response: "ERR"
                };

                char remoteAddress[64];
                if(!at_commander_get(&config, &remoteAddressQuery, remoteAddress, sizeof(remoteAddress))) {
                    debug("Unable to get currently stored remote BT address");
                } else {
                    if(strncmp(remoteAddress, "NOT SET", sizeof(remoteAddress))) {
                        desiredMode = BLUETOOTH_AUTO_MASTER_MODE;
                        BLUETOOTH_STORE_REMOTE_ADDRESS = true;
                        debug("Remote BT address stored - switching to auto-connect master mode");
                    } else {
                        BLUETOOTH_STORE_REMOTE_ADDRESS = false;
                        desiredMode = BLUETOOTH_SLAVE_MODE;
                        debug("No remote BT address stored - switching to slave mode");
                    }
                }
            }

            if(!at_commander_set(&config, &modeCommand, desiredMode)) {
                debug("Unable to change Bluetooth device mode");
            }
        }

        at_commander_reboot(&config);
    } else {
        debug("Unable to set baud rate of attached UART device");
    }
}

void setStatus(bool enabled) {
#ifdef BLUETOOTH_ENABLE_SUPPORT
    enabled = BLUETOOTH_ENABLE_PIN_POLARITY ? enabled : !enabled;
    debug("Turning Bluetooth %s", enabled ? "on" : "off");
    gpio::setValue(BLUETOOTH_ENABLE_PORT, BLUETOOTH_ENABLE_PIN,
            enabled ? GPIO_VALUE_HIGH : GPIO_VALUE_LOW);
#endif
}

void openxc::bluetooth::initialize(UartDevice* device) {
#ifdef BLUETOOTH_SUPPORT
    debug("Initializing Bluetooth...");

#ifdef BLUETOOTH_ENABLE_SUPPORT
    // be aware that setting the direction here will default it to the off
    // state, so the Bluetooth module will go *off* and then back *on*
    gpio::setDirection(BLUETOOTH_ENABLE_PORT, BLUETOOTH_ENABLE_PIN,
            GPIO_DIRECTION_OUTPUT);
    setStatus(true);
#endif

    strcpy(device->deviceId, "Unknown");
    configureExternalModule(device);
    // re-init to flush any junk in the buffer
    uart::initializeCommon(device);

    debug("Done.");
#endif
}

void openxc::bluetooth::deinitialize() {
    setStatus(false);
}
