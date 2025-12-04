#include "PolynomialHandler.h"

#include <algorithm>  // for max
#include <cmath>      // for abs, pow

#include "control/Control.h"                       // for Control
#include "control/settings/Settings.h"             // for Settings
#include "control/tools/BaseShapeHandler.h"        // for BaseShapeHandler
#include "control/tools/SnapToGridInputHandler.h"  // for SnapToGridInputHan...
#include "model/Point.h"                           // for Point

PolynomialHandler::PolynomialHandler(Control* control, const PageRef& page, bool flipShift, bool flipControl):
        BaseShapeHandler(control, page, flipShift, flipControl) {}

PolynomialHandler::~PolynomialHandler() = default;

auto PolynomialHandler::createShape(bool isAltDown, bool isShiftDown, bool isControlDown)
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

    // Click point is at x=0, y=0 of the function (origin/vertex)
    double origin_x = this->startPoint.x;
    double origin_y = this->startPoint.y;

    std::pair<std::vector<Point>, Range> res;
    std::vector<Point>& shape = res.first;
    shape.reserve(npts + 1);

    // Draw polynomial curve based on modifiers:
    // No modifier: x^2
    // Control: x^3
    // Shift: x^4
    // Shift+Control: x^5
    // The curve is symmetric around the click point (origin)
    // t goes from -1 to 1, so the curve extends equally on both sides
    // width determines the horizontal extent (half on each side from origin)
    // height determines the vertical extent at t=1 (and t=-1 for even powers)
    for (unsigned int j = 0; j <= npts; j++) {
        double t = -1.0 + 2.0 * j / npts;  // t goes from -1 to 1
        double y;
        if (!modShift && !modControl) {
            // x^2
            y = t * t;
        } else if (!modShift && modControl) {
            // x^3
            y = t * t * t;
        } else if (modShift && !modControl) {
            // x^4
            y = t * t * t * t;
        } else {
            // x^5
            y = t * t * t * t * t;
        }
        shape.emplace_back(origin_x + t * width, origin_y + y * height);
    }

    // Compute bounding box - curve extends from -width to +width horizontally
    double minX = std::min(origin_x - width, origin_x + width);
    double maxX = std::max(origin_x - width, origin_x + width);
    // For even powers, y ranges from 0 to height; for odd powers, from -height to height
    double minY, maxY;
    if ((!modShift && modControl) || (modShift && modControl)) {
        // Odd powers (x^3, x^5): y ranges from -1 to 1
        minY = std::min(origin_y - height, origin_y + height);
        maxY = std::max(origin_y - height, origin_y + height);
    } else {
        // Even powers (x^2, x^4): y ranges from 0 to 1
        minY = std::min(origin_y, origin_y + height);
        maxY = std::max(origin_y, origin_y + height);
    }
    Range rg(minX, minY);
    rg.addPoint(maxX, maxY);
    res.second = rg;

    return res;
}
