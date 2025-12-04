#include "GaussianHandler.h"

#include <algorithm>  // for max
#include <cmath>      // for abs, exp

#include "control/Control.h"                       // for Control
#include "control/settings/Settings.h"             // for Settings
#include "control/tools/BaseShapeHandler.h"        // for BaseShapeHandler
#include "control/tools/SnapToGridInputHandler.h"  // for SnapToGridInputHan...
#include "model/Point.h"                           // for Point

GaussianHandler::GaussianHandler(Control* control, const PageRef& page, bool flipShift, bool flipControl):
        BaseShapeHandler(control, page, flipShift, flipControl) {}

GaussianHandler::~GaussianHandler() = default;

auto GaussianHandler::createShape(bool isAltDown, bool isShiftDown, bool isControlDown)
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
    auto npts = static_cast<unsigned int>(std::max(24.0, std::abs(width * 2.0)));

    double start_x = this->startPoint.x;
    double start_y = this->startPoint.y + height;

    std::pair<std::vector<Point>, Range> res;
    std::vector<Point>& shape = res.first;
    shape.reserve(npts + 1);

    // Draw Gaussian curve: y = exp(-x^2) for x in [-2.5, 2.5]
    for (unsigned int j = 0; j <= npts; j++) {
        double x = -2.5 + j * 5.0 / npts;
        double y = std::exp(-x * x);
        shape.emplace_back(start_x + x * width / 2.5, start_y - y * height);
    }

    // Compute bounding box
    Range rg(start_x - std::abs(width), start_y);
    rg.addPoint(start_x + std::abs(width), start_y - height);
    res.second = rg;

    return res;
}
