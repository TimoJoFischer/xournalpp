#include "MainWindow.h"

#include <algorithm>
#include <cctype>
#include <regex>

#include <gdk-pixbuf/gdk-pixbuf.h>  // for gdk_pixbuf_new_fr...
#include <gdk/gdk.h>                // for gdk_screen_get_de...
#include <gio/gio.h>                // for g_cancellable_is_...
#include <gtk/gtkcssprovider.h>     // for gtk_css_provider_...

#include "control/AudioController.h"                    // for AudioController
#include "control/Control.h"                            // for Control
#include "control/DeviceListHelper.h"                   // for getSourceMapping
#include "control/ScrollHandler.h"                      // for ScrollHandler
#include "control/ToolEnums.h"                          // for TOOL_FLOATING_TOOLBOX
#include "control/ToolHandler.h"                        // for ToolHandler
#include "model/Document.h"                             // for Document
#include "control/actions/ActionDatabase.h"             // for ActionDatabase
#include "control/jobs/XournalScheduler.h"              // for XournalScheduler
#include "control/layer/LayerController.h"              // for LayerController
#include "control/settings/Settings.h"                  // for Settings
#include "control/settings/SettingsEnums.h"             // for SCROLLBAR_HIDE_HO...
#include "control/zoom/ZoomControl.h"                   // for ZoomControl
#include "gui/FloatingToolbox.h"                        // for FloatingToolbox
#include "gui/GladeGui.h"                               // for GladeGui
#include "gui/PdfFloatingToolbox.h"                     // for PdfFloatingToolbox
#include "gui/SearchBar.h"                              // for SearchBar
#include "gui/inputdevices/InputContext.h"              // for InputContext
#include "gui/inputdevices/InputEvents.h"               // for INPUT_DEVICE_TOUC...
#include "gui/inputdevices/InputUtils.h"                // for InputUtils
#include "gui/inputdevices/PositionInputData.h"         // for PositionInputData
#include "gui/inputdevices/DeviceId.h"                  // for DeviceId
#include "gui/menus/menubar/Menubar.h"                  // for Menubar
#include "model/Point.h"                                // for Point::NO_PRESSURE
#include "gui/menus/menubar/ToolbarSelectionSubmenu.h"  // for ToolbarSelectionSubmenu
#include "gui/scroll/ScrollHandling.h"                  // for ScrollHandling
#include "gui/sidebar/Sidebar.h"                        // for Sidebar
#include "gui/toolbarMenubar/ToolMenuHandler.h"         // for ToolMenuHandler
#include "gui/toolbarMenubar/model/ToolbarData.h"       // for ToolbarData
#include "gui/toolbarMenubar/model/ToolbarModel.h"      // for ToolbarModel
#include "gui/widgets/SpinPageAdapter.h"                // for SpinPageAdapter
#include "gui/widgets/XournalWidget.h"                  // for gtk_xournal_get_l...
#include "util/GListView.h"                             // for GListView, GListV...
#include "util/GtkUtil.h"                               // for getWidgetDPI
#include "util/PathUtil.h"                              // for getConfigFile
#include "util/StringUtils.h"                           // for char_cast
#include "util/Util.h"                                  // for execInUiThread, npos
#include "util/XojMsgBox.h"                             // for XojMsgBox
#include "util/glib_casts.h"                            // for wrap_for_once_v
#include "util/gtk4_helper.h"                           // for gtk_widget_get_width
#include "util/i18n.h"                                  // for FS, _F
#include "util/raii/CStringWrapper.h"                   // for OwnedCString
#include "util/TabletMapping.h"                          // for TabletMapping

#include "GladeSearchpath.h"     // for GladeSearchpath
#include "PageView.h"            // for XojPageView
#include "ToolbarDefinitions.h"  // for TOOLBAR_DEFINITIO...
#include "XournalView.h"         // for XournalView
#include "config-dev.h"          // for TOOLBAR_CONFIG
#include "filesystem.h"          // for path, exists

#ifdef __APPLE__
// the following header file contains a definition of struct Point that conflicts with model/Point.h
#define Point Point_CF
#include <CoreFoundation/CoreFoundation.h>
#undef Point
#endif

using std::string;


static void themeCallback(GObject*, GParamSpec*, gpointer data) { static_cast<MainWindow*>(data)->updateColorscheme(); }

MainWindow::MainWindow(GladeSearchpath* gladeSearchPath, Control* control, GtkApplication* parent):
        GladeGui(gladeSearchPath, "main.glade", "mainWindow"),
        control(control),
        toolbar(std::make_unique<ToolMenuHandler>(control, this)),
        menubar(std::make_unique<Menubar>()) {
    gtk_window_set_application(GTK_WINDOW(getWindow()), parent);

    panedContainerWidget.reset(get("panelMainContents"), xoj::util::ref);
    boxContainerWidget.reset(get("mainContentContainer"), xoj::util::ref);
    mainContentWidget.reset(get("boxContents"), xoj::util::ref);
    sidebarWidget.reset(get("sidebar"), xoj::util::ref);

    loadMainCSS(gladeSearchPath, "xournalpp.css");

    GtkOverlay* overlay = GTK_OVERLAY(get("mainOverlay"));
    this->pdfFloatingToolBox = std::make_unique<PdfFloatingToolbox>(this, overlay);
    this->floatingToolbox = std::make_unique<FloatingToolbox>(this, overlay);

    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        this->toolbarWidgets[i].reset(get(TOOLBAR_DEFINITIONS[i].guiName), xoj::util::ref);
    }

    initXournalWidget();
    initZoomWindow();

    setSidebarVisible(control->getSettings()->isSidebarVisible());

    // Window handler
    g_signal_connect(this->window, "delete-event", xoj::util::wrap_for_g_callback_v<deleteEventCallback>,
                     this->control);
#if GTK_MAJOR_VERSION == 3
    g_signal_connect(this->window, "notify::is-maximized", xoj::util::wrap_for_g_callback_v<windowMaximizedCallback>,
                     this);
#else
    g_signal_connect(this->window, "notify::maximized", xoj::util::wrap_for_g_callback_v<windowMaximizedCallback>,
                     this);
#endif

    // Handle zoom window key events at window level (before other handlers)
    g_signal_connect(this->window, "key-press-event", G_CALLBACK(onZoomWindowKeyPress), this);

    // "watch over" all key events
    auto keyPropagate = +[](GtkWidget* w, GdkEvent* e, gpointer) {
        return gtk_window_propagate_key_event(GTK_WINDOW(w), (GdkEventKey*)(e));
    };
    g_signal_connect(this->window, "key-press-event", G_CALLBACK(keyPropagate), nullptr);
    g_signal_connect(this->window, "key-release-event", G_CALLBACK(keyPropagate), nullptr);

    updateScrollbarSidebarPosition();

    gtk_window_set_default_size(GTK_WINDOW(this->window), control->getSettings()->getMainWndWidth(),
                                control->getSettings()->getMainWndHeight());

    if (control->getSettings()->isMainWndMaximized()) {
        gtk_window_maximize(GTK_WINDOW(this->window));
    } else {
        gtk_window_unmaximize(GTK_WINDOW(this->window));
    }

    Util::execInUiThread([=]() {
        // Execute after the window is visible, else the check won't work
        control->setShowMenubar(control->getSettings()->isMenubarVisible());
    });

    // Drag and Drop
    g_signal_connect(this->window, "drag-data-received", G_CALLBACK(dragDataRecived), this);

    gtk_drag_dest_set(this->window, GTK_DEST_DEFAULT_ALL, nullptr, 0, GDK_ACTION_COPY);
    gtk_drag_dest_add_uri_targets(this->window);
    gtk_drag_dest_add_image_targets(this->window);
    gtk_drag_dest_add_text_targets(this->window);

    g_signal_connect(gtk_widget_get_settings(this->window), "notify::gtk-theme-name", G_CALLBACK(themeCallback), this);
    g_signal_connect(gtk_widget_get_settings(this->window), "notify::gtk-application-prefer-dark-theme",
                     G_CALLBACK(themeCallback), this);

    updateColorscheme();
}

void MainWindow::populate(GladeSearchpath* gladeSearchPath) {

    toolbar->populate(gladeSearchPath);
    menubar->populate(gladeSearchPath, this);

    // need to create tool buttons registered in plugins, so they can be added to toolbars
    control->registerPluginToolButtons(this->toolbar.get());

    createToolbar();

    setToolbarVisible(control->getSettings()->isToolbarVisible());
}

GMenuModel* MainWindow::getMenuModel() const { return menubar->getModel(); }

MainWindow::~MainWindow() = default;

struct ThemeProperties {
    bool dark;
    bool darkSuffix;
    std::string rootname;  ///< Name without any putative -dark suffix
};
static ThemeProperties getThemeProperties(GtkWidget* w) {
    xoj::util::OwnedCString name;
    [[maybe_unused]] bool useEnv = false;
    // Gtk prioritizes GTK_THEME over GtkSettings content
    // cf https://gitlab.gnome.org/GNOME/gtk/blob/90d84a2af8b367bd5a5312b3fa3b67563462c0ef/gtk/gtksettings.c#L1567-L1622
    if (auto* p = g_getenv("GTK_THEME")) {
        *(name.contentReplacer()) = g_strdup(p);
        useEnv = true;
    } else {
        g_object_get(gtk_widget_get_settings(w), "gtk-theme-name", name.contentReplacer(), nullptr);
    }

    // Try to figure out if the theme is dark or light
    // Some themes handle their dark variant via "gtk-application-prefer-dark-theme" while other just append "-dark"
    const std::regex nameparser("([a-zA-Z-]+?)([:-][dD]ark)?");
    std::cmatch sm;
    std::regex_match(name.get(), sm, nameparser);

    ThemeProperties props;
    if (sm.size() < 3) {
        g_warning("Fails to extract theme root name from: \"%s\"", name.get());
        props.rootname = name.get();
        props.darkSuffix = false;
    } else {
        props.rootname = sm[1];
        props.darkSuffix = sm[2].length() > 0;
    }
    gboolean dark = false;

#ifdef __APPLE__
    if (!useEnv) {
        CFStringRef interfaceStyle =
                (CFStringRef)CFPreferencesCopyAppValue(CFSTR("AppleInterfaceStyle"), kCFPreferencesCurrentApplication);
        if (interfaceStyle) {
            char buffer[128];
            if (CFStringGetCString(interfaceStyle, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
                std::string style = buffer;
                if (auto pos = style.find("Dark"); pos != std::string::npos) {
                    dark = true;
                }
            }
        }
    }
#else
    g_object_get(gtk_widget_get_settings(w), "gtk-application-prefer-dark-theme", &dark, nullptr);
#endif

    g_debug("Extracted theme info: Name = %s, rootname = %s, dark = %s", name.get(), props.rootname.c_str(),
            dark ? "true" : "false");

    props.dark = props.darkSuffix || dark;  // Some themes handle their dark variant via this setting

    return props;
}

void MainWindow::updateColorscheme() {
    g_signal_handlers_block_by_func(gtk_widget_get_settings(this->window),
                                    reinterpret_cast<gpointer>(G_CALLBACK(themeCallback)), this);
    auto variant = control->getSettings()->getThemeVariant();
    if (variant == THEME_VARIANT_USE_SYSTEM) {
        gtk_settings_reset_property(gtk_widget_get_settings(this->window), "gtk-application-prefer-dark-theme");
        if (modifiedGtkSettingsTheme) {
            // Some bug in Gtk makes an infinite loop despite us blocking the signals
            gtk_settings_reset_property(gtk_widget_get_settings(this->window), "gtk-theme-name");
            modifiedGtkSettingsTheme = false;
        }
    }
    auto props = getThemeProperties(this->window);

    this->darkMode = (props.dark && variant != THEME_VARIANT_FORCE_LIGHT) || variant == THEME_VARIANT_FORCE_DARK;

    // Set up icons
    {
        const auto uiPath = this->getGladeSearchPath()->getFirstSearchPath();
        const auto lightColorIcons = (uiPath / "iconsColor-light");
        const auto darkColorIcons = (uiPath / "iconsColor-dark");
        const auto lightLucideIcons = (uiPath / "iconsLucide-light");
        const auto darkLucideIcons = (uiPath / "iconsLucide-dark");

        // icon load order from lowest priority to highest priority
        std::vector<fs::path> iconLoadOrder = {};
        const auto chosenTheme = control->getSettings()->getIconTheme();
        switch (chosenTheme) {
            case ICON_THEME_COLOR:
                iconLoadOrder = {darkLucideIcons, lightLucideIcons, darkColorIcons, lightColorIcons};
                break;
            case ICON_THEME_LUCIDE:
                iconLoadOrder = {darkColorIcons, lightColorIcons, darkLucideIcons, lightLucideIcons};
                break;
            default:
                g_message("Unknown icon theme!");
        }

        if (this->darkMode) {
            for (size_t i = 0; 2 * i + 1 < iconLoadOrder.size(); ++i) {
                std::swap(iconLoadOrder[2 * i], iconLoadOrder[2 * i + 1]);
            }
        }

        for (auto& p: iconLoadOrder) {
            gtk_icon_theme_prepend_search_path(gtk_icon_theme_get_default(), char_cast(p.u8string().c_str()));
        }
    }

    GtkStyleContext* context = gtk_widget_get_style_context(GTK_WIDGET(this->window));

    if (this->darkMode) {
        gtk_style_context_add_class(context, "darkMode");
        g_object_set(gtk_widget_get_settings(this->window), "gtk-application-prefer-dark-theme", true, nullptr);
    } else {
        gtk_style_context_remove_class(context, "darkMode");
        g_object_set(gtk_widget_get_settings(this->window), "gtk-application-prefer-dark-theme", false, nullptr);
        if (props.darkSuffix) {  // The active theme is all dark. Remove the trailing "-dark"
            g_object_set(gtk_widget_get_settings(this->window), "gtk-theme-name", props.rootname.c_str(), nullptr);
            modifiedGtkSettingsTheme = true;
        }
    }

    {
        gchar* name = nullptr;
        g_object_get(gtk_widget_get_settings(this->window), "gtk-theme-name", &name, nullptr);
        g_debug("Theme name: %s", name);
        g_debug("Modified in GtkSettings: %s", modifiedGtkSettingsTheme ? "true" : "false");
        g_free(name);
        gboolean gtkdark = true;
        g_object_get(gtk_widget_get_settings(this->window), "gtk-application-prefer-dark-theme", &gtkdark, nullptr);
        g_debug("Theme variant: %s", gtkdark ? "dark" : "light");
        g_debug("Icon theme: %s", iconThemeToString(control->getSettings()->getIconTheme()));
    }
    g_signal_handlers_unblock_by_func(gtk_widget_get_settings(this->window), reinterpret_cast<gpointer>(themeCallback),
                                      this);
}

void MainWindow::initXournalWidget() {
    winXournal = gtk_scrolled_window_new();

    setGtkTouchscreenScrollingForDeviceMapping();

    gtk_box_append(GTK_BOX(get("boxContents")), winXournal);

    GtkWidget* vpXournal = gtk_viewport_new(nullptr, nullptr);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(winXournal), vpXournal);

    scrollHandling = std::make_unique<ScrollHandling>(GTK_SCROLLED_WINDOW(winXournal));

    this->xournal = std::make_unique<XournalView>(vpXournal, control, scrollHandling.get());

    control->getZoomControl()->initZoomHandler(this->window, winXournal, xournal.get(), control);
    gtk_widget_show_all(winXournal);

    scrollHandling->init(this->xournal->getWidget(), this->xournal->getLayout());
    
    // Set up pre-event handler for zoom indicator dragging
    // This runs before normal input handling in InputContext
    GtkXournal* xoj = GTK_XOURNAL(this->xournal->getWidget());
    if (xoj && xoj->input) {
        xoj->input->setPreEventHandler(
            [](GdkEvent* event, void* userData) -> bool {
                auto* self = static_cast<MainWindow*>(userData);
                GtkWidget* widget = self->xournal->getWidget();
                
                // Only handle if zoom window is visible
                if (!self->zoomWindowFrame || !gtk_widget_get_visible(self->zoomWindowFrame)) {
                    return false;
                }
                
                GdkEventType type = gdk_event_get_event_type(event);
                
                if (type == GDK_BUTTON_PRESS) {
                    GdkEventButton* btnEvent = reinterpret_cast<GdkEventButton*>(event);
                    if (btnEvent->button == 1) {  // Left click only
                        // Check if click is on the indicator's drag handle (bottom-right corner)
                        if (gtk_xournal_point_in_indicator_corner(widget, btnEvent->x, btnEvent->y)) {
                            gtk_xournal_start_indicator_drag(widget, btnEvent->x, btnEvent->y);
                            self->indicatorDirectDragging = true;
                            return true;  // Consume the event
                        }
                    }
                } else if (type == GDK_MOTION_NOTIFY && self->indicatorDirectDragging) {
                    GdkEventMotion* motionEvent = reinterpret_cast<GdkEventMotion*>(event);
                    
                    GtkXournal* xoj = GTK_XOURNAL(widget);
                    
                    // Calculate new indicator position from mouse position and drag offset
                    double newIndicatorX = motionEvent->x - xoj->indicatorDragOffsetX;
                    double newIndicatorY = motionEvent->y - xoj->indicatorDragOffsetY;
                    
                    // Get zoom window dimensions from settings
                    double zoomFactor = self->getZoomWindowFactor();
                    int defaultWidth, defaultHeight;
                    self->getZoomWindowSize(defaultWidth, defaultHeight);
                    int zoomWidth = gtk_widget_get_allocated_width(self->zoomWindowDrawingArea);
                    int zoomHeight = gtk_widget_get_allocated_height(self->zoomWindowDrawingArea);
                    if (zoomWidth <= 0) zoomWidth = defaultWidth;
                    if (zoomHeight <= 0) zoomHeight = defaultHeight;
                    double visibleWidth = zoomWidth / zoomFactor;
                    double visibleHeight = zoomHeight / zoomFactor;
                    
                    // Get current page info
                    size_t currentPage = self->xournal->getCurrentPage();
                    
                    if (currentPage == npos) {
                        return true;
                    }
                    
                    XojPageView* currentPageView = self->xournal->getViewFor(currentPage);
                    if (!currentPageView) {
                        return true;
                    }
                    
                    // Convert to page-relative coordinates
                    double pageRelX = newIndicatorX - currentPageView->getX();
                    double pageRelY = newIndicatorY - currentPageView->getY();
                    
                    // Get current page bounds
                    double pageDisplayWidth = currentPageView->getDisplayWidthDouble();
                    double pageDisplayHeight = currentPageView->getDisplayHeightDouble();
                    double maxX = std::max(0.0, pageDisplayWidth - visibleWidth);
                    double maxY = std::max(0.0, pageDisplayHeight - visibleHeight);
                    
                    // Simply clamp position within current page bounds
                    self->zoomIndicatorPosX = std::max(0.0, std::min(pageRelX, maxX));
                    self->zoomIndicatorPosY = std::max(0.0, std::min(pageRelY, maxY));
                    
                    // Update widget indicator position
                    xoj->zoomIndicatorX = currentPageView->getX() + self->zoomIndicatorPosX;
                    xoj->zoomIndicatorY = currentPageView->getY() + self->zoomIndicatorPosY;
                    
                    // Scroll main view to keep indicator visible
                    self->xournal->ensureRectIsVisible(
                        static_cast<int>(xoj->zoomIndicatorX),
                        static_cast<int>(xoj->zoomIndicatorY),
                        static_cast<int>(visibleWidth),
                        static_cast<int>(visibleHeight));
                    
                    // Redraw the zoom window
                    gtk_widget_queue_draw(self->zoomWindowDrawingArea);
                    gtk_widget_queue_draw(widget);
                    
                    return true;  // Consume the event
                } else if (type == GDK_BUTTON_RELEASE && self->indicatorDirectDragging) {
                    GdkEventButton* btnEvent = reinterpret_cast<GdkEventButton*>(event);
                    if (btnEvent->button == 1) {
                        gtk_xournal_stop_indicator_drag(widget);
                        self->indicatorDirectDragging = false;
                        return true;  // Consume the event
                    }
                }
                
                return false;  // Let normal handlers process the event
            }, this);
    }
}

void MainWindow::initZoomWindow() {
    zoomWindowDrawingArea = get("zoomWindowDrawingArea");
    zoomWindowFrame = get("zoomWindowFrame");
    zoomWindowBtnMinimize = get("zoomWindowBtnMinimize");
    zoomWindowBtnMaximize = get("zoomWindowBtnMaximize");
    zoomWindowBtnFocusZoom = get("zoomWindowBtnFocusZoom");
    zoomWindowBtnFocusAll = get("zoomWindowBtnFocusAll");
    zoomWindowBtnDrag = get("zoomWindowBtnDrag");
    
    // Set zoom window size from settings
    int zoomWidth, zoomHeight;
    getZoomWindowSize(zoomWidth, zoomHeight);
    if (zoomWindowDrawingArea) {
        gtk_widget_set_size_request(zoomWindowDrawingArea, zoomWidth, zoomHeight);
    }
    if (zoomWindowFrame) {
        // Frame is slightly taller to accommodate the toolbar
        gtk_widget_set_size_request(zoomWindowFrame, zoomWidth, zoomHeight + 10);
    }
    
    // Load tablet mapping configuration from settings
    loadTabletMappingConfig();
    
    // Apply full window tablet mapping at startup
    // This ensures the tablet is mapped to the full window when Xournal++ starts
    if (TabletMapping::isAvailable()) {
        TabletMapping::setMappingMode(TabletMapping::MappingMode::FullWindow);
        g_message("TabletMapping: Applied full window mapping at startup");
    }
    
    // Set up minimize button
    if (zoomWindowBtnMinimize && zoomWindowFrame && zoomWindowBtnMaximize) {
        g_signal_connect(zoomWindowBtnMinimize, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* self = static_cast<MainWindow*>(data);
            gtk_widget_hide(self->zoomWindowFrame);
            gtk_widget_show(self->zoomWindowBtnMaximize);
            // Hide the indicator on the main canvas
            if (self->xournal) {
                gtk_xournal_set_zoom_indicator(self->xournal->getWidget(), false, 0, 0, 0, 0);
            }
            // Map tablet to full window when zoom window is minimized
            TabletMapping::setMappingMode(TabletMapping::MappingMode::FullWindow);
        }), this);
    }
    
    // Set up maximize button
    if (zoomWindowBtnMaximize && zoomWindowFrame) {
        g_signal_connect(zoomWindowBtnMaximize, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* self = static_cast<MainWindow*>(data);
            gtk_widget_show(self->zoomWindowFrame);
            gtk_widget_hide(self->zoomWindowBtnMaximize);
            // Map tablet to zoom window when zoom window is shown (if focus mode is zoom)
            if (self->zoomWindowFocusMode) {
                TabletMapping::setMappingMode(TabletMapping::MappingMode::ZoomWindow);
            }
        }), this);
    }
    
    // Set up focus toggle buttons (radio-button style behavior)
    if (zoomWindowBtnFocusZoom && zoomWindowBtnFocusAll) {
        g_signal_connect(zoomWindowBtnFocusZoom, "toggled", G_CALLBACK(+[](GtkToggleButton* btn, gpointer data) {
            auto* self = static_cast<MainWindow*>(data);
            if (gtk_toggle_button_get_active(btn)) {
                self->zoomWindowFocusMode = true;
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->zoomWindowBtnFocusAll), FALSE);
                // Map tablet input to zoom window only
                TabletMapping::setMappingMode(TabletMapping::MappingMode::ZoomWindow);
            }
        }), this);
        
        g_signal_connect(zoomWindowBtnFocusAll, "toggled", G_CALLBACK(+[](GtkToggleButton* btn, gpointer data) {
            auto* self = static_cast<MainWindow*>(data);
            if (gtk_toggle_button_get_active(btn)) {
                self->zoomWindowFocusMode = false;
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->zoomWindowBtnFocusZoom), FALSE);
                // Map tablet input to full window
                TabletMapping::setMappingMode(TabletMapping::MappingMode::FullWindow);
            }
        }), this);
    }
    
    // Set up drag button for moving the indicator
    if (zoomWindowBtnDrag) {
        gtk_widget_add_events(zoomWindowBtnDrag, 
                              GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | 
                              GDK_POINTER_MOTION_MASK | GDK_BUTTON_MOTION_MASK);
        
        auto dragBtnPress = +[](GtkWidget*, GdkEventButton* event, gpointer data) -> gboolean {
            auto* self = static_cast<MainWindow*>(data);
            if (event->button == 1) {
                self->zoomWindowDragging = true;
                // Store the click position as the reference center
                self->zoomWindowDragStartX = event->x_root;
                self->zoomWindowDragStartY = event->y_root;
                self->zoomWindowDragCurrentX = event->x_root;
                self->zoomWindowDragCurrentY = event->y_root;
                
                // Start the timer for continuous movement (30ms interval for smooth fast movement)
                if (self->zoomWindowDragTimerId == 0) {
                    self->zoomWindowDragTimerId = g_timeout_add(30, +[](gpointer data) -> gboolean {
                        auto* self = static_cast<MainWindow*>(data);
                        if (!self->zoomWindowDragging || !self->xournal) {
                            self->zoomWindowDragTimerId = 0;
                            return FALSE;
                        }
                        
                        // Calculate offset from click position to current mouse position
                        double offsetX = self->zoomWindowDragCurrentX - self->zoomWindowDragStartX;
                        double offsetY = self->zoomWindowDragCurrentY - self->zoomWindowDragStartY;
                        
                        // Check if within dead zone (circular, small radius)
                        double distance = std::sqrt(offsetX * offsetX + offsetY * offsetY);
                        double deadZone = 3.0;  // pixels - no movement if within this radius
                        double slowZone = 10.0; // pixels - slower speed between deadZone and slowZone
                        
                        if (distance < deadZone) {
                            // Too close to center, no movement
                            return TRUE;
                        }
                        
                        // Normalize direction vector to length 1 (preserves angle)
                        double dirX = offsetX / distance;
                        double dirY = offsetY / distance;
                        
                        // Variable speed based on distance from center
                        double speed;
                        if (distance < slowZone) {
                            // Slower speed in the 3-10px range
                            speed = 5.0;
                        } else {
                            // Max speed beyond 10px
                            speed = 15.0;
                        }
                        double moveX = dirX * speed;
                        double moveY = dirY * speed;
                        
                        size_t currentPage = self->xournal->getCurrentPage();
                        if (currentPage != npos) {
                            XojPageView* pageView = self->xournal->getViewFor(currentPage);
                            if (pageView) {
                                double pageDisplayWidth = pageView->getDisplayWidthDouble();
                                double pageDisplayHeight = pageView->getDisplayHeightDouble();
                                
                                // Get zoom window dimensions from settings
                                double zoomFactor = self->getZoomWindowFactor();
                                int defaultWidth, defaultHeight;
                                self->getZoomWindowSize(defaultWidth, defaultHeight);
                                int zoomWidth = gtk_widget_get_allocated_width(self->zoomWindowDrawingArea);
                                int zoomHeight = gtk_widget_get_allocated_height(self->zoomWindowDrawingArea);
                                if (zoomWidth <= 0) zoomWidth = defaultWidth;
                                if (zoomHeight <= 0) zoomHeight = defaultHeight;
                                
                                double visibleWidth = zoomWidth / zoomFactor;
                                double visibleHeight = zoomHeight / zoomFactor;
                                
                                double maxX = std::max(0.0, pageDisplayWidth - visibleWidth);
                                double maxY = std::max(0.0, pageDisplayHeight - visibleHeight);
                                
                                double newPosX = self->zoomIndicatorPosX + moveX;
                                double newPosY = self->zoomIndicatorPosY + moveY;
                                
                                // Simply clamp to current page bounds - no page navigation
                                self->zoomIndicatorPosX = std::max(0.0, std::min(newPosX, maxX));
                                self->zoomIndicatorPosY = std::max(0.0, std::min(newPosY, maxY));
                                
                                // Scroll main view to keep indicator visible
                                int pageX = pageView->getX();
                                int pageY = pageView->getY();
                                int indicatorAbsX = pageX + static_cast<int>(self->zoomIndicatorPosX);
                                int indicatorAbsY = pageY + static_cast<int>(self->zoomIndicatorPosY);
                                self->xournal->ensureRectIsVisible(indicatorAbsX, indicatorAbsY,
                                                                   static_cast<int>(visibleWidth),
                                                                   static_cast<int>(visibleHeight));
                                
                                gtk_widget_queue_draw(self->zoomWindowDrawingArea);
                            }
                        }
                        return TRUE;
                    }, self);
                }
                return TRUE;
            }
            return FALSE;
        };
        g_signal_connect(zoomWindowBtnDrag, "button-press-event", G_CALLBACK(dragBtnPress), this);
        
        auto dragBtnRelease = +[](GtkWidget*, GdkEventButton* event, gpointer data) -> gboolean {
            auto* self = static_cast<MainWindow*>(data);
            if (event->button == 1 && self->zoomWindowDragging) {
                self->zoomWindowDragging = false;
                // Timer will stop itself on next tick
                return TRUE;
            }
            return FALSE;
        };
        g_signal_connect(zoomWindowBtnDrag, "button-release-event", G_CALLBACK(dragBtnRelease), this);
        
        auto dragBtnMotion = +[](GtkWidget*, GdkEventMotion* event, gpointer data) -> gboolean {
            auto* self = static_cast<MainWindow*>(data);
            if (!self->zoomWindowDragging) {
                return FALSE;
            }
            // Just update the current mouse position, timer will handle movement
            self->zoomWindowDragCurrentX = event->x_root;
            self->zoomWindowDragCurrentY = event->y_root;
            return TRUE;
        };
        g_signal_connect(zoomWindowBtnDrag, "motion-notify-event", G_CALLBACK(dragBtnMotion), this);
    }
    
    // Set up page up button
    GtkWidget* zoomWindowBtnPageUp = get("zoomWindowBtnPageUp");
    if (zoomWindowBtnPageUp) {
        g_signal_connect(zoomWindowBtnPageUp, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* self = static_cast<MainWindow*>(data);
            if (!self->xournal || !self->control) {
                return;
            }
            size_t currentPage = self->xournal->getCurrentPage();
            if (currentPage > 0) {
                self->zoomWindowInternalPageChange = true;
                self->zoomIndicatorPosX = 0;
                self->zoomIndicatorPosY = 0;
                self->control->getScrollHandler()->scrollToPage(currentPage - 1, XojPdfRectangle{});
            }
        }), this);
    }
    
    // Set up page down button
    GtkWidget* zoomWindowBtnPageDown = get("zoomWindowBtnPageDown");
    if (zoomWindowBtnPageDown) {
        g_signal_connect(zoomWindowBtnPageDown, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* self = static_cast<MainWindow*>(data);
            if (!self->xournal || !self->control) {
                return;
            }
            size_t currentPage = self->xournal->getCurrentPage();
            size_t pageCount = self->control->getDocument()->getPageCount();
            if (currentPage < pageCount - 1) {
                self->zoomWindowInternalPageChange = true;
                self->zoomIndicatorPosX = 0;
                self->zoomIndicatorPosY = 0;
                self->control->getScrollHandler()->scrollToPage(currentPage + 1, XojPdfRectangle{});
            }
        }), this);
    }
    
    if (zoomWindowDrawingArea) {
        g_signal_connect(zoomWindowDrawingArea, "draw", G_CALLBACK(onZoomWindowDraw), this);
        
        // Enable events for input handling
        gtk_widget_add_events(zoomWindowDrawingArea, 
                              GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | 
                              GDK_POINTER_MOTION_MASK | GDK_BUTTON_MOTION_MASK |
                              GDK_KEY_PRESS_MASK | GDK_ENTER_NOTIFY_MASK);
        
        // Make the widget focusable to receive keyboard events
        gtk_widget_set_can_focus(zoomWindowDrawingArea, TRUE);
        
        // Connect input event handlers
        g_signal_connect(zoomWindowDrawingArea, "button-press-event", G_CALLBACK(onZoomWindowButtonPress), this);
        g_signal_connect(zoomWindowDrawingArea, "button-release-event", G_CALLBACK(onZoomWindowButtonRelease), this);
        g_signal_connect(zoomWindowDrawingArea, "motion-notify-event", G_CALLBACK(onZoomWindowMotion), this);
        g_signal_connect(zoomWindowDrawingArea, "key-press-event", G_CALLBACK(onZoomWindowKeyPress), this);
        
        // Sync cursor from main view when entering the zoom window
        g_signal_connect(zoomWindowDrawingArea, "enter-notify-event", G_CALLBACK(+[](GtkWidget* widget, GdkEventCrossing*, gpointer data) -> gboolean {
            auto* self = static_cast<MainWindow*>(data);
            if (self->xournal) {
                GdkWindow* mainWindow = gtk_widget_get_window(self->xournal->getWidget());
                GdkWindow* zoomWindow = gtk_widget_get_window(widget);
                if (mainWindow && zoomWindow) {
                    GdkCursor* cursor = gdk_window_get_cursor(mainWindow);
                    gdk_window_set_cursor(zoomWindow, cursor);
                }
            }
            return FALSE;
        }), this);
        
        // Set up a timer to periodically refresh the zoom window and update the indicator
        g_timeout_add(100, +[](gpointer data) -> gboolean {
            auto* self = static_cast<MainWindow*>(data);
            // Check if zoom window frame is visible (not minimized)
            bool zoomVisible = self->zoomWindowFrame && gtk_widget_get_visible(self->zoomWindowFrame) &&
                               self->zoomWindowDrawingArea && gtk_widget_get_visible(self->zoomWindowDrawingArea);
            if (zoomVisible) {
                gtk_widget_queue_draw(self->zoomWindowDrawingArea);
                
                // Update the zoom indicator rectangle on the main canvas
                if (self->xournal) {
                    size_t currentPage = self->xournal->getCurrentPage();
                    if (currentPage != npos) {
                        XojPageView* pageView = self->xournal->getViewFor(currentPage);
                        if (pageView) {
                            double pageDisplayWidth = pageView->getDisplayWidthDouble();
                            double pageDisplayHeight = pageView->getDisplayHeightDouble();
                            
                            // Zoom window dimensions from settings (use defaults if not yet allocated)
                            double zoomFactor = self->getZoomWindowFactor();
                            int defaultWidth, defaultHeight;
                            self->getZoomWindowSize(defaultWidth, defaultHeight);
                            int zoomWidth = gtk_widget_get_allocated_width(self->zoomWindowDrawingArea);
                            int zoomHeight = gtk_widget_get_allocated_height(self->zoomWindowDrawingArea);
                            if (zoomWidth <= 0) zoomWidth = defaultWidth;
                            if (zoomHeight <= 0) zoomHeight = defaultHeight;
                            
                            // Apply magnification factor
                            // The visible area in page display coordinates is smaller than the zoom window size
                            double indicatorWidth = static_cast<double>(zoomWidth) / zoomFactor;
                            double indicatorHeight = static_cast<double>(zoomHeight) / zoomFactor;
                            
                            // Use stored position (can be moved with arrow keys)
                            double indicatorX = self->zoomIndicatorPosX;
                            double indicatorY = self->zoomIndicatorPosY;
                            
                            // Clamp to page bounds
                            indicatorX = std::max(0.0, std::min(indicatorX, pageDisplayWidth - indicatorWidth));
                            indicatorY = std::max(0.0, std::min(indicatorY, pageDisplayHeight - indicatorHeight));
                            
                            // Add page offset for drawing on the xournal widget
                            double drawX = indicatorX + pageView->getX();
                            double drawY = indicatorY + pageView->getY();
                            
                            gtk_xournal_set_zoom_indicator(self->xournal->getWidget(), true,
                                                          drawX, drawY, indicatorWidth, indicatorHeight);
                        }
                    }
                }
            } else {
                // Hide the indicator when zoom window is not visible
                if (self->xournal) {
                    gtk_xournal_set_zoom_indicator(self->xournal->getWidget(), false, 0, 0, 0, 0);
                }
            }
            return G_SOURCE_CONTINUE;
        }, this);
    }
}

gboolean MainWindow::onZoomWindowDraw(GtkWidget* widget, cairo_t* cr, MainWindow* self) {
    if (!self->xournal) {
        return FALSE;
    }

    // Get the current page
    size_t currentPage = self->xournal->getCurrentPage();
    if (currentPage == npos) {
        return FALSE;
    }

    // Reset indicator to top-left when page changes externally (not via our navigation)
    bool needsScrollUpdate = false;
    if (self->zoomWindowLastPage != currentPage) {
        if (!self->zoomWindowInternalPageChange) {
            // External page change - reset to top-left
            self->zoomIndicatorPosX = 0.0;
            self->zoomIndicatorPosY = 0.0;
        } else {
            // Internal page change - we need to scroll the main view to show the indicator
            needsScrollUpdate = true;
        }
        self->zoomWindowLastPage = currentPage;
        self->zoomWindowInternalPageChange = false;  // Clear the flag
    }

    XojPageView* pageView = self->xournal->getViewFor(currentPage);
    if (!pageView) {
        return FALSE;
    }

    // Get the dimensions of the zoom window
    int zoomWidth = gtk_widget_get_allocated_width(widget);
    int zoomHeight = gtk_widget_get_allocated_height(widget);

    // Get the page dimensions (display size includes current xournal zoom)
    double pageDisplayWidth = pageView->getDisplayWidthDouble();
    double pageDisplayHeight = pageView->getDisplayHeightDouble();
    double xournalZoom = self->xournal->getZoom();

    if (pageDisplayWidth <= 0 || pageDisplayHeight <= 0) {
        return FALSE;
    }

    // Apply magnification factor for the zoom window from settings
    double zoomFactor = self->getZoomWindowFactor();
    double visibleWidth = zoomWidth / zoomFactor;
    double visibleHeight = zoomHeight / zoomFactor;

    // Use stored indicator position (can be moved with arrow keys)
    double indicatorX = self->zoomIndicatorPosX;
    double indicatorY = self->zoomIndicatorPosY;

    // Clamp to page bounds
    indicatorX = std::max(0.0, std::min(indicatorX, pageDisplayWidth - visibleWidth));
    indicatorY = std::max(0.0, std::min(indicatorY, pageDisplayHeight - visibleHeight));
    
    // Update stored position with clamped values (important when navigating to new pages)
    self->zoomIndicatorPosX = indicatorX;
    self->zoomIndicatorPosY = indicatorY;
    
    // Scroll main view to show indicator if we just changed pages internally
    if (needsScrollUpdate) {
        int pageX = pageView->getX();
        int pageY = pageView->getY();
        int indicatorAbsX = pageX + static_cast<int>(indicatorX);
        int indicatorAbsY = pageY + static_cast<int>(indicatorY);
        self->xournal->ensureRectIsVisible(indicatorAbsX, indicatorAbsY, zoomWidth, zoomHeight);
    }

    // Store transformation parameters for input handling
    self->zoomWindowScale = zoomFactor;
    self->zoomWindowIndicatorX = indicatorX;
    self->zoomWindowIndicatorY = indicatorY;

    // Draw background
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    // Calculate the area in page coordinates
    double sourceXPage = indicatorX / xournalZoom;
    double sourceYPage = indicatorY / xournalZoom;

    cairo_save(cr);
    
    // Clip to the zoom window area
    cairo_rectangle(cr, 0, 0, zoomWidth, zoomHeight);
    cairo_clip(cr);
    
    // Render with magnification
    // Apply zoom factor to show content larger
    cairo_scale(cr, zoomFactor * xournalZoom, zoomFactor * xournalZoom);
    cairo_translate(cr, -sourceXPage, -sourceYPage);
    
    // paintPage scales by xournalZoom internally, so we undo the xournalZoom scale
    cairo_scale(cr, 1.0 / xournalZoom, 1.0 / xournalZoom);
    pageView->paintPage(cr, nullptr);
    
    cairo_restore(cr);

    return TRUE;
}

PositionInputData MainWindow::transformZoomWindowCoords(double x, double y, GdkEvent* event) {
    PositionInputData pos = {};
    
    // Transform from zoom window coordinates to page display coordinates
    // Zoom window shows the area starting at (indicatorX, indicatorY) magnified by zoomWindowScale
    // So: zoomWindowCoord / scale + indicatorPos = pageDisplayCoord
    pos.x = x / zoomWindowScale + zoomWindowIndicatorX;
    pos.y = y / zoomWindowScale + zoomWindowIndicatorY;
    pos.pressure = Point::NO_PRESSURE;
    
    GdkDevice* device = gdk_event_get_source_device(event);
    if (device) {
        pos.deviceId = DeviceId(device);
    }
    
    pos.timestamp = gdk_event_get_time(event);
    
    GdkModifierType state;
    if (gdk_event_get_state(event, &state)) {
        pos.state = state;
    } else {
        pos.state = static_cast<GdkModifierType>(0);
    }
    
    return pos;
}

gboolean MainWindow::onZoomWindowButtonPress(GtkWidget* widget, GdkEventButton* event, MainWindow* self) {
    if (!self->xournal) {
        return FALSE;
    }
    
    // Track stylus button state and check for eraser device
    GdkDevice* device = gdk_event_get_source_device(reinterpret_cast<GdkEvent*>(event));
    bool isEraser = device && gdk_device_get_source(device) == GDK_SOURCE_ERASER;
    
    // Handle stylus buttons (2 and 3) for tool changes
    if (event->button == 2) {
        self->zoomWindowStylusBtn2 = true;
    } else if (event->button == 3) {
        self->zoomWindowStylusBtn3 = true;
    }
    
    // Apply tool change based on stylus button or eraser
    ToolHandler* toolHandler = self->control->getToolHandler();
    Settings* settings = self->control->getSettings();
    bool toolChanged = false;
    
    if (self->zoomWindowStylusBtn2) {
        toolChanged = InputUtils::applyButton(toolHandler, settings, Button::BUTTON_STYLUS_ONE);
    } else if (self->zoomWindowStylusBtn3) {
        toolChanged = InputUtils::applyButton(toolHandler, settings, Button::BUTTON_STYLUS_TWO);
    } else if (isEraser) {
        self->zoomWindowIsEraser = true;
        toolChanged = InputUtils::applyButton(toolHandler, settings, Button::BUTTON_ERASER);
    }
    
    if (toolChanged) {
        toolHandler->fireToolChanged();
    }
    
    // Handle stylus button press "in the air" (not pen tip) for tools like floating toolbox
    // This happens when buttons 2 or 3 are pressed without the pen touching the surface
    if (event->button == 2 || event->button == 3) {
        // Check if the tool is floating toolbox - show it at zoom window position
        if (toolHandler->getToolType() == TOOL_FLOATING_TOOLBOX) {
            // Get zoom window position relative to the top-level window
            gint wx = 0, wy = 0;
            gtk_widget_translate_coordinates(widget, gtk_widget_get_toplevel(widget), 
                                             static_cast<gint>(event->x), static_cast<gint>(event->y), 
                                             &wx, &wy);
            self->floatingToolbox->show(wx, wy);
        }
        return TRUE;  // Consume the event but don't start drawing
    }
    
    // Only start drawing on button 1 (pen tip)
    if (event->button != 1) {
        return TRUE;  // Consume the event but don't start drawing
    }
    
    size_t currentPage = self->xournal->getCurrentPage();
    if (currentPage == npos) {
        return FALSE;
    }
    
    XojPageView* pageView = self->xournal->getViewFor(currentPage);
    if (!pageView) {
        return FALSE;
    }
    
    PositionInputData pos = self->transformZoomWindowCoords(event->x, event->y, reinterpret_cast<GdkEvent*>(event));
    
    // Check if the click is within the page bounds
    if (pos.x >= 0 && pos.y >= 0 && 
        pos.x <= pageView->getDisplayWidth() && pos.y <= pageView->getDisplayHeight()) {
        self->zoomWindowInputActive = true;
        pageView->onButtonPressEvent(pos);
        gtk_widget_queue_draw(widget);
        return TRUE;
    }
    
    return FALSE;
}

gboolean MainWindow::onZoomWindowButtonRelease(GtkWidget* widget, GdkEventButton* event, MainWindow* self) {
    if (!self->xournal) {
        return FALSE;
    }
    
    // Handle stylus button release - restore tool to toolbar tool
    ToolHandler* toolHandler = self->control->getToolHandler();
    bool toolChanged = false;
    
    if (event->button == 2) {
        self->zoomWindowStylusBtn2 = false;
        // If no other modifier buttons are pressed, restore toolbar tool
        if (!self->zoomWindowStylusBtn3 && !self->zoomWindowIsEraser) {
            toolChanged = toolHandler->pointActiveToolToToolbarTool();
        }
    } else if (event->button == 3) {
        self->zoomWindowStylusBtn3 = false;
        // If no other modifier buttons are pressed, restore toolbar tool
        if (!self->zoomWindowStylusBtn2 && !self->zoomWindowIsEraser) {
            toolChanged = toolHandler->pointActiveToolToToolbarTool();
        }
    }
    
    // Check if eraser device is no longer being used
    GdkDevice* device = gdk_event_get_source_device(reinterpret_cast<GdkEvent*>(event));
    bool isEraser = device && gdk_device_get_source(device) == GDK_SOURCE_ERASER;
    if (self->zoomWindowIsEraser && !isEraser && event->button == 1) {
        self->zoomWindowIsEraser = false;
        if (!self->zoomWindowStylusBtn2 && !self->zoomWindowStylusBtn3) {
            toolChanged = toolHandler->pointActiveToolToToolbarTool();
        }
    }
    
    if (toolChanged) {
        toolHandler->fireToolChanged();
    }
    
    // Only handle button 1 release for finishing drawing
    if (event->button != 1 || !self->zoomWindowInputActive) {
        return TRUE;  // Consume the event
    }
    
    size_t currentPage = self->xournal->getCurrentPage();
    if (currentPage == npos) {
        self->zoomWindowInputActive = false;
        return FALSE;
    }
    
    XojPageView* pageView = self->xournal->getViewFor(currentPage);
    if (!pageView) {
        self->zoomWindowInputActive = false;
        return FALSE;
    }
    
    PositionInputData pos = self->transformZoomWindowCoords(event->x, event->y, reinterpret_cast<GdkEvent*>(event));
    pageView->onButtonReleaseEvent(pos);
    
    self->zoomWindowInputActive = false;
    gtk_widget_queue_draw(widget);
    return TRUE;
}

gboolean MainWindow::onZoomWindowMotion(GtkWidget* widget, GdkEventMotion* event, MainWindow* self) {
    // Always sync cursor from main view when moving in the zoom window
    if (self->xournal) {
        GdkWindow* mainWindow = gtk_widget_get_window(self->xournal->getWidget());
        GdkWindow* zoomWindow = gtk_widget_get_window(widget);
        if (mainWindow && zoomWindow) {
            GdkCursor* cursor = gdk_window_get_cursor(mainWindow);
            gdk_window_set_cursor(zoomWindow, cursor);
        }
    }
    
    if (!self->xournal || !self->zoomWindowInputActive) {
        return FALSE;
    }
    
    size_t currentPage = self->xournal->getCurrentPage();
    if (currentPage == npos) {
        return FALSE;
    }
    
    XojPageView* pageView = self->xournal->getViewFor(currentPage);
    if (!pageView) {
        return FALSE;
    }
    
    PositionInputData pos = self->transformZoomWindowCoords(event->x, event->y, reinterpret_cast<GdkEvent*>(event));
    pageView->onMotionNotifyEvent(pos);
    
    gtk_widget_queue_draw(widget);
    return TRUE;
}

gboolean MainWindow::onZoomWindowKeyPress(GtkWidget* widget, GdkEventKey* event, MainWindow* self) {
    // Only handle if zoom window is visible
    if (!self->zoomWindowDrawingArea || !gtk_widget_get_visible(self->zoomWindowDrawingArea)) {
        return FALSE;
    }
    
    // Check for tablet mapping toggle shortcut (works regardless of modifier)
    if (self->matchesShortcut(event, "toggleTabletMappingShortcut")) {
        if (TabletMapping::isAvailable()) {
            // Toggle between full window and zoom window mapping
            if (self->zoomWindowFocusMode) {
                // Currently zoom mode, switch to full window
                TabletMapping::setMappingMode(TabletMapping::MappingMode::FullWindow);
                self->zoomWindowFocusMode = false;
                if (self->zoomWindowBtnFocusAll) {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->zoomWindowBtnFocusAll), TRUE);
                }
                g_message("Tablet mapped to full window");
            } else {
                // Currently full window mode, switch to zoom mode
                TabletMapping::setMappingMode(TabletMapping::MappingMode::ZoomWindow);
                self->zoomWindowFocusMode = true;
                if (self->zoomWindowBtnFocusZoom) {
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->zoomWindowBtnFocusZoom), TRUE);
                }
                g_message("Tablet mapped to zoom window");
            }
        }
        return TRUE;
    }
    
    // Get the configured modifier for indicator movement
    GdkModifierType requiredModifier = self->getIndicatorMoveModifier();
    
    // Check if the modifier requirement is met
    bool modifierMet = false;
    if (requiredModifier == 0) {
        // No modifier required - but make sure no modifier is pressed to avoid conflicts
        GdkModifierType relevantMods = static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_MOD1_MASK);
        modifierMet = !(event->state & relevantMods);
    } else {
        modifierMet = (event->state & requiredModifier) != 0;
    }
    
    if (!modifierMet) {
        return FALSE;
    }
    
    // Get current page bounds for clamping
    if (!self->xournal || !self->control) {
        return FALSE;
    }
    
    size_t currentPage = self->xournal->getCurrentPage();
    size_t pageCount = self->control->getDocument()->getPageCount();
    if (currentPage == npos || pageCount == 0) {
        return FALSE;
    }
    
    XojPageView* pageView = self->xournal->getViewFor(currentPage);
    if (!pageView) {
        return FALSE;
    }
    
    double pageDisplayWidth = pageView->getDisplayWidthDouble();
    double pageDisplayHeight = pageView->getDisplayHeightDouble();
    
    // Zoom window dimensions from settings
    int defaultWidth, defaultHeight;
    self->getZoomWindowSize(defaultWidth, defaultHeight);
    int zoomWidth = gtk_widget_get_allocated_width(self->zoomWindowDrawingArea);
    int zoomHeight = gtk_widget_get_allocated_height(self->zoomWindowDrawingArea);
    if (zoomWidth <= 0) zoomWidth = defaultWidth;
    if (zoomHeight <= 0) zoomHeight = defaultHeight;
    
    // Maximum position (so the indicator stays within page bounds)
    double maxX = std::max(0.0, pageDisplayWidth - zoomWidth);
    double maxY = std::max(0.0, pageDisplayHeight - zoomHeight);
    
    // Movement step size in pixels
    // Hold Shift for larger steps (works with any modifier, including when Shift is the movement modifier)
    // When Shift is the movement modifier, steps are always large since Shift is always pressed
    double step = (event->state & GDK_SHIFT_MASK) ? 100.0 : 20.0;
    // Vertical step when wrapping (half the zoom window height)
    double wrapStepY = zoomHeight / 2.0;
    
    bool handled = false;
    bool pageChanged = false;
    
    // Get customizable movement keys
    guint keyLeft = self->getMovementKey("left");
    guint keyRight = self->getMovementKey("right");
    guint keyUp = self->getMovementKey("up");
    guint keyDown = self->getMovementKey("down");
    
    guint pressedKey = gdk_keyval_to_lower(event->keyval);
    
    if (pressedKey == keyLeft) {
            self->zoomIndicatorPosX -= step;
            // Wrap to right side and move up if going past left edge
            if (self->zoomIndicatorPosX < 0) {
                self->zoomIndicatorPosX = maxX;
                self->zoomIndicatorPosY -= wrapStepY;
                // If we're past top, go to previous page (bottom-right)
                if (self->zoomIndicatorPosY < 0 && currentPage > 0) {
                    // Mark as internal page change to prevent draw from resetting position
                    self->zoomWindowInternalPageChange = true;
                    // Use large values - the draw function will clamp to actual page bounds
                    self->zoomIndicatorPosX = 100000.0;
                    self->zoomIndicatorPosY = 100000.0;
                    self->control->getScrollHandler()->scrollToPage(currentPage - 1, XojPdfRectangle{});
                    pageChanged = true;
                }
            }
            handled = true;
    } else if (pressedKey == keyRight) {
            self->zoomIndicatorPosX += step;
            // Wrap to left side and move down if going past right edge
            if (self->zoomIndicatorPosX > maxX) {
                self->zoomIndicatorPosX = 0;
                self->zoomIndicatorPosY += wrapStepY;
                // If we're past bottom, go to next page (top-left)
                if (self->zoomIndicatorPosY > maxY && currentPage < pageCount - 1) {
                    // Mark as internal page change to prevent draw from resetting position
                    self->zoomWindowInternalPageChange = true;
                    self->zoomIndicatorPosX = 0;
                    self->zoomIndicatorPosY = 0;
                    self->control->getScrollHandler()->scrollToPage(currentPage + 1, XojPdfRectangle{});
                    pageChanged = true;
                }
            }
            handled = true;
    } else if (pressedKey == keyUp) {
            self->zoomIndicatorPosY -= step;
            // If past top, go to previous page (bottom, same X)
            if (self->zoomIndicatorPosY < 0 && currentPage > 0) {
                // Mark as internal page change to prevent draw from resetting position
                self->zoomWindowInternalPageChange = true;
                // Use large value - the draw function will clamp to actual page bounds
                self->zoomIndicatorPosY = 100000.0;
                self->control->getScrollHandler()->scrollToPage(currentPage - 1, XojPdfRectangle{});
                pageChanged = true;
            }
            handled = true;
    } else if (pressedKey == keyDown) {
            self->zoomIndicatorPosY += step;
            // If past bottom, go to next page (top, same X)
            if (self->zoomIndicatorPosY > maxY && currentPage < pageCount - 1) {
                // Mark as internal page change to prevent draw from resetting position
                self->zoomWindowInternalPageChange = true;
                self->zoomIndicatorPosY = 0;  // Go to top of next page
                self->control->getScrollHandler()->scrollToPage(currentPage + 1, XojPdfRectangle{});
                pageChanged = true;
            }
            handled = true;
    } else if (pressedKey == self->getMovementKey("home")) {
            // Jump to top-left
            self->zoomIndicatorPosX = 0;
            self->zoomIndicatorPosY = 0;
            handled = true;
    } else if (pressedKey == self->getMovementKey("end")) {
            // Jump to bottom-right
            self->zoomIndicatorPosX = maxX;
            self->zoomIndicatorPosY = maxY;
            handled = true;
    } else if (pressedKey == self->getMovementKey("pageUp")) {
            // Go to previous page
            if (currentPage > 0) {
                self->zoomWindowInternalPageChange = true;
                self->zoomIndicatorPosX = 0;
                self->zoomIndicatorPosY = 0;
                self->control->getScrollHandler()->scrollToPage(currentPage - 1, XojPdfRectangle{});
                pageChanged = true;
            }
            handled = true;
    } else if (pressedKey == self->getMovementKey("pageDown")) {
            // Go to next page
            if (currentPage < pageCount - 1) {
                self->zoomWindowInternalPageChange = true;
                self->zoomIndicatorPosX = 0;
                self->zoomIndicatorPosY = 0;
                self->control->getScrollHandler()->scrollToPage(currentPage + 1, XojPdfRectangle{});
                pageChanged = true;
            }
            handled = true;
    }
    
    if (handled) {
        // Re-get maxX/maxY if page changed (new page may have different dimensions)
        if (pageChanged) {
            // The flag zoomWindowInternalPageChange is set, so the draw function
            // won't reset our position when it detects the page change.
            // Just clamp to reasonable values here.
            self->zoomIndicatorPosX = std::max(0.0, self->zoomIndicatorPosX);
            self->zoomIndicatorPosY = std::max(0.0, self->zoomIndicatorPosY);
        } else {
            // Clamp position to valid range
            self->zoomIndicatorPosX = std::max(0.0, std::min(self->zoomIndicatorPosX, maxX));
            self->zoomIndicatorPosY = std::max(0.0, std::min(self->zoomIndicatorPosY, maxY));
        }
        
        // Ensure the indicator rectangle is visible in the main view
        // Note: when page changed, pageView is the old page, but ensureRectIsVisible
        // will be called again by the timer with correct coordinates
        if (!pageChanged) {
            int pageX = pageView->getX();
            int pageY = pageView->getY();
            int indicatorAbsX = pageX + static_cast<int>(self->zoomIndicatorPosX);
            int indicatorAbsY = pageY + static_cast<int>(self->zoomIndicatorPosY);
            self->xournal->ensureRectIsVisible(indicatorAbsX, indicatorAbsY, zoomWidth, zoomHeight);
        }
        
        if (self->zoomWindowDrawingArea) {
            gtk_widget_queue_draw(self->zoomWindowDrawingArea);
        }
        return TRUE;
    }
    
    return FALSE;
}

void MainWindow::loadTabletMappingConfig() {
    // Load tablet mapping configuration from settings
    Settings* settings = control->getSettings();
    SElement& tabletMapping = settings->getCustomElement("tabletMapping");
    
    // Linux KDE configuration
    std::string group;
    std::string outputUuid;
    
    if (tabletMapping.getString("linuxKDEGroup", group) && 
        tabletMapping.getString("linuxKDEOutputUuid", outputUuid)) {
        
        TabletMapping::LinuxKDEConfig linuxConfig;
        linuxConfig.group = group;
        linuxConfig.outputUuid = outputUuid;
        
        double value;
        
        // Full window input area (portion of tablet to use)
        if (tabletMapping.getDouble("linuxFullInputX", value)) linuxConfig.fullInputX = value;
        if (tabletMapping.getDouble("linuxFullInputY", value)) linuxConfig.fullInputY = value;
        if (tabletMapping.getDouble("linuxFullInputWidth", value)) linuxConfig.fullInputWidth = value;
        if (tabletMapping.getDouble("linuxFullInputHeight", value)) linuxConfig.fullInputHeight = value;
        
        // Full window output area (portion of screen to map to)
        if (tabletMapping.getDouble("linuxFullOutputX", value)) linuxConfig.fullOutputX = value;
        if (tabletMapping.getDouble("linuxFullOutputY", value)) linuxConfig.fullOutputY = value;
        if (tabletMapping.getDouble("linuxFullOutputWidth", value)) linuxConfig.fullOutputWidth = value;
        if (tabletMapping.getDouble("linuxFullOutputHeight", value)) linuxConfig.fullOutputHeight = value;
        
        // Zoom window input area (portion of tablet to use)
        if (tabletMapping.getDouble("linuxZoomInputX", value)) linuxConfig.zoomInputX = value;
        if (tabletMapping.getDouble("linuxZoomInputY", value)) linuxConfig.zoomInputY = value;
        if (tabletMapping.getDouble("linuxZoomInputWidth", value)) linuxConfig.zoomInputWidth = value;
        if (tabletMapping.getDouble("linuxZoomInputHeight", value)) linuxConfig.zoomInputHeight = value;
        
        // Zoom window output area (portion of screen to map to)
        if (tabletMapping.getDouble("linuxZoomOutputX", value)) linuxConfig.zoomOutputX = value;
        if (tabletMapping.getDouble("linuxZoomOutputY", value)) linuxConfig.zoomOutputY = value;
        if (tabletMapping.getDouble("linuxZoomOutputWidth", value)) linuxConfig.zoomOutputWidth = value;
        if (tabletMapping.getDouble("linuxZoomOutputHeight", value)) linuxConfig.zoomOutputHeight = value;
        
        TabletMapping::setLinuxKDEConfig(linuxConfig);
        
        g_message("TabletMapping: Loaded Linux KDE config - group=%s", group.c_str());
    } else {
        g_message("TabletMapping: No Linux KDE config found in settings. "
                 "To configure, add tabletMapping section to settings.xml with:\n"
                 "  linuxKDEGroup, linuxKDEOutputUuid\n"
                 "  linuxFullInputX/Y/Width/Height, linuxFullOutputX/Y/Width/Height\n"
                 "  linuxZoomInputX/Y/Width/Height, linuxZoomOutputX/Y/Width/Height");
    }
    
    // Windows configuration
    {
        TabletMapping::WindowsConfig windowsConfig;
        
        double value;
        // Full window input area (portion of tablet to use)
        if (tabletMapping.getDouble("windowsFullInputX", value)) windowsConfig.fullInputX = value;
        if (tabletMapping.getDouble("windowsFullInputY", value)) windowsConfig.fullInputY = value;
        if (tabletMapping.getDouble("windowsFullInputWidth", value)) windowsConfig.fullInputWidth = value;
        if (tabletMapping.getDouble("windowsFullInputHeight", value)) windowsConfig.fullInputHeight = value;
        
        // Full window output area (portion of screen to map to)
        if (tabletMapping.getDouble("windowsFullOutputX", value)) windowsConfig.fullOutputX = value;
        if (tabletMapping.getDouble("windowsFullOutputY", value)) windowsConfig.fullOutputY = value;
        if (tabletMapping.getDouble("windowsFullOutputWidth", value)) windowsConfig.fullOutputWidth = value;
        if (tabletMapping.getDouble("windowsFullOutputHeight", value)) windowsConfig.fullOutputHeight = value;
        
        // Zoom window input area (portion of tablet to use)
        if (tabletMapping.getDouble("windowsZoomInputX", value)) windowsConfig.zoomInputX = value;
        if (tabletMapping.getDouble("windowsZoomInputY", value)) windowsConfig.zoomInputY = value;
        if (tabletMapping.getDouble("windowsZoomInputWidth", value)) windowsConfig.zoomInputWidth = value;
        if (tabletMapping.getDouble("windowsZoomInputHeight", value)) windowsConfig.zoomInputHeight = value;
        
        // Zoom window output area (portion of screen to map to)
        if (tabletMapping.getDouble("windowsZoomOutputX", value)) windowsConfig.zoomOutputX = value;
        if (tabletMapping.getDouble("windowsZoomOutputY", value)) windowsConfig.zoomOutputY = value;
        if (tabletMapping.getDouble("windowsZoomOutputWidth", value)) windowsConfig.zoomOutputWidth = value;
        if (tabletMapping.getDouble("windowsZoomOutputHeight", value)) windowsConfig.zoomOutputHeight = value;
        
        TabletMapping::setWindowsConfig(windowsConfig);
        
        g_message("TabletMapping: Loaded Windows config - FullOutput=(%.2f,%.2f,%.2f,%.2f), ZoomOutput=(%.2f,%.2f,%.2f,%.2f)",
                  windowsConfig.fullOutputX, windowsConfig.fullOutputY, 
                  windowsConfig.fullOutputWidth, windowsConfig.fullOutputHeight,
                  windowsConfig.zoomOutputX, windowsConfig.zoomOutputY,
                  windowsConfig.zoomOutputWidth, windowsConfig.zoomOutputHeight);
    }
    
    // Check if tablet mapping is available on this system
    if (TabletMapping::isAvailable()) {
        g_message("TabletMapping: System supports tablet mapping");
    } else {
        g_message("TabletMapping: System does not support tablet mapping (kwriteconfig not found on Linux, or not implemented on Windows)");
    }
}

double MainWindow::getZoomWindowFactor() const {
    Settings* settings = control->getSettings();
    SElement& zoomWindow = settings->getCustomElement("zoomWindow");
    double factor = 1.5;  // Default value
    zoomWindow.getDouble("zoomFactor", factor);
    return factor;
}

void MainWindow::getZoomWindowSize(int& width, int& height) const {
    Settings* settings = control->getSettings();
    SElement& zoomWindow = settings->getCustomElement("zoomWindow");
    width = 560;   // Default value
    height = 350;  // Default value
    zoomWindow.getInt("width", width);
    zoomWindow.getInt("height", height);
}

GdkModifierType MainWindow::getIndicatorMoveModifier() const {
    Settings* settings = control->getSettings();
    SElement& zoomWindow = settings->getCustomElement("zoomWindow");
    std::string modifier = "alt";
    zoomWindow.getString("indicatorMoveModifier", modifier);
    
    if (modifier == "ctrl") {
        return GDK_CONTROL_MASK;
    } else if (modifier == "shift") {
        return GDK_SHIFT_MASK;
    } else if (modifier == "none") {
        return static_cast<GdkModifierType>(0);
    }
    // Default to Alt
    return GDK_MOD1_MASK;
}

bool MainWindow::matchesShortcut(GdkEventKey* event, const std::string& shortcutSetting) const {
    Settings* settings = control->getSettings();
    SElement& zoomWindow = settings->getCustomElement("zoomWindow");
    std::string shortcut;
    zoomWindow.getString(shortcutSetting, shortcut);
    
    if (shortcut.empty()) {
        return false;
    }
    
    // Parse the shortcut string using GTK's accelerator parser
    guint accelKey = 0;
    GdkModifierType accelMods = static_cast<GdkModifierType>(0);
    gtk_accelerator_parse(shortcut.c_str(), &accelKey, &accelMods);
    
    if (accelKey == 0) {
        return false;
    }
    
    // Normalize the key to lowercase for comparison
    guint eventKey = gdk_keyval_to_lower(event->keyval);
    guint accelKeyLower = gdk_keyval_to_lower(accelKey);
    
    // Check if the modifiers match (ignoring lock keys like Caps Lock, Num Lock)
    GdkModifierType relevantMods = static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK);
    GdkModifierType eventMods = static_cast<GdkModifierType>(event->state & relevantMods);
    
    return (eventKey == accelKeyLower) && (eventMods == accelMods);
}

guint MainWindow::getMovementKey(const std::string& direction) const {
    Settings* settings = control->getSettings();
    SElement& zoomWindow = settings->getCustomElement("zoomWindow");
    std::string keyName;
    std::string settingName = "movementKey" + direction.substr(0, 1);
    // Capitalize first letter for setting name
    if (!direction.empty()) {
        settingName = "movementKey";
        settingName += static_cast<char>(std::toupper(direction[0]));
        settingName += direction.substr(1);
    }
    
    zoomWindow.getString(settingName, keyName);
    
    // If no custom key is set, use defaults
    if (keyName.empty()) {
        if (direction == "left") return gdk_keyval_to_lower(GDK_KEY_Left);
        if (direction == "right") return gdk_keyval_to_lower(GDK_KEY_Right);
        if (direction == "up") return gdk_keyval_to_lower(GDK_KEY_Up);
        if (direction == "down") return gdk_keyval_to_lower(GDK_KEY_Down);
        if (direction == "home") return gdk_keyval_to_lower(GDK_KEY_Home);
        if (direction == "end") return gdk_keyval_to_lower(GDK_KEY_End);
        if (direction == "pageUp") return gdk_keyval_to_lower(GDK_KEY_Page_Up);
        if (direction == "pageDown") return gdk_keyval_to_lower(GDK_KEY_Page_Down);
        return 0;
    }
    
    // Parse the key name using GTK's function
    guint keyval = gdk_keyval_from_name(keyName.c_str());
    if (keyval == GDK_KEY_VoidSymbol) {
        // Try uppercase version
        keyval = gdk_keyval_from_name(keyName.c_str());
    }
    
    return gdk_keyval_to_lower(keyval);
}

void MainWindow::setGtkTouchscreenScrollingForDeviceMapping() {
    InputDeviceClass touchscreenClass =
            DeviceListHelper::getSourceMapping(GDK_SOURCE_TOUCHSCREEN, this->getControl()->getSettings());

    setGtkTouchscreenScrollingEnabled(touchscreenClass == INPUT_DEVICE_TOUCHSCREEN &&
                                      !control->getSettings()->getTouchDrawingEnabled());
}

void MainWindow::setGtkTouchscreenScrollingEnabled(bool enabled) {
    if (!control->getSettings()->getGtkTouchInertialScrollingEnabled()) {
        enabled = false;
    }
    gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(winXournal), enabled);
}

auto MainWindow::getLayout() const -> Layout* { return this->xournal->getLayout(); }

auto MainWindow::getNegativeXournalWidgetPos() const -> xoj::util::Point<double> {
    return Util::toWidgetCoords(this->winXournal, xoj::util::Point{0.0, 0.0});
}

auto cancellable_cancel(GCancellable* cancel) -> bool {
    g_cancellable_cancel(cancel);

    g_warning("Timeout... Cancel loading URL");

    return false;
}

void MainWindow::dragDataRecived(GtkWidget* widget, GdkDragContext* dragContext, gint x, gint y, GtkSelectionData* data,
                                 guint info, guint time, MainWindow* win) {
    GtkWidget* source = gtk_drag_get_source_widget(dragContext);
    if (source && widget == gtk_widget_get_toplevel(source)) {
        gtk_drag_finish(dragContext, false, false, time);
        return;
    }

    guchar* text = gtk_selection_data_get_text(data);
    if (text) {
        win->control->clipboardPasteText(reinterpret_cast<const char*>(text));

        g_free(text);
        gtk_drag_finish(dragContext, true, false, time);
        return;
    }

    xoj::util::GObjectSPtr<GdkPixbuf> image(gtk_selection_data_get_pixbuf(data), xoj::util::adopt);
    if (image) {
        win->control->clipboardPasteImage(image.get());

        gtk_drag_finish(dragContext, true, false, time);
        return;
    }

    gchar** uris = gtk_selection_data_get_uris(data);
    if (uris) {
        for (int i = 0; uris[i] != nullptr && i < 3; i++) {
            const char* uri = uris[i];

            GCancellable* cancel = g_cancellable_new();
            auto cancelTimeout = g_timeout_add(3000, xoj::util::wrap_for_once_v<cancellable_cancel>, cancel);

            xoj::util::GObjectSPtr<GFile> file(g_file_new_for_uri(uri), xoj::util::adopt);
            GError* err = nullptr;
            GFileInputStream* in = g_file_read(file.get(), cancel, &err);
            if (g_cancellable_is_cancelled(cancel)) {
                continue;
            }

            if (err == nullptr) {
                xoj::util::GObjectSPtr<GdkPixbuf> pixbuf(
                        gdk_pixbuf_new_from_stream(G_INPUT_STREAM(in), cancel, nullptr), xoj::util::adopt);
                if (g_cancellable_is_cancelled(cancel)) {
                    continue;
                }
                g_input_stream_close(G_INPUT_STREAM(in), cancel, nullptr);
                if (g_cancellable_is_cancelled(cancel)) {
                    continue;
                }

                if (pixbuf) {
                    win->control->clipboardPasteImage(pixbuf.get());
                }
            } else {
                g_error_free(err);
            }

            if (!g_cancellable_is_cancelled(cancel)) {
                g_source_remove(cancelTimeout);
            }
            g_object_unref(cancel);
        }

        gtk_drag_finish(dragContext, true, false, time);

        g_strfreev(uris);
    }

    gtk_drag_finish(dragContext, false, false, time);
}

auto MainWindow::getControl() const -> Control* { return control; }

void MainWindow::updateScrollbarSidebarPosition() {
    // Part 1: update scrollbar position
    if (winXournal != nullptr) {
        GtkScrolledWindow* scrolledWindow = GTK_SCROLLED_WINDOW(winXournal);

        ScrollbarHideType type = this->getControl()->getSettings()->getScrollbarHideType();

        bool scrollbarOnLeft = control->getSettings()->isScrollbarOnLeft();
        if (scrollbarOnLeft) {
            gtk_scrolled_window_set_placement(scrolledWindow, GTK_CORNER_TOP_RIGHT);
        } else {
            gtk_scrolled_window_set_placement(scrolledWindow, GTK_CORNER_TOP_LEFT);
        }

        gtk_widget_set_visible(gtk_scrolled_window_get_hscrollbar(scrolledWindow), !(type & SCROLLBAR_HIDE_HORIZONTAL));
        gtk_widget_set_visible(gtk_scrolled_window_get_vscrollbar(scrolledWindow), !(type & SCROLLBAR_HIDE_VERTICAL));

        gtk_scrolled_window_set_overlay_scrolling(scrolledWindow,
                                                  !control->getSettings()->isScrollbarFadeoutDisabled());
    }

    // Part 2: update sidebar position
    GtkPaned* paned = GTK_PANED(this->panedContainerWidget.get());

    // Allocation is reset when we switch up the contained elements. Fetch the
    // width here in case we need it afterwards.
    int contentWidth = gtk_widget_get_width(this->boxContainerWidget.get());

    bool sidebarRight = control->getSettings()->isSidebarOnRight();
    if (sidebarRight != (gtk_paned_get_child2(paned) == this->sidebarWidget.get())) {
        // switch sidebar and main content
        GtkWidget* sidebar = this->sidebarWidget.get();
        GtkWidget* mainContent = this->sidebarVisible ? this->mainContentWidget.get() : nullptr;
#if GTK_MAJOR_VERSION == 3
        if (this->sidebarVisible) {
            gtk_container_remove(GTK_CONTAINER(paned), sidebar);
            gtk_container_remove(GTK_CONTAINER(paned), mainContent);

            if (sidebarRight) {
                gtk_paned_pack1(paned, mainContent, true, false);
                gtk_paned_pack2(paned, sidebar, false, false);
            } else {
                gtk_paned_pack1(paned, sidebar, false, false);
                gtk_paned_pack2(paned, mainContent, true, false);
            }
        } else {
            // The sidebar is hidden. That means the paned widget only contains the
            // sidebar while the main contents are shown alone in the box container.
            gtk_container_remove(GTK_CONTAINER(paned), sidebar);

            if (sidebarRight) {
                gtk_paned_pack2(paned, sidebar, false, false);
            } else {
                gtk_paned_pack1(paned, sidebar, false, false);
            }
        }
#else
        gtk_paned_set_start_child(paned, nullptr);
        gtk_paned_set_end_child(paned, nullptr);
        if (sidebarRight) {
            gtk_paned_set_start_child(paned, mainContent);
            gtk_paned_set_resize_start_child(paned, true);
            gtk_paned_set_shrink_start_child(paned, false);
            gtk_paned_set_end_child(paned, sidebar);
            gtk_paned_set_resize_end_child(paned, false);
            gtk_paned_set_shrink_end_child(paned, false);
        } else {
            gtk_paned_set_end_child(paned, mainContent);
            gtk_paned_set_resize_end_child(paned, true);
            gtk_paned_set_shrink_end_child(paned, false);
            gtk_paned_set_start_child(paned, sidebar);
            gtk_paned_set_resize_start_child(paned, false);
            gtk_paned_set_shrink_start_child(paned, false);
        }
#endif
    }

    if (this->sidebarVisible) {
        updatePanedPosition(contentWidth);
    }
}

auto MainWindow::deleteEventCallback(GtkWidget* widget, GdkEvent* event, Control* control) -> bool {
    control->quit();

    return true;
}

void MainWindow::setSidebarVisible(bool visible) {
    if (!visible && (this->control->getSidebar() != nullptr)) {
        this->control->getSidebar()->saveSize();
    }

    if (visible != this->sidebarVisible) {
        // Due to a GTK bug, we can't just hide the sidebar widget in the GtkPaned.
        // If we do this, we create a dead region where the pane separator was previously.
        // In this region, we can't use the touchscreen to start horizontal strokes.
        // As such:
        if (!visible) {
            // hide sidebar
#if GTK_MAJOR_VERSION == 3
            gtk_container_remove(GTK_CONTAINER(panedContainerWidget.get()), mainContentWidget.get());
#else
            if (control->getSettings()->isSidebarOnRight()) {
                gtk_paned_set_start_child(GTK_PANED(panedContainerWidget.get()), nullptr);
            } else {
                gtk_paned_set_end_child(GTK_PANED(panedContainerWidget.get()), nullptr);
            }
#endif
            gtk_box_remove(GTK_BOX(boxContainerWidget.get()), panedContainerWidget.get());
            gtk_box_append(GTK_BOX(boxContainerWidget.get()), mainContentWidget.get());
            this->sidebarVisible = false;
        } else {
            // show sidebar

            // Allocation is reset when we switch up the contained elements. Fetch the
            // width here in case we need it afterwards.
            int contentWidth = gtk_widget_get_width(boxContainerWidget.get());

            gtk_box_remove(GTK_BOX(boxContainerWidget.get()), mainContentWidget.get());

#if GTK_MAJOR_VERSION == 3
            if (control->getSettings()->isSidebarOnRight()) {
                gtk_paned_pack1(GTK_PANED(panedContainerWidget.get()), mainContentWidget.get(), true, false);
            } else {
                gtk_paned_pack2(GTK_PANED(panedContainerWidget.get()), mainContentWidget.get(), true, false);
            }
#else
            if (control->getSettings()->isSidebarOnRight()) {
                gtk_paned_set_start_child(GTK_PANED(panedContainerWidget.get()), mainContentWidget.get());
            } else {
                gtk_paned_set_end_child(GTK_PANED(panedContainerWidget.get()), mainContentWidget.get());
            }
#endif

            gtk_box_append(GTK_BOX(boxContainerWidget.get()), panedContainerWidget.get());
            this->sidebarVisible = true;

            updatePanedPosition(contentWidth);
        }
    }

    gtk_widget_set_visible(sidebarWidget.get(), visible);
}

/**
 * Invert the position of the paned widget and disconnect from the signal.
 * @param handlerId should be the ID of the signal handler that should be disconnected.
 */
static void invertPanedPosition(GtkWidget* widget, GtkAllocation* allocation, gulong* handlerId) {
    int newDividerPos = allocation->width - gtk_paned_get_position(GTK_PANED(widget));
    gtk_paned_set_position(GTK_PANED(widget), newDividerPos);

    // We only need to switch the position once, so disconnect the signal right away.
    g_signal_handler_disconnect(widget, *handlerId);
}

void MainWindow::updatePanedPosition(int contentWidth) {
    if (!this->control->getSettings()->isSidebarOnRight()) {
        // Sidebar is on the left side.
        gtk_paned_set_position(GTK_PANED(this->panedContainerWidget.get()),
                               this->control->getSettings()->getSidebarWidth());
    } else {
        // Sidebar is on the right side.
        if (contentWidth > 0) {
            int dividerPos = contentWidth - this->control->getSettings()->getSidebarWidth();
            gtk_paned_set_position(GTK_PANED(this->panedContainerWidget.get()), dividerPos);
        } else {
            // Allocation is unkown (window hasn't been shown yet). We have to wait for the signal.
            // Set position as if the sidebar was on the left side, and let the signal handler
            // simply invert the position when the allocation is known.
            gtk_paned_set_position(GTK_PANED(this->panedContainerWidget.get()),
                                   this->control->getSettings()->getSidebarWidth());
            gulong* signal_id = new gulong{};
            *signal_id = g_signal_connect_data(
                    this->panedContainerWidget.get(), "size-allocate",
                    xoj::util::wrap_for_g_callback_v<invertPanedPosition>, signal_id,
                    [](gpointer d, GClosure*) { delete reinterpret_cast<gulong*>(d); }, GConnectFlags(0));
        }
    }
}

void MainWindow::setToolbarVisible(bool visible) {
    Settings* settings = control->getSettings();

    settings->setToolbarVisible(visible);
    for (auto& w: this->toolbarWidgets) {
        if (!visible || (gtk_toolbar_get_n_items(GTK_TOOLBAR(w.get())) != 0)) {
            gtk_widget_set_visible(w.get(), visible);
        }
    }
}

void MainWindow::setMenubarVisible(bool visible) {
    gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(this->getWindow()), visible);
}

void MainWindow::setMaximized(bool maximized) { this->maximized = maximized; }

auto MainWindow::isMaximized() const -> bool { return this->maximized; }

auto MainWindow::setFullscreen(bool enabled) const -> void {
    if (enabled) {
        gtk_window_fullscreen(GTK_WINDOW(this->getWindow()));
    } else {
        gtk_window_unfullscreen(GTK_WINDOW(this->getWindow()));
    }
}

auto MainWindow::isDarkTheme() const -> bool { return this->darkMode; }

auto MainWindow::getXournal() const -> XournalView* { return xournal.get(); }

auto MainWindow::getZoomWindowDrawingArea() const -> GtkWidget* { return zoomWindowDrawingArea; }

auto MainWindow::windowMaximizedCallback(GObject* window, GParamSpec*, MainWindow* win) -> void {
    win->setMaximized(gtk_window_is_maximized(GTK_WINDOW(window)));
}

void MainWindow::toolbarSelected(const std::string& id) {
    const auto& toolbars = toolbar->getModel()->getToolbars();
    auto it = std::find_if(toolbars.begin(), toolbars.end(), [&](const auto& d) { return d->getId() == id; });
    toolbarSelected(it == toolbars.end() ? nullptr : it->get());
}

void MainWindow::toolbarSelected(ToolbarData* d) {
    if (!d || this->selectedToolbar == d) {
        return;
    }

    Settings* settings = control->getSettings();
    settings->setSelectedToolbar(d->getId());

    this->clearToolbar();
    this->loadToolbar(d);
}

auto MainWindow::clearToolbar() -> const ToolbarData* {
    if (this->selectedToolbar != nullptr) {
        for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
            ToolMenuHandler::unloadToolbar(this->toolbarWidgets[i].get());
        }

        this->toolbar->freeDynamicToolbarItems();
    }
    return std::exchange(this->selectedToolbar, nullptr);
}

void MainWindow::loadToolbar(ToolbarData* d) {
    this->selectedToolbar = d;

    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        this->toolbar->load(d, this->toolbarWidgets[i].get(), TOOLBAR_DEFINITIONS[i].propName,
                            TOOLBAR_DEFINITIONS[i].horizontal);
    }

    this->floatingToolbox->flagRecalculateSizeRequired();
}

void MainWindow::reloadToolbars() {
    ToolbarData* d = getSelectedToolbar();
    this->clearToolbar();
    this->toolbarSelected(d);
}

auto MainWindow::getSelectedToolbar() const -> ToolbarData* { return this->selectedToolbar; }

auto MainWindow::getToolbarWidgets() const -> const ToolbarWidgetArray& { return toolbarWidgets; }

auto MainWindow::getToolbarName(GtkToolbar* toolbar) const -> const char* {
    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        if (static_cast<void*>(this->toolbarWidgets[i].get()) == static_cast<void*>(toolbar)) {
            return TOOLBAR_DEFINITIONS[i].propName;
        }
    }

    return "";
}

void MainWindow::setDynamicallyGeneratedSubmenuDisabled(bool disabled) { menubar->setDisabled(disabled); }

void MainWindow::updateToolbarMenu() {
    menubar->getToolbarSelectionSubmenu().update(toolbar.get(), this->selectedToolbar);
}

void MainWindow::createToolbar() {
    toolbarSelected(control->getSettings()->getSelectedToolbar());

    this->control->getScheduler()->unblockRerenderZoom();
}

void MainWindow::updatePageNumbers(size_t page, size_t pagecount, size_t pdfpage) {
    toolbar->setPageInfo(page, pagecount, pdfpage);
}

auto MainWindow::getMenubar() const -> Menubar* { return menubar.get(); }

void MainWindow::show(GtkWindow* parent) { gtk_widget_show(this->window); }

void MainWindow::setUndoDescription(const string& description) { menubar->setUndoDescription(description); }

void MainWindow::setRedoDescription(const string& description) { menubar->setRedoDescription(description); }

auto MainWindow::getToolbarModel() const -> ToolbarModel* { return this->toolbar->getModel(); }

auto MainWindow::getToolMenuHandler() const -> ToolMenuHandler* { return this->toolbar.get(); }

void MainWindow::loadMainCSS(GladeSearchpath* gladeSearchPath, const gchar* cssFilename) {
    auto filepath = gladeSearchPath->findFile("", cssFilename);
    xoj::util::GObjectSPtr<GtkCssProvider> provider(gtk_css_provider_new(), xoj::util::adopt);
    gtk_css_provider_load_from_path(provider.get(), char_cast(filepath.u8string().c_str()), nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider.get()),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

PdfFloatingToolbox* MainWindow::getPdfToolbox() const { return this->pdfFloatingToolBox.get(); }

FloatingToolbox* MainWindow::getFloatingToolbox() const { return this->floatingToolbox.get(); }

void MainWindow::setDPI() const {
    if (auto dpi = this->getControl()->getSettings()->getDisplayDpi(); dpi == -1) {
        auto res = xoj::util::gtk::getWidgetDPI(this->window);
        this->getControl()->getZoomControl()->setZoom100Value(res.value_or(Util::DPI_NORMALIZATION_FACTOR) /
                                                              Util::DPI_NORMALIZATION_FACTOR);
    } else {
        this->getControl()->getZoomControl()->setZoom100Value(dpi / Util::DPI_NORMALIZATION_FACTOR);
    }
}
