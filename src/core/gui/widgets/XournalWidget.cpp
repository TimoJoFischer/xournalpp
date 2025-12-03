#include "XournalWidget.h"

#include <algorithm>  // for max
#include <cmath>      // for NAN
#include <optional>   // for optional
#include <vector>     // for vector

#include <cairo.h>    // for cairo_restore, cairo_save
#include <gdk/gdk.h>  // for GdkRectangle, GdkWindowAttr

#include "control/Control.h"                // for Control
#include "control/settings/Settings.h"      // for Settings
#include "control/tools/EditSelection.h"    // for EditSelection
#include "gui/Layout.h"                     // for Layout
#include "gui/LegacyRedrawable.h"           // for Redrawable
#include "gui/PageView.h"                   // for XojPageView
#include "gui/Shadow.h"                     // for Shadow
#include "gui/XournalView.h"                // for XournalView
#include "gui/inputdevices/InputContext.h"  // for InputContext
#include "gui/scroll/ScrollHandling.h"      // for ScrollHandling
#include "util/Color.h"                     // for cairo_set_source_rgbi
#include "util/Rectangle.h"                 // for Rectangle

#include "config-debug.h"  // for DEBUG_DRAW_WIDGET

/*
 * Declares:
 *      static void gtk_xournal_class_init(GtkXournalClass*);
 *      static void gtk_xournal_init(GtkXournal*);
 * Defines
 *      gtk_xournal_parent_class (pointer to GtkWidgetClass instance)
 *      GType gtk_xournal_get_type();
 */
G_DEFINE_TYPE(GtkXournal, gtk_xournal, GTK_TYPE_WIDGET)

static void gtk_xournal_get_preferred_width(GtkWidget* widget, gint* minimal_width, gint* natural_width);
static void gtk_xournal_get_preferred_height(GtkWidget* widget, gint* minimal_height, gint* natural_height);
static void gtk_xournal_size_allocate(GtkWidget* widget, GtkAllocation* allocation);
static void gtk_xournal_realize(GtkWidget* widget);
static auto gtk_xournal_draw(GtkWidget* widget, cairo_t* cr) -> gboolean;
static void gtk_xournal_dispose(GObject* object);

auto gtk_xournal_new(XournalView* view, InputContext* inputContext) -> GtkWidget* {
    GtkXournal* xoj = GTK_XOURNAL(g_object_new(gtk_xournal_get_type(), nullptr));
    xoj->view = view;
    xoj->scrollHandling = inputContext->getScrollHandling();
    xoj->layout = new Layout(view, inputContext->getScrollHandling());
    xoj->selection = nullptr;
    xoj->input = inputContext;

    xoj->input->connect(GTK_WIDGET(xoj));

    return GTK_WIDGET(xoj);
}

static void gtk_xournal_class_init(GtkXournalClass* cptr) {
    auto* widget_class = reinterpret_cast<GtkWidgetClass*>(cptr);

    widget_class->realize = gtk_xournal_realize;
    widget_class->get_preferred_width = gtk_xournal_get_preferred_width;
    widget_class->get_preferred_height = gtk_xournal_get_preferred_height;
    widget_class->size_allocate = gtk_xournal_size_allocate;

    widget_class->draw = gtk_xournal_draw;

#ifdef DEBUG_DRAW_WIDGET
    widget_class->queue_draw_region = +[](GtkWidget* w, const cairo_region_t* reg) {
        cairo_rectangle_int_t r;
        cairo_region_get_extents(reg, &r);
        auto width = gtk_widget_get_allocated_width(w);
        auto height = gtk_widget_get_allocated_height(w);

        auto widthp = gtk_widget_get_allocated_width(gtk_widget_get_parent(w));
        auto heightp = gtk_widget_get_allocated_height(gtk_widget_get_parent(w));
        printf("   * queue_draw_region: %d x %d + (%d ; %d) out of %d x %d   parent: %d x %d\n", r.width, r.height, r.x,
               r.y, width, height, widthp, heightp);
        GTK_WIDGET_CLASS(gtk_xournal_parent_class)->queue_draw_region(w, reg);
    };
#endif

    G_OBJECT_CLASS(cptr)->dispose = gtk_xournal_dispose;
}

void gtk_xournal_set_zoom_indicator(GtkWidget* widget, bool show, double x, double y, double width, double height) {
    g_return_if_fail(widget != nullptr);
    g_return_if_fail(GTK_IS_XOURNAL(widget));
    
    GtkXournal* xournal = GTK_XOURNAL(widget);
    xournal->showZoomIndicator = show;
    xournal->zoomIndicatorX = x;
    xournal->zoomIndicatorY = y;
    xournal->zoomIndicatorWidth = width;
    xournal->zoomIndicatorHeight = height;
    
    gtk_widget_queue_draw(widget);
}

bool gtk_xournal_point_in_indicator(GtkWidget* widget, double x, double y) {
    g_return_val_if_fail(widget != nullptr, false);
    g_return_val_if_fail(GTK_IS_XOURNAL(widget), false);
    
    GtkXournal* xournal = GTK_XOURNAL(widget);
    if (!xournal->showZoomIndicator) {
        return false;
    }
    
    return x >= xournal->zoomIndicatorX && 
           x <= xournal->zoomIndicatorX + xournal->zoomIndicatorWidth &&
           y >= xournal->zoomIndicatorY && 
           y <= xournal->zoomIndicatorY + xournal->zoomIndicatorHeight;
}

bool gtk_xournal_point_in_indicator_corner(GtkWidget* widget, double x, double y) {
    g_return_val_if_fail(widget != nullptr, false);
    g_return_val_if_fail(GTK_IS_XOURNAL(widget), false);
    
    GtkXournal* xournal = GTK_XOURNAL(widget);
    if (!xournal->showZoomIndicator) {
        return false;
    }
    
    // Size of the drag handle in the bottom-right corner
    const double handleSize = 24.0;
    
    double cornerX = xournal->zoomIndicatorX + xournal->zoomIndicatorWidth - handleSize;
    double cornerY = xournal->zoomIndicatorY + xournal->zoomIndicatorHeight - handleSize;
    
    return x >= cornerX && 
           x <= xournal->zoomIndicatorX + xournal->zoomIndicatorWidth &&
           y >= cornerY && 
           y <= xournal->zoomIndicatorY + xournal->zoomIndicatorHeight;
}

void gtk_xournal_start_indicator_drag(GtkWidget* widget, double x, double y) {
    g_return_if_fail(widget != nullptr);
    g_return_if_fail(GTK_IS_XOURNAL(widget));
    
    GtkXournal* xournal = GTK_XOURNAL(widget);
    xournal->indicatorDragging = true;
    // Store offset from indicator top-left to click position
    xournal->indicatorDragOffsetX = x - xournal->zoomIndicatorX;
    xournal->indicatorDragOffsetY = y - xournal->zoomIndicatorY;
}

void gtk_xournal_update_indicator_drag(GtkWidget* widget, double x, double y) {
    g_return_if_fail(widget != nullptr);
    g_return_if_fail(GTK_IS_XOURNAL(widget));
    
    GtkXournal* xournal = GTK_XOURNAL(widget);
    if (!xournal->indicatorDragging) {
        return;
    }
    
    // Calculate new indicator position (maintaining offset from click point)
    xournal->zoomIndicatorX = x - xournal->indicatorDragOffsetX;
    xournal->zoomIndicatorY = y - xournal->indicatorDragOffsetY;
    
    gtk_widget_queue_draw(widget);
}

void gtk_xournal_stop_indicator_drag(GtkWidget* widget) {
    g_return_if_fail(widget != nullptr);
    g_return_if_fail(GTK_IS_XOURNAL(widget));
    
    GtkXournal* xournal = GTK_XOURNAL(widget);
    xournal->indicatorDragging = false;
}

bool gtk_xournal_is_indicator_dragging(GtkWidget* widget) {
    g_return_val_if_fail(widget != nullptr, false);
    g_return_val_if_fail(GTK_IS_XOURNAL(widget), false);
    
    GtkXournal* xournal = GTK_XOURNAL(widget);
    return xournal->indicatorDragging;
}

auto gtk_xournal_get_visible_area(GtkWidget* widget, const XojPageView* p) -> xoj::util::Rectangle<double>* {
    g_return_val_if_fail(widget != nullptr, nullptr);
    g_return_val_if_fail(GTK_IS_XOURNAL(widget), nullptr);

    GtkXournal* xournal = GTK_XOURNAL(widget);

    GtkAdjustment* vadj = xournal->scrollHandling->getVertical();
    GtkAdjustment* hadj = xournal->scrollHandling->getHorizontal();

    GdkRectangle r2;
    r2.x = static_cast<int>(gtk_adjustment_get_value(hadj));
    r2.y = static_cast<int>(gtk_adjustment_get_value(vadj));
    r2.width = static_cast<int>(gtk_adjustment_get_page_size(hadj));
    r2.height = static_cast<int>(gtk_adjustment_get_page_size(vadj));

    GdkRectangle r1;
    r1.x = p->getX();
    r1.y = p->getY();
    r1.width = p->getDisplayWidth();
    r1.height = p->getDisplayHeight();

    GdkRectangle r3 = {0, 0, 0, 0};
    gdk_rectangle_intersect(&r1, &r2, &r3);

    if (r3.width == 0 && r3.height == 0) {
        return nullptr;
    }

    r3.x -= r1.x;
    r3.y -= r1.y;

    double zoom = xournal->view->getZoom();

    if (r3.x < 0 || r3.y < 0) {
        g_warning("XournalWidget:gtk_xournal_get_visible_area: intersection rectangle coordinates are negative which "
                  "should never happen");
    }

    return new xoj::util::Rectangle<double>(std::max(r3.x, 0) / zoom, std::max(r3.y, 0) / zoom, r3.width / zoom,
                                            r3.height / zoom);
}

auto gtk_xournal_get_layout(GtkWidget* widget) -> Layout* {
    g_return_val_if_fail(widget != nullptr, nullptr);
    g_return_val_if_fail(GTK_IS_XOURNAL(widget), nullptr);

    GtkXournal* xournal = GTK_XOURNAL(widget);
    return xournal->layout;
}

static void gtk_xournal_init(GtkXournal* xournal) {
    GtkWidget* widget = GTK_WIDGET(xournal);

    gtk_widget_set_can_focus(widget, true);
}

static void gtk_xournal_get_preferred_width(GtkWidget* widget, gint* minimal_width, gint* natural_width) {
    g_return_if_fail(GTK_IS_XOURNAL(widget));
    GtkXournal* xournal = GTK_XOURNAL(widget);
    g_return_if_fail(xournal->layout);
    *minimal_width = *natural_width = xournal->layout->getMinimalWidth();
}

static void gtk_xournal_get_preferred_height(GtkWidget* widget, gint* minimal_height, gint* natural_height) {
    g_return_if_fail(GTK_IS_XOURNAL(widget));
    GtkXournal* xournal = GTK_XOURNAL(widget);
    g_return_if_fail(xournal->layout);
    *minimal_height = *natural_height = xournal->layout->getMinimalHeight();
}

/**
 * This method is called while scrolling or after the XournalWidget size has changed
 */
static void gtk_xournal_size_allocate(GtkWidget* widget, GtkAllocation* allocation) {
    g_return_if_fail(widget != nullptr);
    g_return_if_fail(GTK_IS_XOURNAL(widget));
    g_return_if_fail(allocation != nullptr);

    gtk_widget_set_allocation(widget, allocation);

    if (gtk_widget_get_realized(widget)) {
        gdk_window_move_resize(gtk_widget_get_window(widget), allocation->x, allocation->y, allocation->width,
                               allocation->height);
    }

    GtkXournal* xournal = GTK_XOURNAL(widget);

    // layout the pages in the XournalWidget
    xournal->layout->layoutPages(allocation->width, allocation->height);
}

static void gtk_xournal_realize(GtkWidget* widget) {
    GdkWindowAttr attributes;

    g_return_if_fail(widget != nullptr);
    g_return_if_fail(GTK_IS_XOURNAL(widget));

    gtk_widget_set_realized(widget, true);

    gtk_widget_set_hexpand(widget, true);
    gtk_widget_set_vexpand(widget, true);

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.x = allocation.x;
    attributes.y = allocation.y;
    attributes.width = allocation.width;
    attributes.height = allocation.height;

    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.event_mask = gtk_widget_get_events(widget) | GDK_EXPOSURE_MASK;

    gint attributes_mask = GDK_WA_X | GDK_WA_Y;

    gtk_widget_set_window(widget, gdk_window_new(gtk_widget_get_parent_window(widget), &attributes, attributes_mask));
    gdk_window_set_user_data(gtk_widget_get_window(widget), widget);
}

static void gtk_xournal_draw_shadow(GtkXournal* xournal, cairo_t* cr, int left, int top, int width, int height,
                                    bool selected) {
    if (selected) {
        Shadow::drawShadow(cr, left - 2, top - 2, width + 4, height + 4);

        Settings* settings = xournal->view->getControl()->getSettings();

        // Draw border
        Util::cairo_set_source_rgbi(cr, settings->getBorderColor());
        cairo_set_line_width(cr, 2.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);

        cairo_rectangle(cr, left - 1, top - 1, width + 2, height + 2);
        cairo_stroke(cr);
    } else {
        Shadow::drawShadow(cr, left, top, width, height);
    }
}

void gtk_xournal_repaint_area(GtkWidget* widget, int x1, int y1, int x2, int y2) {
    g_return_if_fail(widget != nullptr);
    g_return_if_fail(GTK_IS_XOURNAL(widget));

    if (x2 < 0 || y2 < 0) {
        return;  // outside visible area
    }

    GtkAllocation alloc = {0};
    gtk_widget_get_allocation(widget, &alloc);

    if (x1 > alloc.width || y1 > alloc.height) {
        return;  // outside visible area
    }

    gtk_widget_queue_draw_area(widget, x1, y1, x2 - x1, y2 - y1);
}

static auto gtk_xournal_draw(GtkWidget* widget, cairo_t* cr) -> gboolean {
    g_return_val_if_fail(widget != nullptr, false);
    g_return_val_if_fail(GTK_IS_XOURNAL(widget), false);


#ifdef DEBUG_DRAW_WIDGET
    {
        double x1 = NAN, x2 = NAN, y1 = NAN, y2 = NAN;
        cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
        printf("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n\n"
               "      DRAW  %d x %d + (%d ; %d)\n\n"
               "$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$&&&&&&&&&&&&&&&&&&&&&&\n",
               round_cast<int>(x2 - x1), round_cast<int>(y2 - y1), round_cast<int>(x1), round_cast<int>(y1));
    }
#endif

    GtkXournal* xournal = GTK_XOURNAL(widget);

    double x1 = NAN, x2 = NAN, y1 = NAN, y2 = NAN;

    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);

    // Draw background
    Settings* settings = xournal->view->getControl()->getSettings();
    Util::cairo_set_source_rgbi(cr, settings->getBackgroundColor());
    cairo_paint(cr);

    // Add a padding for the shadow of the pages
    xoj::util::Rectangle<double> clippingRect(x1 - 10, y1 - 10, x2 - x1 + 20, y2 - y1 + 20);

    for (auto&& pv: xournal->view->getViewPages()) {
        int px = pv->getX();
        int py = pv->getY();
        int pw = pv->getDisplayWidth();
        int ph = pv->getDisplayHeight();

        if (!clippingRect.intersects(pv->getRect())) {
            continue;
        }

        gtk_xournal_draw_shadow(xournal, cr, px, py, pw, ph, pv->isSelected());

        cairo_save(cr);
        cairo_translate(cr, px, py);

        pv->paintPage(cr, nullptr);
        cairo_restore(cr);
    }

    if (xournal->selection) {
        cairo_save(cr);
        double zoom = xournal->view->getZoom();

        LegacyRedrawable* red = xournal->selection->getView();
        cairo_translate(cr, red->getX(), red->getY());

        xournal->selection->paint(cr, zoom);
        cairo_restore(cr);
    }

    std::optional<Recolor> recolor = settings->getRecolorParameters().recolorizeMainView ?
                                             std::make_optional(settings->getRecolorParameters().recolor) :
                                             std::nullopt;

    if (recolor) {
        recolor->recolorCurrentCairoRegion(cr);
    }

    // Draw zoom window indicator rectangle
    if (xournal->showZoomIndicator) {
        cairo_save(cr);
        
        // Draw blue border rectangle
        cairo_set_source_rgb(cr, 0.0, 0.4, 1.0);  // Blue color
        cairo_set_line_width(cr, 2.0);
        cairo_rectangle(cr, xournal->zoomIndicatorX, xournal->zoomIndicatorY,
                        xournal->zoomIndicatorWidth, xournal->zoomIndicatorHeight);
        cairo_stroke(cr);
        
        // Draw semi-transparent fill
        cairo_set_source_rgba(cr, 0.0, 0.4, 1.0, 0.1);  // Light blue fill
        cairo_rectangle(cr, xournal->zoomIndicatorX, xournal->zoomIndicatorY,
                        xournal->zoomIndicatorWidth, xournal->zoomIndicatorHeight);
        cairo_fill(cr);
        
        // Draw drag handle in bottom-right corner
        const double handleSize = 24.0;
        double handleX = xournal->zoomIndicatorX + xournal->zoomIndicatorWidth - handleSize;
        double handleY = xournal->zoomIndicatorY + xournal->zoomIndicatorHeight - handleSize;
        
        // Filled corner triangle/area for the drag handle
        cairo_set_source_rgba(cr, 0.0, 0.4, 1.0, 0.4);  // Slightly more opaque blue
        cairo_move_to(cr, handleX + handleSize, handleY);
        cairo_line_to(cr, handleX + handleSize, handleY + handleSize);
        cairo_line_to(cr, handleX, handleY + handleSize);
        cairo_close_path(cr);
        cairo_fill(cr);
        
        // Draw diagonal lines in the corner to indicate it's draggable
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);  // White lines
        cairo_set_line_width(cr, 1.5);
        
        // Three diagonal lines
        cairo_move_to(cr, handleX + handleSize - 6, handleY + handleSize);
        cairo_line_to(cr, handleX + handleSize, handleY + handleSize - 6);
        cairo_stroke(cr);
        
        cairo_move_to(cr, handleX + handleSize - 12, handleY + handleSize);
        cairo_line_to(cr, handleX + handleSize, handleY + handleSize - 12);
        cairo_stroke(cr);
        
        cairo_move_to(cr, handleX + handleSize - 18, handleY + handleSize);
        cairo_line_to(cr, handleX + handleSize, handleY + handleSize - 18);
        cairo_stroke(cr);
        
        cairo_restore(cr);
    }

    return true;
}

static void gtk_xournal_dispose(GObject* object) {
    g_return_if_fail(object != nullptr);
    g_return_if_fail(GTK_IS_XOURNAL(object));
    GtkXournal* xournal = GTK_XOURNAL(object);

    delete xournal->selection;
    xournal->selection = nullptr;

    delete xournal->layout;
    xournal->layout = nullptr;

    delete xournal->input;
    xournal->input = nullptr;

    G_OBJECT_CLASS(gtk_xournal_parent_class)->dispose(object);
}
