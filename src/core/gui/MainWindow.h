/*
 * Xournal++
 *
 * The Main window
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>    // for array
#include <atomic>   // for atomic_bool
#include <cstddef>  // for size_t
#include <memory>   // for unique_ptr
#include <string>   // for string

#include <gdk/gdk.h>      // for GdkDragContext, GdkEvent
#include <glib-object.h>  // for GClosure
#include <glib.h>         // for gpointer, gboolean, gint
#include <gtk/gtk.h>      // for GtkWidget, GtkCheckMenu...

#include "util/Point.h"
#include "util/raii/GObjectSPtr.h"

#include "GladeGui.h"            // for GladeGui
#include "ToolbarDefinitions.h"  // for TOOLBAR_DEFINITIONS_LEN

class Control;
class PositionInputData;
class Layout;
class SpinPageAdapter;
class ScrollHandling;
class ToolMenuHandler;
class ToolbarData;
class ToolbarModel;
class XournalView;
class PdfFloatingToolbox;
class FloatingToolbox;
class GladeSearchpath;

class Menubar;

typedef std::array<xoj::util::WidgetSPtr, TOOLBAR_DEFINITIONS_LEN> ToolbarWidgetArray;

class MainWindow: public GladeGui {
public:
    MainWindow(GladeSearchpath* gladeSearchPath, Control* control, GtkApplication* parent);
    ~MainWindow() override;

    void populate(GladeSearchpath* gladeSearchPath);

public:
    GMenuModel* getMenuModel() const;

    void show(GtkWindow* parent) override;

    void toolbarSelected(const std::string& id);
    void toolbarSelected(ToolbarData* d);
    ToolbarData* getSelectedToolbar() const;

private:
    const ToolbarData* clearToolbar();
    void loadToolbar(ToolbarData* d);

public:
    /**
     * reloadToolbars reloads the currently selected toolbar
     *
     * This is especially useful when a change in the SettingsDialog should be reflected right away
     */
    void reloadToolbars();

    void updatePageNumbers(size_t page, size_t pagecount, size_t pdfpage);

    void setMaximized(bool maximized);
    bool isMaximized() const;

    void setFullscreen(bool enabled) const;

    bool isDarkTheme() const;

    XournalView* getXournal() const;

    void setMenubarVisible(bool visible);
    void setSidebarVisible(bool visible);
    void setToolbarVisible(bool visible);

    Control* getControl() const;

    PdfFloatingToolbox* getPdfToolbox() const;
    FloatingToolbox* getFloatingToolbox() const;

    void updateScrollbarSidebarPosition();

    void setUndoDescription(const std::string& description);
    void setRedoDescription(const std::string& description);

    ToolbarModel* getToolbarModel() const;
    ToolMenuHandler* getToolMenuHandler() const;

    void setDynamicallyGeneratedSubmenuDisabled(bool disabled);

    void updateToolbarMenu();
    void updateColorscheme();

    const ToolbarWidgetArray& getToolbarWidgets() const;
    const char* getToolbarName(GtkToolbar* toolbar) const;

    Layout* getLayout() const;

    [[maybe_unused]] Menubar* getMenubar() const;

    /**
     * Get the zoom window drawing area widget (may be nullptr if not initialized)
     */
    GtkWidget* getZoomWindowDrawingArea() const;

    /**
     * Get the position of the top left corner of screen (X11) or the window (Wayland)
     * relative to the Xournal Widget top left corner
     *
     * @see Util::toWidgetCoords()
     */
    xoj::util::Point<double> getNegativeXournalWidgetPos() const;

    /**
     * Disable kinetic scrolling if there is a touchscreen device that was manually mapped to another enabled input
     * device class. This is required so the GtkScrolledWindow does not swallow all the events.
     */
    void setGtkTouchscreenScrollingForDeviceMapping();
    void setGtkTouchscreenScrollingEnabled(bool enabled);

    /// Infer the window's DPI from available monitor info and use it to set the default zoom value.
    void setDPI() const;

private:
    void initXournalWidget();

    void createToolbar();

    /**
     * Update the position of the separator in the paned container, adjusting it to the saved sidebar width.
     * @param contentWidth should be the width of the paned container. The caller should retrieve the width
     * of the container before any modifications to it, as that will reset its allocation.
     */
    void updatePanedPosition(int contentWidth);

    /**
     * Window close Button is pressed
     */
    static bool deleteEventCallback(GtkWidget* widget, GdkEvent* event, Control* control);

    /**
     * Window is maximized/minimized
     */
    static void windowMaximizedCallback(GObject* window, GParamSpec*, MainWindow* win);

    /**
     * Callback for drag & drop files
     */
    static void dragDataRecived(GtkWidget* widget, GdkDragContext* dragContext, gint x, gint y, GtkSelectionData* data,
                                guint info, guint time, MainWindow* win);

    /**
     * Load Overall CSS file with custom icons, other styling and potentially, user changes
     */
    static void loadMainCSS(GladeSearchpath* gladeSearchPath, const gchar* cssFilename);

private:
    Control* control;

    std::unique_ptr<XournalView> xournal;
    GtkWidget* winXournal = nullptr;
    std::unique_ptr<ScrollHandling> scrollHandling;

    std::atomic_bool gtkTouchscreenScrollingEnabled{true};

    std::unique_ptr<PdfFloatingToolbox> pdfFloatingToolBox;
    std::unique_ptr<FloatingToolbox> floatingToolbox;

    // Toolbars
    std::unique_ptr<ToolMenuHandler> toolbar;
    ToolbarData* selectedToolbar = nullptr;

    std::unique_ptr<Menubar> menubar;

    bool maximized = false;
    bool darkMode = false;
    bool modifiedGtkSettingsTheme = false;

    ToolbarWidgetArray toolbarWidgets;

    bool sidebarVisible = true;

    xoj::util::WidgetSPtr boxContainerWidget;
    xoj::util::WidgetSPtr panedContainerWidget;
    xoj::util::WidgetSPtr mainContentWidget;
    xoj::util::WidgetSPtr sidebarWidget;

    // Zoom window for displaying a scaled view of the canvas
    GtkWidget* zoomWindowDrawingArea = nullptr;
    GtkWidget* zoomWindowFrame = nullptr;
    GtkWidget* zoomWindowBtnMinimize = nullptr;
    GtkWidget* zoomWindowBtnMaximize = nullptr;
    GtkWidget* zoomWindowBtnFocusZoom = nullptr;
    GtkWidget* zoomWindowBtnFocusAll = nullptr;
    GtkWidget* zoomWindowBtnDrag = nullptr;
    bool zoomWindowFocusMode = true;  // true = zoom window only, false = full window
    
    // Drag button state for moving the indicator
    bool zoomWindowDragging = false;
    double zoomWindowDragStartX = 0.0;  // Click position (reference center)
    double zoomWindowDragStartY = 0.0;
    double zoomWindowDragCurrentX = 0.0;  // Current mouse position
    double zoomWindowDragCurrentY = 0.0;
    guint zoomWindowDragTimerId = 0;  // Timer for continuous movement
    
    // Zoom window transformation parameters (for input handling)
    double zoomWindowScale = 5.0;       // Magnification factor
    double zoomWindowIndicatorX = 0.0;   // Top-left X of visible area in page display coords
    double zoomWindowIndicatorY = 0.0;   // Top-left Y of visible area in page display coords
    bool zoomWindowInputActive = false;
    
    // Stylus button state for zoom window (for tool changes like eraser)
    bool zoomWindowStylusBtn2 = false;   // Stylus button 1 (modifier2)
    bool zoomWindowStylusBtn3 = false;   // Stylus button 2 (modifier3)
    bool zoomWindowIsEraser = false;     // True if using eraser tip
    
    // Stored indicator position (can be moved with keyboard)
    double zoomIndicatorPosX = 0.0;      // User-controlled X position
    double zoomIndicatorPosY = 0.0;      // User-controlled Y position
    size_t zoomWindowLastPage = static_cast<size_t>(-1);  // Track last rendered page to detect external page changes
    bool zoomWindowInternalPageChange = false;  // Flag to prevent resetting during keyboard page navigation
    
    // Indicator direct dragging (on main view)
    bool indicatorDirectDragging = false;

    /**
     * Initialize the zoom window drawing area
     */
    void initZoomWindow();
    
    /**
     * Load tablet mapping configuration from settings
     */
    void loadTabletMappingConfig();
    
    /**
     * Transform zoom window coordinates to page coordinates
     */
    PositionInputData transformZoomWindowCoords(double x, double y, GdkEvent* event);

    /**
     * Callback for drawing the zoom window content
     */
    static gboolean onZoomWindowDraw(GtkWidget* widget, cairo_t* cr, MainWindow* self);
    
    /**
     * Callback for button press events in the zoom window
     */
    static gboolean onZoomWindowButtonPress(GtkWidget* widget, GdkEventButton* event, MainWindow* self);
    
    /**
     * Callback for button release events in the zoom window
     */
    static gboolean onZoomWindowButtonRelease(GtkWidget* widget, GdkEventButton* event, MainWindow* self);
    
    /**
     * Callback for motion events in the zoom window
     */
    static gboolean onZoomWindowMotion(GtkWidget* widget, GdkEventMotion* event, MainWindow* self);
    
    /**
     * Callback for key press events in the zoom window
     */
    static gboolean onZoomWindowKeyPress(GtkWidget* widget, GdkEventKey* event, MainWindow* self);
};
