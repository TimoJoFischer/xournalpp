/*
 * Xournal++
 *
 * Handles input to draw Gaussian curves
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <vector>  // for vector

#include "model/PageRef.h"  // for PageRef

#include "BaseShapeHandler.h"  // for BaseShapeHandler

class Point;
class Control;

class GaussianHandler: public BaseShapeHandler {
public:
    GaussianHandler(Control* control, const PageRef& page, bool flipShift = false, bool flipControl = false);
    ~GaussianHandler() override;

private:
    auto createShape(bool isAltDown, bool isShiftDown, bool isControlDown)
            -> std::pair<std::vector<Point>, Range> override;
};
