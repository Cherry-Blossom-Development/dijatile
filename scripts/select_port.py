# PlatformIO pre-script: pick the upload port by the board's USB serial number.
#
# Port names like /dev/cu.usbmodem1101 belong to the USB socket, not the board, so
# they change whenever a tile is plugged in somewhere else. Every ESP32 USB-JTAG port
# reports the chip's MAC as its serial number, which never changes. Set it per
# environment (in the git-ignored platformio_local.ini):
#
#   [env:TheControlsOne]
#   custom_usb_serial = 40:4C:CA:F5:81:C4
#
# On upload, the port with that serial number is used. If that board isn't plugged
# in, the upload stops instead of flashing whichever board happens to be there.
# Without custom_usb_serial, PlatformIO picks the port itself as usual.

Import("env")  # noqa: F821  (provided by PlatformIO)

from SCons.Script import COMMAND_LINE_TARGETS  # noqa: E402

wanted = env.GetProjectOption("custom_usb_serial", "").strip().upper()  # noqa: F821
uploading = any(t in COMMAND_LINE_TARGETS for t in ("upload", "uploadfs", "nobuild"))

if wanted and uploading:
    from serial.tools import list_ports

    found = None
    for port in list_ports.comports():
        if (port.serial_number or "").strip().upper() == wanted:
            found = port.device
            break

    if found:
        env.Replace(UPLOAD_PORT=found)  # noqa: F821
        print("Upload port for %s: %s (board %s)" % (env["PIOENV"], found, wanted))  # noqa: F821
    else:
        present = ", ".join(
            "%s (%s)" % (p.device, p.serial_number) for p in list_ports.comports() if p.serial_number
        ) or "none"
        print("\n*** %s: the board with USB serial %s is not plugged in." % (env["PIOENV"], wanted))  # noqa: F821
        print("*** Boards found: %s" % present)
        print("*** Not uploading, so the wrong tile can't be flashed.\n")
        env.Exit(1)  # noqa: F821
