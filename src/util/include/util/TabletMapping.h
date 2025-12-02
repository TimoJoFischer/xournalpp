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
     * TODO: Fill in with actual Windows tablet API parameters.
     */
    struct WindowsConfig {
        // Device identifier (e.g., from Wintab API or Windows Ink)
        std::string deviceId;
        
        // Full window mapping area (normalized 0-1)
        double fullLeft = 0.0;
        double fullTop = 0.0;
        double fullRight = 1.0;
        double fullBottom = 1.0;
        
        // Zoom window mapping area (normalized 0-1)
        double zoomLeft = 0.0;
        double zoomTop = 0.0;
        double zoomRight = 1.0;
        double zoomBottom = 1.0;
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

private:
    static LinuxKDEConfig linuxConfig;
    static WindowsConfig windowsConfig;

    /**
     * Apply tablet mapping on Linux using kwriteconfig6.
     */
    static bool applyLinuxKDEMapping(MappingMode mode);

    /**
     * Apply tablet mapping on Windows.
     * TODO: Implement using Windows tablet API.
     */
    static bool applyWindowsMapping(MappingMode mode);
};
