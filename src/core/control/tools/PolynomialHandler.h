/*
 * Xournal++
 *
 * Handles input to draw Polynomial curves (x^2, x^3, x^4, x^5)
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

class PolynomialHandler: public BaseShapeHandler {
public:
    PolynomialHandler(Control* control, const PageRef& page, bool flipShift = false, bool flipControl = false);
    ~PolynomialHandler() override;

private:
    auto createShape(bool isAltDown, bool isShiftDown, bool isControlDown)
            -> std::pair<std::vector<Point>, Range> override;
};
