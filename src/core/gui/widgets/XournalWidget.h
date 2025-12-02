/*
 * Xournal++
 *
 * Xournal widget which is the "View" widget
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>  // for unique_ptr

#include <glib-object.h>  // for G_TYPE_CHECK_INSTANCE_CAST, G_TYPE_C...
#include <glib.h>         // for G_BEGIN_DECLS, G_END_DECLS
#include <gtk/gtk.h>      // for GtkWidget, GtkWidgetClass

namespace xoj::util {
template <class T>
class Rectangle;
}  // namespace xoj::util
struct _GtkXournal;
struct _GtkXournalClass;

G_BEGIN_DECLS

#define GTK_XOURNAL(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, gtk_xournal_get_type(), GtkXournal)
#define GTK_XOURNAL_CLASS(klass) GTK_CHECK_CLASS_CAST(klass, gtk_xournal_get_type(), GtkXournalClass)
#define GTK_IS_XOURNAL(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, gtk_xournal_get_type())

class EditSelection;
class Layout;
class XojPageView;
class ScrollHandling;
class XournalView;
class InputContext;


typedef struct _GtkXournal GtkXournal;
typedef struct _GtkXournalClass GtkXournalClass;

struct _GtkXournal {
    GtkWidget widget;

    /**
     * The view class
     */
    XournalView* view;

    /**
     * Scrollbars
     */
    ScrollHandling* scrollHandling;


    Layout* layout;


    /**
     * Selected content, if any
     */
    EditSelection* selection;

    /**
     * Input handling
     */
    InputContext* input = nullptr;
    
    /**
     * Zoom window indicator rectangle (in view coordinates)
     * Shows which area is displayed in the zoom window
     */
    bool showZoomIndicator = false;
    double zoomIndicatorX = 0;
    double zoomIndicatorY = 0;
    double zoomIndicatorWidth = 0;
    double zoomIndicatorHeight = 0;
    
    /**
     * Indicator dragging state
     */
    bool indicatorDragging = false;
    double indicatorDragOffsetX = 0;  // Offset from indicator top-left to click position
    double indicatorDragOffsetY = 0;
};

struct _GtkXournalClass {
    GtkWidgetClass parent_class;
};

GType gtk_xournal_get_type();

GtkWidget* gtk_xournal_new(XournalView* view, InputContext* inputContext);

Layout* gtk_xournal_get_layout(GtkWidget* widget);

void gtk_xournal_scroll_relative(GtkWidget* widget, double x, double y);

void gtk_xournal_repaint_area(GtkWidget* widget, int x1, int y1, int x2, int y2);

xoj::util::Rectangle<double>* gtk_xournal_get_visible_area(GtkWidget* widget, const XojPageView* p);

void gtk_xournal_set_zoom_indicator(GtkWidget* widget, bool show, double x, double y, double width, double height);

/**
 * Check if a point is inside the zoom indicator rectangle
 */
bool gtk_xournal_point_in_indicator(GtkWidget* widget, double x, double y);

/**
 * Start dragging the indicator from a given point
 */
void gtk_xournal_start_indicator_drag(GtkWidget* widget, double x, double y);

/**
 * Update indicator position during drag
 */
void gtk_xournal_update_indicator_drag(GtkWidget* widget, double x, double y);

/**
 * Stop dragging the indicator
 */
void gtk_xournal_stop_indicator_drag(GtkWidget* widget);

/**
 * Check if indicator is currently being dragged
 */
bool gtk_xournal_is_indicator_dragging(GtkWidget* widget);

G_END_DECLS
