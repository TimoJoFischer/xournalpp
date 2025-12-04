#include "SinusHandler.h"

#include <algorithm>  // for max
#include <cmath>      // for abs, sin, M_PI

#include "control/Control.h"                       // for Control
#include "control/settings/Settings.h"             // for Settings
#include "control/tools/BaseShapeHandler.h"        // for BaseShapeHandler
#include "control/tools/SnapToGridInputHandler.h"  // for SnapToGridInputHan...
#include "model/Point.h"                           // for Point

SinusHandler::SinusHandler(Control* control, const PageRef& page, bool flipShift, bool flipControl):
        BaseShapeHandler(control, page, flipShift, flipControl) {}

SinusHandler::~SinusHandler() = default;

auto SinusHandler::createShape(bool isAltDown, bool isShiftDown, bool isControlDown)
        -> std::pair<std::vector<Point>, Range> {
    /**
     * Snap point to grid (if enabled - Alt key pressed will toggle)
     */
    Point c = snappingHandler.snapToGrid(this->currPoint, isAltDown);

    double width = c.x - this->startPoint.x;
    double height = c.y - this->startPoint.y;

    this->modShift = isShiftDown;
    this->modControl = isControlDown;

    Settings* settings = control->getSettings();
    if (settings->getDrawDirModsEnabled()) {
        // change modifiers based on draw dir
        this->modifyModifiersByDrawDir(width, height, true);
    }

    // Number of points for the curve
    auto npts = static_cast<unsigned int>(std::max(48.0, std::abs(width * 2.0)));

    // Click point is at x=0, y=0 of the function
    double origin_x = this->startPoint.x;
    double origin_y = this->startPoint.y;

    // Period depends on modifiers:
    // No modifier: 2 periods
    // Control: 3 periods
    // Shift: 4 periods
    // Shift+Control: 5 periods
    double period;
    if (!modShift && !modControl) {
        period = 2.0;
    } else if (!modShift && modControl) {
        period = 3.0;
    } else if (modShift && !modControl) {
        period = 4.0;
    } else {
        period = 5.0;
    }

    std::pair<std::vector<Point>, Range> res;
    std::vector<Point>& shape = res.first;
    shape.reserve(npts + 1);

    // Draw Sinus curve with origin at click point
    // x ranges from 0 to width, function argument ranges from 0 to period*PI
    for (unsigned int j = 0; j <= npts; j++) {
        double t = static_cast<double>(j) / npts;  // t goes from 0 to 1
        double func_x = t * period * M_PI;
        double y = std::sin(func_x);
        shape.emplace_back(origin_x + t * width, origin_y - y * height);
    }

    // Compute bounding box
    double minX = std::min(origin_x, origin_x + width);
    double maxX = std::max(origin_x, origin_x + width);
    double minY = std::min(origin_y - height, origin_y + height);
    double maxY = std::max(origin_y - height, origin_y + height);
    Range rg(minX, minY);
    rg.addPoint(maxX, maxY);
    res.second = rg;

    return res;
}
