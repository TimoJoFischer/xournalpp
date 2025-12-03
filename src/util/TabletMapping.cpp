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

#include <glib.h>

#ifdef _WIN32
#include <Windows.h>
#include <shlobj.h>  // For SHGetFolderPathA

// Wintab API definitions for tablet mapping
// Load dynamically from WINTAB32.DLL
#include <cstring>

// Wintab data types
typedef DWORD WTPKT;
typedef HANDLE HCTX;
typedef HANDLE HMGR;
typedef UINT FIX32;

// Context options
#define CXO_SYSTEM      0x0001
#define CXO_PEN         0x0002
#define CXO_MESSAGES    0x0004

// Lock options
#define CXL_INSIZE      0x0001
#define CXL_INASPECT    0x0002
#define CXL_MARGIN      0x0004
#define CXL_SENSITIVITY 0x0008
#define CXL_SYSOUT      0x0010

// WTI categories
#define WTI_INTERFACE   1
#define WTI_STATUS      2
#define WTI_DEFCONTEXT  3
#define WTI_DEFSYSCTX   4
#define WTI_DEVICES     100
#define WTI_CURSORS     200
#define WTI_DDCTXS      400
#define WTI_DSCTXS      500

// WTI_DEVICES indices
#define DVC_X           13
#define DVC_Y           14

// LOGCONTEXT structure (must match Wintab header exactly)
#pragma pack(push, 1)
typedef struct tagLOGCONTEXTA {
    char    lcName[40];
    UINT    lcOptions;
    UINT    lcStatus;
    UINT    lcLocks;
    UINT    lcMsgBase;
    UINT    lcDevice;
    UINT    lcPktRate;
    WTPKT   lcPktData;
    WTPKT   lcPktMode;
    WTPKT   lcMoveMask;
    DWORD   lcBtnDnMask;
    DWORD   lcBtnUpMask;
    LONG    lcInOrgX;
    LONG    lcInOrgY;
    LONG    lcInOrgZ;
    LONG    lcInExtX;
    LONG    lcInExtY;
    LONG    lcInExtZ;
    LONG    lcOutOrgX;
    LONG    lcOutOrgY;
    LONG    lcOutOrgZ;
    LONG    lcOutExtX;
    LONG    lcOutExtY;
    LONG    lcOutExtZ;
    FIX32   lcSensX;
    FIX32   lcSensY;
    FIX32   lcSensZ;
    BOOL    lcSysMode;
    int     lcSysOrgX;
    int     lcSysOrgY;
    int     lcSysExtX;
    int     lcSysExtY;
    FIX32   lcSysSensX;
    FIX32   lcSysSensY;
} LOGCONTEXTA;

// AXIS structure for device info
typedef struct tagAXIS {
    LONG    axMin;
    LONG    axMax;
    UINT    axUnits;
    FIX32   axResolution;
} AXIS;
#pragma pack(pop)

// Wintab function typedefs
typedef UINT (WINAPI *WTINFOA)(UINT, UINT, LPVOID);
typedef HCTX (WINAPI *WTOPENA)(HWND, LOGCONTEXTA*, BOOL);
typedef BOOL (WINAPI *WTCLOSE)(HCTX);
typedef BOOL (WINAPI *WTGETA)(HCTX, LOGCONTEXTA*);
typedef BOOL (WINAPI *WTSETA)(HCTX, LOGCONTEXTA*);
typedef BOOL (WINAPI *WTENABLE)(HCTX, BOOL);
typedef BOOL (WINAPI *WTOVERLAP)(HCTX, BOOL);
typedef HMGR (WINAPI *WTMGROPEN)(HWND, UINT);
typedef BOOL (WINAPI *WTMGRCLOSE)(HMGR);
typedef HCTX (WINAPI *WTMGRDEFCONTEXT)(HMGR, BOOL);
typedef HCTX (WINAPI *WTMGRDEFCONTEXTEX)(HMGR, UINT, BOOL);

// Wintab function pointers (loaded dynamically)
static HMODULE g_hWintab = nullptr;
static WTINFOA g_WTInfoA = nullptr;
static WTOPENA g_WTOpenA = nullptr;
static WTCLOSE g_WTClose = nullptr;
static WTGETA g_WTGetA = nullptr;
static WTSETA g_WTSetA = nullptr;
static WTENABLE g_WTEnable = nullptr;
static WTOVERLAP g_WTOverlap = nullptr;
static WTMGROPEN g_WTMgrOpen = nullptr;
static WTMGRCLOSE g_WTMgrClose = nullptr;
static WTMGRDEFCONTEXT g_WTMgrDefContext = nullptr;
static WTMGRDEFCONTEXTEX g_WTMgrDefContextEx = nullptr;

// Our tablet context handle
static HCTX g_hTabletContext = nullptr;

// Tablet physical dimensions (in native tablet units)
static LONG g_tabletMaxX = 21600;
static LONG g_tabletMaxY = 13500;

static bool loadWintab() {
    if (g_hWintab) return true;
    
    g_hWintab = LoadLibraryA("WINTAB32.DLL");
    if (!g_hWintab) {
        g_warning("TabletMapping: Could not load WINTAB32.DLL");
        return false;
    }
    
    g_WTInfoA = (WTINFOA)GetProcAddress(g_hWintab, "WTInfoA");
    g_WTOpenA = (WTOPENA)GetProcAddress(g_hWintab, "WTOpenA");
    g_WTClose = (WTCLOSE)GetProcAddress(g_hWintab, "WTClose");
    g_WTGetA = (WTGETA)GetProcAddress(g_hWintab, "WTGetA");
    g_WTSetA = (WTSETA)GetProcAddress(g_hWintab, "WTSetA");
    g_WTEnable = (WTENABLE)GetProcAddress(g_hWintab, "WTEnable");
    g_WTOverlap = (WTOVERLAP)GetProcAddress(g_hWintab, "WTOverlap");
    g_WTMgrOpen = (WTMGROPEN)GetProcAddress(g_hWintab, "WTMgrOpen");
    g_WTMgrClose = (WTMGRCLOSE)GetProcAddress(g_hWintab, "WTMgrClose");
    g_WTMgrDefContext = (WTMGRDEFCONTEXT)GetProcAddress(g_hWintab, "WTMgrDefContext");
    g_WTMgrDefContextEx = (WTMGRDEFCONTEXTEX)GetProcAddress(g_hWintab, "WTMgrDefContextEx");
    
    if (!g_WTInfoA || !g_WTOpenA || !g_WTClose || !g_WTGetA || !g_WTSetA) {
        g_warning("TabletMapping: Could not load Wintab functions");
        FreeLibrary(g_hWintab);
        g_hWintab = nullptr;
        return false;
    }
    
    // Get tablet physical dimensions
    AXIS axisX, axisY;
    if (g_WTInfoA(WTI_DEVICES, DVC_X, &axisX) && g_WTInfoA(WTI_DEVICES, DVC_Y, &axisY)) {
        g_tabletMaxX = axisX.axMax;
        g_tabletMaxY = axisY.axMax;
        g_message("TabletMapping: Tablet dimensions: %ld x %ld", g_tabletMaxX, g_tabletMaxY);
    }
    
    return true;
}
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

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
    "",                                // Device ID - to be configured
    0.0, 0.0, 1.0, 1.0,               // Full window input area (full tablet)
    0.0, 0.0, 0.5, 1.0,               // Full window output area (left half of screen: 0-1720 x 0-1440)
    0.0, 0.0, 1.0, 1.0,               // Zoom window input area (full tablet)
    0.0, 0.5, 0.5, 0.5                // Zoom window output area (bottom-left quarter: 0-1720 x 720-1440)
};

// Current mapping state
TabletMapping::MappingMode TabletMapping::currentMode = TabletMapping::MappingMode::FullWindow;

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
    // Windows: Always available - we use application-local coordinate transformation
    return true;
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

TabletMapping::MappingMode TabletMapping::getCurrentMode() {
    return currentMode;
}

bool TabletMapping::setMappingMode(MappingMode mode) {
    currentMode = mode;
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
     * Windows tablet mapping using Wintab API.
     * 
     * Creates a system context with CXO_SYSTEM flag that controls the Windows cursor position.
     * By modifying lcSysOrgX/Y and lcSysExtX/Y, we can instantly change where the tablet
     * maps to on the screen.
     */
    
    if (!loadWintab()) {
        g_warning("TabletMapping: Wintab not available, cannot change mapping");
        return false;
    }
    
    // Get screen dimensions
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    g_message("TabletMapping: Screen size: %d x %d", screenWidth, screenHeight);
    
    // Determine mapping parameters based on mode
    double inX, inY, inW, inH;   // Input area on tablet (normalized 0-1)
    double outX, outY, outW, outH; // Output area on screen (normalized 0-1)
    
    if (mode == MappingMode::FullWindow) {
        inX = windowsConfig.fullInputX;
        inY = windowsConfig.fullInputY;
        inW = windowsConfig.fullInputWidth;
        inH = windowsConfig.fullInputHeight;
        outX = windowsConfig.fullOutputX;
        outY = windowsConfig.fullOutputY;
        outW = windowsConfig.fullOutputWidth;
        outH = windowsConfig.fullOutputHeight;
    } else {
        inX = windowsConfig.zoomInputX;
        inY = windowsConfig.zoomInputY;
        inW = windowsConfig.zoomInputWidth;
        inH = windowsConfig.zoomInputHeight;
        outX = windowsConfig.zoomOutputX;
        outY = windowsConfig.zoomOutputY;
        outW = windowsConfig.zoomOutputWidth;
        outH = windowsConfig.zoomOutputHeight;
    }
    
    g_message("TabletMapping: Mode=%s, Input=(%.2f,%.2f,%.2f,%.2f), Output=(%.2f,%.2f,%.2f,%.2f)",
              mode == MappingMode::FullWindow ? "FullWindow" : "ZoomWindow",
              inX, inY, inW, inH, outX, outY, outW, outH);
    
    // If we already have a context, modify it
    if (g_hTabletContext) {
        LOGCONTEXTA ctx;
        memset(&ctx, 0, sizeof(ctx));
        
        if (g_WTGetA(g_hTabletContext, &ctx)) {
            // Update input area (tablet coordinates)
            ctx.lcInOrgX = (LONG)(inX * g_tabletMaxX);
            ctx.lcInOrgY = (LONG)(inY * g_tabletMaxY);
            ctx.lcInExtX = (LONG)(inW * g_tabletMaxX);
            ctx.lcInExtY = (LONG)(inH * g_tabletMaxY);
            
            // Update system cursor output area (screen coordinates)
            ctx.lcSysOrgX = (int)(outX * screenWidth);
            ctx.lcSysOrgY = (int)(outY * screenHeight);
            ctx.lcSysExtX = (int)(outW * screenWidth);
            ctx.lcSysExtY = (int)(outH * screenHeight);
            
            g_message("TabletMapping: Modifying context - SysOrg=(%d,%d), SysExt=(%d,%d)",
                      ctx.lcSysOrgX, ctx.lcSysOrgY, ctx.lcSysExtX, ctx.lcSysExtY);
            
            if (g_WTSetA(g_hTabletContext, &ctx)) {
                currentMode = mode;
                g_message("TabletMapping: Successfully updated mapping");
                return true;
            } else {
                g_warning("TabletMapping: WTSetA failed");
            }
        } else {
            g_warning("TabletMapping: WTGetA failed");
        }
    }
    
    // No existing context or update failed - create a new one
    // Get the default system context as a template
    LOGCONTEXTA defCtx;
    memset(&defCtx, 0, sizeof(defCtx));
    
    if (!g_WTInfoA(WTI_DEFSYSCTX, 0, &defCtx)) {
        g_warning("TabletMapping: Could not get default system context");
        return false;
    }
    
    g_message("TabletMapping: Default context - InOrg=(%ld,%ld), InExt=(%ld,%ld), SysOrg=(%d,%d), SysExt=(%d,%d)",
              defCtx.lcInOrgX, defCtx.lcInOrgY, defCtx.lcInExtX, defCtx.lcInExtY,
              defCtx.lcSysOrgX, defCtx.lcSysOrgY, defCtx.lcSysExtX, defCtx.lcSysExtY);
    
    // Store tablet dimensions from context if not set
    if (defCtx.lcInExtX > 0) g_tabletMaxX = defCtx.lcInOrgX + defCtx.lcInExtX;
    if (defCtx.lcInExtY > 0) g_tabletMaxY = defCtx.lcInOrgY + defCtx.lcInExtY;
    
    // Modify context for our mapping
    strncpy(defCtx.lcName, "Xournal++ Tablet", 40);
    defCtx.lcOptions |= CXO_SYSTEM;  // This is a system cursor context
    
    // Set input area (tablet coordinates)
    defCtx.lcInOrgX = (LONG)(inX * g_tabletMaxX);
    defCtx.lcInOrgY = (LONG)(inY * g_tabletMaxY);
    defCtx.lcInExtX = (LONG)(inW * g_tabletMaxX);
    defCtx.lcInExtY = (LONG)(inH * g_tabletMaxY);
    
    // Set system cursor output area (screen coordinates)
    defCtx.lcSysOrgX = (int)(outX * screenWidth);
    defCtx.lcSysOrgY = (int)(outY * screenHeight);
    defCtx.lcSysExtX = (int)(outW * screenWidth);
    defCtx.lcSysExtY = (int)(outH * screenHeight);
    
    // Close existing context if any
    if (g_hTabletContext) {
        g_WTClose(g_hTabletContext);
        g_hTabletContext = nullptr;
    }
    
    // We need a window handle - get the active window or desktop
    HWND hWnd = GetActiveWindow();
    if (!hWnd) hWnd = GetDesktopWindow();
    
    g_message("TabletMapping: Opening new context - SysOrg=(%d,%d), SysExt=(%d,%d)",
              defCtx.lcSysOrgX, defCtx.lcSysOrgY, defCtx.lcSysExtX, defCtx.lcSysExtY);
    
    g_hTabletContext = g_WTOpenA(hWnd, &defCtx, TRUE);
    if (!g_hTabletContext) {
        g_warning("TabletMapping: Could not open tablet context");
        return false;
    }
    
    // Move our context to the top of the overlap order
    if (g_WTOverlap) {
        g_WTOverlap(g_hTabletContext, TRUE);
    }
    
    currentMode = mode;
    g_message("TabletMapping: Successfully created tablet context with new mapping");
    
    return true;
}
#else
bool TabletMapping::applyWindowsMapping(MappingMode /*mode*/) {
    return false;  // Not applicable on Linux
}
#endif

