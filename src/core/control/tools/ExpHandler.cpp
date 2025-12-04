#include "ExpHandler.h"

#include <algorithm>  // for max
#include <cmath>      // for abs, exp, log2, M_E

#include "control/Control.h"                       // for Control
#include "control/settings/Settings.h"             // for Settings
#include "control/tools/BaseShapeHandler.h"        // for BaseShapeHandler
#include "control/tools/SnapToGridInputHandler.h"  // for SnapToGridInputHan...
#include "model/Point.h"                           // for Point

ExpHandler::ExpHandler(Control* control, const PageRef& page, bool flipShift, bool flipControl):
        BaseShapeHandler(control, page, flipShift, flipControl) {}

ExpHandler::~ExpHandler() = default;

auto ExpHandler::createShape(bool isAltDown, bool isShiftDown, bool isControlDown)
        -> std::pair<std::vector<Point>, Range> {
    /**
     * Snap point to grid (if enabled - Alt key pressed will toggle)
     */
    Point c = snappingHandler.snapToGrid(this->currPoint, isAltDown);

    double width = c.x - this->startPoint.x;
    double height = c.y - this->startPoint.y;

    this->modControl = isControlDown;

    Settings* settings = control->getSettings();
    if (settings->getDrawDirModsEnabled()) {
        // change modifiers based on draw dir
        this->modifyModifiersByDrawDir(width, height, true);
    }

    // Number of points for the curve
    auto npts = static_cast<unsigned int>(std::max(96.0, std::abs(width * 2.0)));

    // Click point is at x=0 for both exp(x) and ln(x)
    double origin_x = this->startPoint.x;
    double origin_y = this->startPoint.y;

    std::pair<std::vector<Point>, Range> res;
    std::vector<Point>& shape = res.first;
    shape.reserve(npts + 1);

    if (!modControl) {
        // exp(x): draw from x=-4 to x=4
        // At x=0 (click point), exp(0)=1
        // We map: t from 0 to 1 -> x from -4 to 4
        // x_at_cursor = 4 means width corresponds to x from 0 to 4
        // So full width corresponds to x from -4 to 4 (8 units), cursor is at x=4
        for (unsigned int j = 0; j <= npts; j++) {
            double t = static_cast<double>(j) / npts;  // 0 to 1
            double x_val = -4.0 + t * 8.0;  // -4 to 4
            double y_val = std::exp(x_val);  // exp(-4) to exp(4)
            // Normalize: at cursor (x=4), y=exp(4); at click (x=0), y=1
            // Map x: click point at x=0 is at origin_x, cursor at x=4 is at origin_x + width
            // So x_pos = origin_x + (x_val / 4.0) * width
            double x_pos = origin_x + (x_val / 4.0) * width;
            // Map y: y=1 at click, y=exp(4) at cursor
            // y_pos = origin_y + (y_val - 1.0) / (exp(4) - 1.0) * height
            double y_normalized = (y_val - 1.0) / (std::exp(4.0) - 1.0);
            double y_pos = origin_y + y_normalized * height;
            shape.emplace_back(x_pos, y_pos);
        }
    } else {
        // ln(x): draw from x=exp(-4) to x=exp(4)
        // At x=1 (click point), ln(1)=0
        // Cursor at end corresponds to x=exp(4), ln(exp(4))=4
        for (unsigned int j = 0; j <= npts; j++) {
            double t = static_cast<double>(j) / npts;  // 0 to 1
            double x_val = std::exp(-4.0 + t * 8.0);  // exp(-4) to exp(4)
            double y_val = std::log(x_val);  // -4 to 4
            // Normalize: at cursor (x=exp(4)), y=4; at click (x=1), y=0
            // Map x: click point at x=1 is at origin_x, cursor at x=exp(4) is at origin_x + width
            double x_normalized = (x_val - 1.0) / (std::exp(4.0) - 1.0);
            double x_pos = origin_x + x_normalized * width;
            // Map y: y=0 at click, y=4 at cursor
            double y_normalized = y_val / 4.0;
            double y_pos = origin_y + y_normalized * height;
            shape.emplace_back(x_pos, y_pos);
        }
    }

    // Compute bounding box - need to include the full curve extent
    double minX = origin_x - std::abs(width);  // curve extends left of click point
    double maxX = origin_x + std::abs(width);
    double minY = origin_y - std::abs(height);
    double maxY = origin_y + std::abs(height);
    Range rg(minX, minY);
    rg.addPoint(maxX, maxY);
    res.second = rg;

    return res;
}
