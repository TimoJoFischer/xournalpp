/*
 * Xournal++
 *
 * Tablet input area mapping utility for drawing tablets.
 * Maps tablet input to either the full application window or a specific zoom window region.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>

/**
 * Utility class for mapping tablet input areas.
 * 
 * On Linux (KDE/KWin): Modifies kwinrc tablet configuration using kwriteconfig6.
 * On Windows: Template for future implementation (requires Wacom/tablet driver API).
 */
class TabletMapping {
public:
    /**
     * Configuration for tablet mapping on Linux (KDE).
     * These values correspond to the [Libinput] section in kwinrc.
     */
    struct LinuxKDEConfig {
        std::string group;       // e.g., "[Libinput][1386][888][Wacom Intuos BT M Pen]"
        std::string outputUuid;  // Monitor UUID
        
        // Input area for full window mapping (portion of tablet surface to use)
        double fullInputX = 0.0;
        double fullInputY = 0.0;
        double fullInputWidth = 1.0;
        double fullInputHeight = 1.0;
        
        // Output area for full window mapping (portion of screen to map to)
        double fullOutputX = 0.0;
        double fullOutputY = 0.0;
        double fullOutputWidth = 0.5003431165540541;
        double fullOutputHeight = 1.0;
        
        // Input area for zoom window mapping (portion of tablet surface to use)
        double zoomInputX = 0.0;
        double zoomInputY = 0.0;
        double zoomInputWidth = 0.7730263157894738;
        double zoomInputHeight = 1.0;
        
        // Output area for zoom window mapping (portion of screen to map to)
        double zoomOutputX = 0.4;
        double zoomOutputY = 0.8;
        double zoomOutputWidth = 0.5003431165540541;
        double zoomOutputHeight = 1.0;
    };

    /**
     * Configuration for tablet mapping on Windows.
     * Uses Wintab API concepts for tablet-to-screen mapping.
     * 
     * NOTE: Unlike Linux KDE where we can modify system-wide mappings,
     * Wintab contexts are per-application. This configuration affects
     * how Xournal++ receives tablet data, but may not change the
     * system cursor position mapping.
     */
    struct WindowsConfig {
        // Device identifier (for future use - multiple tablet support)
        std::string deviceId;
        
        // Input area for full window mapping (portion of tablet surface to use, normalized 0-1)
        double fullInputX = 0.0;
        double fullInputY = 0.0;
        double fullInputWidth = 1.0;
        double fullInputHeight = 1.0;
        
        // Output area for full window mapping (portion of screen to map to, normalized 0-1)
        double fullOutputX = 0.0;
        double fullOutputY = 0.0;
        double fullOutputWidth = 1.0;
        double fullOutputHeight = 1.0;
        
        // Input area for zoom window mapping (portion of tablet surface to use, normalized 0-1)
        double zoomInputX = 0.0;
        double zoomInputY = 0.0;
        double zoomInputWidth = 1.0;
        double zoomInputHeight = 1.0;
        
        // Output area for zoom window mapping (portion of screen to map to, normalized 0-1)
        double zoomOutputX = 0.0;
        double zoomOutputY = 0.0;
        double zoomOutputWidth = 1.0;
        double zoomOutputHeight = 1.0;
    };

    /**
     * Mapping mode for the tablet.
     */
    enum class MappingMode {
        FullWindow,   // Map tablet to entire application window
        ZoomWindow    // Map tablet to zoom window region only
    };

    /**
     * Set the tablet mapping mode.
     * 
     * @param mode The desired mapping mode (FullWindow or ZoomWindow)
     * @return true if the mapping was successfully applied, false otherwise
     */
    static bool setMappingMode(MappingMode mode);

    /**
     * Set the Linux KDE configuration.
     * Call this before using setMappingMode() on Linux.
     * 
     * @param config The KDE tablet configuration
     */
    static void setLinuxKDEConfig(const LinuxKDEConfig& config);

    /**
     * Set the Windows configuration.
     * Call this before using setMappingMode() on Windows.
     * 
     * @param config The Windows tablet configuration
     */
    static void setWindowsConfig(const WindowsConfig& config);

    /**
     * Get the current Linux KDE configuration.
     */
    static LinuxKDEConfig getLinuxKDEConfig();

    /**
     * Get the current Windows configuration.
     */
    static WindowsConfig getWindowsConfig();

    /**
     * Check if tablet mapping is available on this system.
     * 
     * @return true if the system supports tablet mapping
     */
    static bool isAvailable();

    /**
     * Get the current mapping mode.
     */
    static MappingMode getCurrentMode();

private:
    static LinuxKDEConfig linuxConfig;
    static WindowsConfig windowsConfig;
    static MappingMode currentMode;

    /**
     * Apply tablet mapping on Linux using kwriteconfig6 and DBus.
     */
    static bool applyLinuxKDEMapping(MappingMode mode);

    /**
     * Apply tablet mapping on Windows using Wintab API.
     */
    static bool applyWindowsMapping(MappingMode mode);
};
