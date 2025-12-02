/*
 * Xournal++
 *
 * Tablet input area mapping utility implementation.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include "util/TabletMapping.h"

#include <cstdlib>
#include <sstream>
#include <array>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

#include <glib.h>

// Static member initialization
TabletMapping::LinuxKDEConfig TabletMapping::linuxConfig = {
    "[Libinput][1386][888][Wacom Intuos BT M Pen]",  // Default group - should be configured
    "64357a01-6dbe-4386-a091-2f660a05ec5d",          // Default output UUID - should be configured
    0.0, 0.0, 0.7730, 1.0,                              // Full window input area (full tablet)
    0.0, 0.0, 0.5003, 1.0,              // Full window output area
    0.0, 0.0, 0.7730, 1.0,               // Zoom window input area
    0.3683,0.6321,0.1792,0.3594           // Zoom window output area
};

TabletMapping::WindowsConfig TabletMapping::windowsConfig = {
    "",                     // Device ID - to be configured
    0.0, 0.0, 1.0, 1.0,    // Full window area
    0.0, 0.0, 1.0, 1.0     // Zoom window area
};

void TabletMapping::setLinuxKDEConfig(const LinuxKDEConfig& config) {
    linuxConfig = config;
}

void TabletMapping::setWindowsConfig(const WindowsConfig& config) {
    windowsConfig = config;
}

TabletMapping::LinuxKDEConfig TabletMapping::getLinuxKDEConfig() {
    return linuxConfig;
}

TabletMapping::WindowsConfig TabletMapping::getWindowsConfig() {
    return windowsConfig;
}

bool TabletMapping::isAvailable() {
#ifdef _WIN32
    // Windows: Check if we can access tablet APIs
    // TODO: Implement proper check for Wintab or Windows Ink availability
    return false;  // Not yet implemented
#else
    // Linux: Check if kwriteconfig6 is available (KDE Plasma 6)
    // Falls back to kwriteconfig5 for older KDE versions
    int result = std::system("which kwriteconfig6 > /dev/null 2>&1");
    if (result == 0) {
        return true;
    }
    result = std::system("which kwriteconfig5 > /dev/null 2>&1");
    return result == 0;
#endif
}

bool TabletMapping::setMappingMode(MappingMode mode) {
#ifdef _WIN32
    return applyWindowsMapping(mode);
#else
    return applyLinuxKDEMapping(mode);
#endif
}

#ifndef _WIN32
bool TabletMapping::applyLinuxKDEMapping(MappingMode mode) {
    // Determine which kwriteconfig to use
    std::string kwriteconfig = "kwriteconfig6";
    int result = std::system("which kwriteconfig6 > /dev/null 2>&1");
    if (result != 0) {
        result = std::system("which kwriteconfig5 > /dev/null 2>&1");
        if (result != 0) {
            g_warning("TabletMapping: Neither kwriteconfig6 nor kwriteconfig5 found");
            return false;
        }
        kwriteconfig = "kwriteconfig5";
    }

    // Build the input and output area strings based on mode
    // Use std::to_string to ensure C locale (dot as decimal separator)
    std::string inputArea;
    std::string outputArea;
    if (mode == MappingMode::FullWindow) {
        inputArea = std::to_string(linuxConfig.fullInputX) + "," +
                    std::to_string(linuxConfig.fullInputY) + "," +
                    std::to_string(linuxConfig.fullInputWidth) + "," +
                    std::to_string(linuxConfig.fullInputHeight);
        outputArea = std::to_string(linuxConfig.fullOutputX) + "," +
                     std::to_string(linuxConfig.fullOutputY) + "," +
                     std::to_string(linuxConfig.fullOutputWidth) + "," +
                     std::to_string(linuxConfig.fullOutputHeight);
    } else {
        inputArea = std::to_string(linuxConfig.zoomInputX) + "," +
                    std::to_string(linuxConfig.zoomInputY) + "," +
                    std::to_string(linuxConfig.zoomInputWidth) + "," +
                    std::to_string(linuxConfig.zoomInputHeight);
        outputArea = std::to_string(linuxConfig.zoomOutputX) + "," +
                     std::to_string(linuxConfig.zoomOutputY) + "," +
                     std::to_string(linuxConfig.zoomOutputWidth) + "," +
                     std::to_string(linuxConfig.zoomOutputHeight);
    }

    // Build the command to update kcminputrc
    // The group is something like "[Libinput][1386][888][Wacom Intuos BT M Pen]"
    // kwriteconfig6 expects multiple --group arguments for nested groups, without brackets
    std::string group = linuxConfig.group;
    
    // Parse the group string into individual group components
    // Input: "[Libinput][1386][888][Wacom Intuos BT M Pen]"
    // Output: --group Libinput --group 1386 --group 888 --group "Wacom Intuos BT M Pen"
    std::string groupArgs;
    size_t pos = 0;
    while (pos < group.length()) {
        // Find opening bracket
        size_t start = group.find('[', pos);
        if (start == std::string::npos) break;
        
        // Find closing bracket
        size_t end = group.find(']', start);
        if (end == std::string::npos) break;
        
        // Extract group name between brackets
        std::string groupName = group.substr(start + 1, end - start - 1);
        if (!groupName.empty()) {
            groupArgs += " --group \"" + groupName + "\"";
        }
        
        pos = end + 1;
    }
    
    if (groupArgs.empty()) {
        g_warning("TabletMapping: Failed to parse group name: %s", group.c_str());
        return false;
    }

    // Set InputArea
    std::ostringstream inputCmdStream;
    inputCmdStream << kwriteconfig << " --file kcminputrc"
                   << groupArgs
                   << " --key InputArea"
                   << " \"" << inputArea << "\"";
    
    std::string inputCmd = inputCmdStream.str();
    g_message("TabletMapping: Executing: %s", inputCmd.c_str());
    
    result = std::system(inputCmd.c_str());
    if (result != 0) {
        g_warning("TabletMapping: Failed to execute kwriteconfig command for InputArea");
        return false;
    }

    // Set OutputArea
    std::ostringstream outputCmdStream;
    outputCmdStream << kwriteconfig << " --file kcminputrc"
                    << groupArgs
                    << " --key OutputArea"
                    << " \"" << outputArea << "\"";
    
    std::string outputCmd = outputCmdStream.str();
    g_message("TabletMapping: Executing: %s", outputCmd.c_str());
    
    result = std::system(outputCmd.c_str());
    if (result != 0) {
        g_warning("TabletMapping: Failed to execute kwriteconfig command for OutputArea");
        return false;
    }

    // Reload KWin configuration to apply changes immediately
    // Try various methods to signal KWin to reload its input device configuration
    // qdbus6 for Plasma 6, qdbus for Plasma 5, dbus-send as fallback
    result = std::system(
        "qdbus6 org.kde.KWin /KWin reconfigure 2>/dev/null || "
        "qdbus org.kde.KWin /KWin reconfigure 2>/dev/null || "
        "dbus-send --session --dest=org.kde.KWin --type=method_call /KWin org.kde.KWin.reconfigure 2>/dev/null"
    );
    if (result != 0) {
        g_message("TabletMapping: Could not signal KWin to reload config. "
                 "You may need to restart KWin or log out/in for changes to take effect.");
    }
    
    // Additionally, try to set the input/output area directly via DBus for immediate effect
    // This requires knowing the event device name (e.g., event15 for "Wacom Intuos BT M Pen")
    // Extract device name from the group to find the matching event device
    std::string deviceName;
    size_t lastBracket = group.rfind('[');
    if (lastBracket != std::string::npos) {
        size_t endBracket = group.find(']', lastBracket);
        if (endBracket != std::string::npos) {
            deviceName = group.substr(lastBracket + 1, endBracket - lastBracket - 1);
        }
    }
    
    if (!deviceName.empty()) {
        // Find the event device by name and set properties directly
        // The inputArea and outputArea properties use QRectF format: (x, y, width, height)
        double inX, inY, inW, inH, outX, outY, outW, outH;
        if (mode == MappingMode::FullWindow) {
            inX = linuxConfig.fullInputX;
            inY = linuxConfig.fullInputY;
            inW = linuxConfig.fullInputWidth;
            inH = linuxConfig.fullInputHeight;
            outX = linuxConfig.fullOutputX;
            outY = linuxConfig.fullOutputY;
            outW = linuxConfig.fullOutputWidth;
            outH = linuxConfig.fullOutputHeight;
        } else {
            inX = linuxConfig.zoomInputX;
            inY = linuxConfig.zoomInputY;
            inW = linuxConfig.zoomInputWidth;
            inH = linuxConfig.zoomInputHeight;
            outX = linuxConfig.zoomOutputX;
            outY = linuxConfig.zoomOutputY;
            outW = linuxConfig.zoomOutputWidth;
            outH = linuxConfig.zoomOutputHeight;
        }
        
        // Try to find and update the device via DBus
        // First get list of devices, then find the one matching our device name
        std::ostringstream findDeviceCmd;
        findDeviceCmd << "for dev in $(dbus-send --session --print-reply --dest=org.kde.KWin "
                      << "/org/kde/KWin/InputDevice org.freedesktop.DBus.Properties.Get "
                      << "string:'org.kde.KWin.InputDeviceManager' string:'devicesSysNames' 2>/dev/null "
                      << "| grep 'string \"event' | sed 's/.*\"\\(event[0-9]*\\)\".*/\\1/'); do "
                      << "name=$(dbus-send --session --print-reply --dest=org.kde.KWin "
                      << "/org/kde/KWin/InputDevice/$dev org.freedesktop.DBus.Properties.Get "
                      << "string:'org.kde.KWin.InputDevice' string:'name' 2>/dev/null "
                      << "| grep 'string \"' | tail -1 | sed 's/.*\"\\(.*\\)\".*/\\1/'); "
                      << "if [ \"$name\" = \"" << deviceName << "\" ]; then echo $dev; break; fi; done";
        
        // Execute and capture output
        FILE* pipe = popen(findDeviceCmd.str().c_str(), "r");
        if (pipe) {
            char buffer[128];
            std::string eventDevice;
            if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                eventDevice = buffer;
                // Remove trailing newline
                if (!eventDevice.empty() && eventDevice.back() == '\n') {
                    eventDevice.pop_back();
                }
            }
            pclose(pipe);
            
            if (!eventDevice.empty()) {
                g_message("TabletMapping: Found device '%s' as %s", deviceName.c_str(), eventDevice.c_str());
                
                // Use gdbus to set inputArea - it handles QRectF (struct of 4 doubles) correctly
                std::ostringstream inputDbusCmd;
                inputDbusCmd << "gdbus call --session --dest org.kde.KWin "
                             << "--object-path /org/kde/KWin/InputDevice/" << eventDevice << " "
                             << "--method org.freedesktop.DBus.Properties.Set "
                             << "'org.kde.KWin.InputDevice' 'inputArea' "
                             << "\"<(" << std::to_string(inX) << ", " << std::to_string(inY) << ", "
                             << std::to_string(inW) << ", " << std::to_string(inH) << ")>\" 2>/dev/null";
                
                g_message("TabletMapping: Setting inputArea via DBus: %s", inputDbusCmd.str().c_str());
                result = std::system(inputDbusCmd.str().c_str());
                if (result != 0) {
                    g_warning("TabletMapping: Failed to set inputArea via DBus");
                }
                
                // Use gdbus to set outputArea
                std::ostringstream outputDbusCmd;
                outputDbusCmd << "gdbus call --session --dest org.kde.KWin "
                              << "--object-path /org/kde/KWin/InputDevice/" << eventDevice << " "
                              << "--method org.freedesktop.DBus.Properties.Set "
                              << "'org.kde.KWin.InputDevice' 'outputArea' "
                              << "\"<(" << std::to_string(outX) << ", " << std::to_string(outY) << ", "
                              << std::to_string(outW) << ", " << std::to_string(outH) << ")>\" 2>/dev/null";
                
                g_message("TabletMapping: Setting outputArea via DBus: %s", outputDbusCmd.str().c_str());
                result = std::system(outputDbusCmd.str().c_str());
                if (result != 0) {
                    g_warning("TabletMapping: Failed to set outputArea via DBus");
                }
            } else {
                g_message("TabletMapping: Could not find event device for '%s'", deviceName.c_str());
            }
        }
    }

    return true;
}
#else
bool TabletMapping::applyLinuxKDEMapping(MappingMode /*mode*/) {
    return false;  // Not applicable on Windows
}
#endif

#ifdef _WIN32
bool TabletMapping::applyWindowsMapping(MappingMode mode) {
    /*
     * TODO: Implement Windows tablet mapping.
     * 
     * Possible approaches:
     * 
     * 1. Wintab API:
     *    - Use WTInfo() to query tablet capabilities
     *    - Use WTSet() to modify tablet context
     *    - May require modifying the application's tablet context
     *    - See: https://developer-docs.wacom.com/docs/icbt/windows/wintab/wintab-basics/
     * 
     * 2. Windows Ink / Pointer Input:
     *    - Use Windows.Devices.Input.Preview API
     *    - May have limited mapping control
     * 
     * 3. Wacom Tablet Preferences API:
     *    - Some Wacom drivers expose COM interfaces
     *    - Can modify tablet area mappings programmatically
     * 
     * 4. Registry manipulation:
     *    - Wacom stores settings in registry
     *    - Modify and signal driver to reload
     *    - Location varies by driver version
     * 
     * Example structure for Wintab implementation:
     * 
     * LOGCONTEXT lc;
     * WTInfo(WTI_DEFCONTEXT, 0, &lc);
     * 
     * if (mode == MappingMode::ZoomWindow) {
     *     // Map to zoom window area
     *     lc.lcOutOrgX = (LONG)(windowsConfig.zoomLeft * lc.lcOutExtX);
     *     lc.lcOutOrgY = (LONG)(windowsConfig.zoomTop * lc.lcOutExtY);
     *     lc.lcOutExtX = (LONG)((windowsConfig.zoomRight - windowsConfig.zoomLeft) * lc.lcOutExtX);
     *     lc.lcOutExtY = (LONG)((windowsConfig.zoomBottom - windowsConfig.zoomTop) * lc.lcOutExtY);
     * } else {
     *     // Map to full window area
     *     // Use default or configured full window settings
     * }
     * 
     * WTSet(hCtx, &lc);
     */
    
    // For now, just log that this is not yet implemented
    OutputDebugStringA("TabletMapping: Windows tablet mapping not yet implemented\n");
    
    // Return false to indicate the operation is not supported yet
    return false;
}
#else
bool TabletMapping::applyWindowsMapping(MappingMode /*mode*/) {
    return false;  // Not applicable on Linux
}
#endif
