#pragma once

#include <optional>

#include <qsgnode.h>

namespace qs::window {

// What QSGCurveStrokeMaterialShader applies to a stroke node's vertices besides the node's matrix.
struct CurveStroke {
	// See QSGCurveStrokeNode::localScale().
	float scale = 1;
	// Negative for a cosmetic stroke; see QSGCurveStrokeNode::strokeWidth().
	float width = 0;
};

// Null unless the node is a QSGCurveStrokeNode.
[[nodiscard]] std::optional<CurveStroke> curveStroke(QSGGeometryNode* node);

} // namespace qs::window
