#include "curvestroke.hpp"
#include <optional>

#include <private/qsgcurvestrokenode_p.h>
#include <qsgnode.h>

namespace qs::window {

std::optional<CurveStroke> curveStroke(QSGGeometryNode* node) {
	auto* stroke = dynamic_cast<QSGCurveStrokeNode*>(node);
	if (!stroke) return std::nullopt;

	return CurveStroke {.scale = stroke->localScale(), .width = stroke->strokeWidth()};
}

} // namespace qs::window
