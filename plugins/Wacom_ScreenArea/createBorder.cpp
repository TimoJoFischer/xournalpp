#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/X.h>
#include <X11/Xutil.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <iostream>
#include <unistd.h>

using namespace std;

void draw(cairo_t *cr, double w, double h) {
    cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 0.5);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);
}
/*
 * to compile this file use e.g:
 * g++ -L/usr/lib -lcairo -lX11 createBorder.cpp -o createBorder
 */

int main(int argc, char *argv[]) {
    int x = atoi(argv[1]);
    int y = atoi(argv[2]);
    int w = atoi(argv[3]);
    int h = atoi(argv[4]);
    int linewidth = atoi(argv[5]);

    Display *d = XOpenDisplay(NULL);
    Window root = DefaultRootWindow(d);

    XSetWindowAttributes attrs;
    attrs.override_redirect = true;

    XVisualInfo vinfo;
    if (!XMatchVisualInfo(d, DefaultScreen(d), 32, TrueColor, &vinfo)) {
        printf("No visual found supporting 32 bit color, terminating\n");
        return (1);
    }
    attrs.colormap = XCreateColormap(d, root, vinfo.visual, AllocNone);
    attrs.background_pixel = 0;
    attrs.border_pixel = 0;

    
    // start ti create the borders for the rectangle
    Window line1 = XCreateWindow(
            d, root,
            x, y, linewidth, h, 0,
            vinfo.depth, InputOutput,
            vinfo.visual,
            CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel, &attrs
    );

    Window line2 = XCreateWindow(
            d, root,
            x + w - linewidth, y, linewidth, h, 0,
            vinfo.depth, InputOutput,
            vinfo.visual,
            CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel, &attrs
    );

    Window line3 = XCreateWindow(
            d, root,
            x + linewidth, y, w - 2 * linewidth, linewidth, 0,
            vinfo.depth, InputOutput,
            vinfo.visual,
            CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel, &attrs
    );

    Window line4 = XCreateWindow(
            d, root,
            x + linewidth, y + h - linewidth, w - 2 * linewidth, linewidth, 0,
            vinfo.depth, InputOutput,
            vinfo.visual,
            CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel, &attrs
    );


    XMapWindow(d, line1);
    cairo_surface_t *surf = cairo_xlib_surface_create(d, line1, vinfo.visual, linewidth, h);
    cairo_t *cr1 = cairo_create(surf);
    draw(cr1, linewidth, h);

    XMapWindow(d, line2);
    cairo_surface_t *surf2 = cairo_xlib_surface_create(d, line2, vinfo.visual, linewidth, h);
    cairo_t *cr2 = cairo_create(surf2);
    draw(cr2, linewidth, h);

    XMapWindow(d, line3);
    cairo_surface_t *surf3 = cairo_xlib_surface_create(d, line3, vinfo.visual, w - 2 * linewidth, linewidth);
    cairo_t *cr3 = cairo_create(surf3);
    draw(cr3, w - 2 * linewidth, linewidth);

    XMapWindow(d, line4);
    cairo_surface_t *surf4 = cairo_xlib_surface_create(d, line4, vinfo.visual, w - 2 * linewidth, linewidth);
    cairo_t *cr4 = cairo_create(surf4);
    draw(cr4, w - 2 * linewidth, linewidth);

    XFlush(d);
    usleep(100000000);
}
