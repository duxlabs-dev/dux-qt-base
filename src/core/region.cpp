#include "region.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <qobject.h>
#include <qpoint.h>
#include <qqmllist.h>
#include <qquickitem.h>
#include <qregion.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvectornd.h>

namespace {

struct Radii {
	double rx = 0;
	double ry = 0;
};

// Distance the shape's side is pulled in at height y by an elliptical corner whose arc
// begins at `edge` and spans ry towards `sign` (1 down from a top edge, -1 up from a bottom edge).
double cornerInset(const Radii& c, double edge, double sign, double y) {
	if (c.rx <= 0 || c.ry <= 0) return 0;
	auto dy = (edge + sign * c.ry - y) * sign / c.ry;
	return dy > 0 ? c.rx - c.rx * std::sqrt(std::max(0.0, 1 - dy * dy)) : 0;
}

struct RoundedRect {
	QRectF rect;
	Radii tl {};
	Radii tr {};
	Radii bl {};
	Radii br {};

	[[nodiscard]] double left(double y) const {
		return rect.left()
		     + std::max(cornerInset(tl, rect.top(), 1, y), cornerInset(bl, rect.bottom(), -1, y));
	}

	[[nodiscard]] double right(double y) const {
		return rect.right()
		     - std::max(cornerInset(tr, rect.top(), 1, y), cornerInset(br, rect.bottom(), -1, y));
	}
};

// Rasterizes the shape into whole pixels that are all inside it, or all touched by it.
QRegion rasterize(const RoundedRect& shape, bool inside) {
	constexpr auto eps = 1e-4;
	const auto& rect = shape.rect;
	QRegion region;
	if (rect.width() <= 0 || rect.height() <= 0) return region;

	auto first =
	    static_cast<int>(inside ? std::ceil(rect.top() - eps) : std::floor(rect.top() + eps));
	auto last =
	    static_cast<int>(inside ? std::floor(rect.bottom() + eps) : std::ceil(rect.bottom() - eps));

	QRect pending;
	for (auto row = first; row < last; row++) {
		auto top = std::max<double>(row, rect.top());
		auto bottom = std::min<double>(row + 1, rect.bottom());

		// Both edges are monotonic between their corner arcs, so the extremes are at
		// the row ends or where an arc begins.
		constexpr auto inf = std::numeric_limits<double>::infinity();
		auto left = inside ? -inf : inf;
		auto right = inside ? inf : -inf;
		std::array<double, 6> samples {
		    top,
		    bottom,
		    rect.top() + shape.tl.ry,
		    rect.top() + shape.tr.ry,
		    rect.bottom() - shape.bl.ry,
		    rect.bottom() - shape.br.ry,
		};
		for (auto y: samples) {
			if (y < top || y > bottom) continue;
			if (inside) {
				left = std::max(left, shape.left(y));
				right = std::min(right, shape.right(y));
			} else {
				left = std::min(left, shape.left(y));
				right = std::max(right, shape.right(y));
			}
		}

		auto x0 = static_cast<int>(inside ? std::ceil(left - eps) : std::floor(left + eps));
		auto x1 = static_cast<int>(inside ? std::floor(right + eps) : std::ceil(right - eps));
		if (x1 <= x0) continue;

		if (!pending.isNull() && pending.x() == x0 && pending.right() + 1 == x1
		    && pending.bottom() + 1 == row)
		{
			pending.setBottom(row);
		} else {
			region |= pending;
			pending = QRect(x0, row, x1 - x0, 1);
		}
	}

	region |= pending;
	return region;
}

// Scales each corner down by the tightest of its two edges so adjacent corners don't exceed them.
void normalizeRadii(double& tl, double& tr, double& bl, double& br, double w, double h) {
	auto topScale = tl + tr > w ? w / (tl + tr) : 1.0;
	auto bottomScale = bl + br > w ? w / (bl + br) : 1.0;
	auto leftScale = tl + bl > h ? h / (tl + bl) : 1.0;
	auto rightScale = tr + br > h ? h / (tr + br) : 1.0;

	tl *= std::min(topScale, leftScale);
	tr *= std::min(topScale, rightScale);
	bl *= std::min(bottomScale, leftScale);
	br *= std::min(bottomScale, rightScale);
}

} // namespace

PendingRegion::PendingRegion(QObject* parent): QObject(parent) {
	QObject::connect(this, &PendingRegion::shapeChanged, this, &PendingRegion::changed);
	QObject::connect(this, &PendingRegion::intersectionChanged, this, &PendingRegion::changed);
	QObject::connect(this, &PendingRegion::itemChanged, this, &PendingRegion::changed);
	QObject::connect(this, &PendingRegion::xChanged, this, &PendingRegion::changed);
	QObject::connect(this, &PendingRegion::yChanged, this, &PendingRegion::changed);
	QObject::connect(this, &PendingRegion::widthChanged, this, &PendingRegion::changed);
	QObject::connect(this, &PendingRegion::heightChanged, this, &PendingRegion::changed);
	QObject::connect(this, &PendingRegion::radiusChanged, this, &PendingRegion::changed);
	QObject::connect(this, &PendingRegion::topLeftRadiusChanged, this, &PendingRegion::changed);
	QObject::connect(this, &PendingRegion::topRightRadiusChanged, this, &PendingRegion::changed);
	QObject::connect(this, &PendingRegion::bottomLeftRadiusChanged, this, &PendingRegion::changed);
	QObject::connect(this, &PendingRegion::bottomRightRadiusChanged, this, &PendingRegion::changed);
	QObject::connect(this, &PendingRegion::childrenChanged, this, &PendingRegion::changed);
}

void PendingRegion::setItem(QQuickItem* item) {
	if (item == this->mItem) return;

	if (this->mItem != nullptr) {
		QObject::disconnect(this->mItem, nullptr, this, nullptr);
	}

	this->mItem = item;

	if (item != nullptr) {
		QObject::connect(this->mItem, &QObject::destroyed, this, &PendingRegion::onItemDestroyed);
		QObject::connect(this->mItem, &QQuickItem::xChanged, this, &PendingRegion::itemChanged);
		QObject::connect(this->mItem, &QQuickItem::yChanged, this, &PendingRegion::itemChanged);
		QObject::connect(this->mItem, &QQuickItem::widthChanged, this, &PendingRegion::itemChanged);
		QObject::connect(this->mItem, &QQuickItem::heightChanged, this, &PendingRegion::itemChanged);
	}

	emit this->itemChanged();
}

void PendingRegion::onItemDestroyed() { this->mItem = nullptr; }

void PendingRegion::onChildDestroyed() { this->mRegions.removeAll(this->sender()); }

qreal PendingRegion::radius() const { return this->mRadius; }

void PendingRegion::setRadius(qreal radius) {
	if (radius == this->mRadius) return;
	this->mRadius = radius;
	emit this->radiusChanged();

	if (!(this->mCornerOverrides & TopLeft)) emit this->topLeftRadiusChanged();
	if (!(this->mCornerOverrides & TopRight)) emit this->topRightRadiusChanged();
	if (!(this->mCornerOverrides & BottomLeft)) emit this->bottomLeftRadiusChanged();
	if (!(this->mCornerOverrides & BottomRight)) emit this->bottomRightRadiusChanged();
}

qreal PendingRegion::topLeftRadius() const {
	return (this->mCornerOverrides & TopLeft) ? this->mTopLeftRadius : this->mRadius;
}

void PendingRegion::setTopLeftRadius(qreal radius) {
	this->mTopLeftRadius = radius;
	this->mCornerOverrides |= TopLeft;
	emit this->topLeftRadiusChanged();
}

void PendingRegion::resetTopLeftRadius() {
	this->mCornerOverrides &= ~TopLeft;
	emit this->topLeftRadiusChanged();
}

qreal PendingRegion::topRightRadius() const {
	return (this->mCornerOverrides & TopRight) ? this->mTopRightRadius : this->mRadius;
}

void PendingRegion::setTopRightRadius(qreal radius) {
	this->mTopRightRadius = radius;
	this->mCornerOverrides |= TopRight;
	emit this->topRightRadiusChanged();
}

void PendingRegion::resetTopRightRadius() {
	this->mCornerOverrides &= ~TopRight;
	emit this->topRightRadiusChanged();
}

qreal PendingRegion::bottomLeftRadius() const {
	return (this->mCornerOverrides & BottomLeft) ? this->mBottomLeftRadius : this->mRadius;
}

void PendingRegion::setBottomLeftRadius(qreal radius) {
	this->mBottomLeftRadius = radius;
	this->mCornerOverrides |= BottomLeft;
	emit this->bottomLeftRadiusChanged();
}

void PendingRegion::resetBottomLeftRadius() {
	this->mCornerOverrides &= ~BottomLeft;
	emit this->bottomLeftRadiusChanged();
}

qreal PendingRegion::bottomRightRadius() const {
	return (this->mCornerOverrides & BottomRight) ? this->mBottomRightRadius : this->mRadius;
}

void PendingRegion::setBottomRightRadius(qreal radius) {
	this->mBottomRightRadius = radius;
	this->mCornerOverrides |= BottomRight;
	emit this->bottomRightRadiusChanged();
}

void PendingRegion::resetBottomRightRadius() {
	this->mCornerOverrides &= ~BottomRight;
	emit this->bottomRightRadiusChanged();
}

QQmlListProperty<PendingRegion> PendingRegion::regions() {
	return QQmlListProperty<PendingRegion>(
	    this,
	    nullptr,
	    &PendingRegion::regionsAppend,
	    &PendingRegion::regionsCount,
	    &PendingRegion::regionAt,
	    &PendingRegion::regionsClear,
	    &PendingRegion::regionsReplace,
	    &PendingRegion::regionsRemoveLast
	);
}

bool PendingRegion::empty() const {
	return this->mItem == nullptr && this->mX == 0 && this->mY == 0 && this->mWidth == 0
	    && this->mHeight == 0;
}

QRegion PendingRegion::build(Bound bound) const {
	if (bound != Bound::Exact) return this->buildBounded(bound);

	auto type = QRegion::Rectangle;
	switch (this->mShape) {
	case RegionShape::Rect: type = QRegion::Rectangle; break;
	case RegionShape::Ellipse: type = QRegion::Ellipse; break;
	}

	QRegion region;

	if (this->empty()) {
		region = QRegion();
	} else if (this->mItem != nullptr) {
		auto origin = this->mItem->mapToScene(QPointF(0, 0));
		auto extent = this->mItem->mapToScene(QPointF(this->mItem->width(), this->mItem->height()));
		auto size = extent - origin;

		region = QRegion(
		    static_cast<int>(origin.x()),
		    static_cast<int>(origin.y()),
		    static_cast<int>(std::ceil(size.x())),
		    static_cast<int>(std::ceil(size.y())),
		    type
		);
	} else {
		region = QRegion(this->mX, this->mY, this->mWidth, this->mHeight, type);
	}

	if (this->mShape == RegionShape::Rect && !region.isEmpty()) {
		// Clamped first, since casting NaN, infinity or out of range values to int is undefined.
		auto whole = [](qreal r) {
			return static_cast<qint32>(std::fmin(std::fmax(r, 0.0), std::numeric_limits<qint32>::max()));
		};
		auto tl = whole(this->topLeftRadius());
		auto tr = whole(this->topRightRadius());
		auto bl = whole(this->bottomLeftRadius());
		auto br = whole(this->bottomRightRadius());

		if (tl > 0 || tr > 0 || bl > 0 || br > 0) {
			auto rect = region.boundingRect();
			auto x = rect.x();
			auto y = rect.y();
			auto w = rect.width();
			auto h = rect.height();

			auto ntl = static_cast<double>(tl);
			auto ntr = static_cast<double>(tr);
			auto nbl = static_cast<double>(bl);
			auto nbr = static_cast<double>(br);
			normalizeRadii(ntl, ntr, nbl, nbr, w, h);
			tl = static_cast<qint32>(ntl);
			tr = static_cast<qint32>(ntr);
			bl = static_cast<qint32>(nbl);
			br = static_cast<qint32>(nbr);

			// Unlock each corner: subtract (cornerBox - quarterEllipse) from the
			// full rect. Each corner only modifies pixels inside its own box,
			// so no diagonal overlap is possible.
			if (tl > 0) {
				auto box = QRegion(x, y, tl, tl);
				auto ellipse = QRegion(x, y, tl * 2, tl * 2, QRegion::Ellipse);
				region -= box - (ellipse & box);
			}

			if (tr > 0) {
				auto box = QRegion(x + w - tr, y, tr, tr);
				auto ellipse = QRegion(x + w - tr * 2, y, tr * 2, tr * 2, QRegion::Ellipse);
				region -= box - (ellipse & box);
			}

			if (bl > 0) {
				auto box = QRegion(x, y + h - bl, bl, bl);
				auto ellipse = QRegion(x, y + h - bl * 2, bl * 2, bl * 2, QRegion::Ellipse);
				region -= box - (ellipse & box);
			}

			if (br > 0) {
				auto box = QRegion(x + w - br, y + h - br, br, br);
				auto ellipse = QRegion(x + w - br * 2, y + h - br * 2, br * 2, br * 2, QRegion::Ellipse);
				region -= box - (ellipse & box);
			}
		}
	}

	for (const auto& childRegion: this->mRegions) {
		region = childRegion->applyTo(region);
	}

	return region;
}

QRegion PendingRegion::buildBounded(Bound bound) const {
	QRegion region;

	if (!this->empty()) {
		QRectF rect;
		if (this->mItem != nullptr) {
			auto origin = this->mItem->mapToScene(QPointF(0, 0));
			auto extent = this->mItem->mapToScene(QPointF(this->mItem->width(), this->mItem->height()));
			rect = QRectF(origin, extent);
		} else {
			rect = QRectF(this->mX, this->mY, this->mWidth, this->mHeight);
		}

		RoundedRect shape {.rect = rect};
		if (this->mShape == RegionShape::Ellipse) {
			Radii r {.rx = rect.width() / 2, .ry = rect.height() / 2};
			shape.tl = shape.tr = shape.bl = shape.br = r;
		} else {
			auto w = rect.width();
			auto h = rect.height();
			// fmax/fmin also turn NaN and infinite radii into usable values.
			auto limit = std::max(w, h);
			auto clamp = [limit](qreal r) { return std::fmin(std::fmax(r, 0.0), limit); };
			auto tl = clamp(this->topLeftRadius());
			auto tr = clamp(this->topRightRadius());
			auto bl = clamp(this->bottomLeftRadius());
			auto br = clamp(this->bottomRightRadius());
			normalizeRadii(tl, tr, bl, br, w, h);
			shape.tl = {.rx = tl, .ry = tl};
			shape.tr = {.rx = tr, .ry = tr};
			shape.bl = {.rx = bl, .ry = bl};
			shape.br = {.rx = br, .ry = br};
		}

		region = rasterize(shape, bound == Bound::Inside);
	}

	for (const auto& childRegion: this->mRegions) {
		region = childRegion->applyTo(region, bound);
	}

	return region;
}

QRegion PendingRegion::applyTo(QRegion& region, Bound bound) const {
	// What is subtracted has to cover its painted shape for the result to stay inside the
	// parent's, so it is built on the opposite bound.
	auto childBound = bound;
	if (this->mIntersection == Intersection::Subtract && bound != Bound::Exact) {
		childBound = bound == Bound::Inside ? Bound::Outside : Bound::Inside;
	}

	auto child = this->build(childBound);
	switch (this->mIntersection) {
	case Intersection::Combine: region = region.united(child); break;
	case Intersection::Subtract: region = region.subtracted(child); break;
	case Intersection::Intersect: region = region.intersected(child); break;
	case Intersection::Xor: region = region.xored(child); break;
	}

	return region;
}

QRegion PendingRegion::applyTo(const QRect& rect, Bound bound) const {
	// if left as the default, don't combine it with the whole rect area, leave it as is.
	if (this->mIntersection == Intersection::Combine) {
		return this->build(bound);
	} else {
		auto baseRegion = QRegion(rect);
		return this->applyTo(baseRegion, bound);
	}
}

void PendingRegion::regionsAppend(QQmlListProperty<PendingRegion>* prop, PendingRegion* region) {
	auto* self = static_cast<PendingRegion*>(prop->object); // NOLINT
	if (!region) return;

	QObject::connect(region, &QObject::destroyed, self, &PendingRegion::onChildDestroyed);
	QObject::connect(region, &PendingRegion::changed, self, &PendingRegion::childrenChanged);

	self->mRegions.append(region);

	emit self->childrenChanged();
}

PendingRegion* PendingRegion::regionAt(QQmlListProperty<PendingRegion>* prop, qsizetype i) {
	return static_cast<PendingRegion*>(prop->object)->mRegions.at(i); // NOLINT
}

void PendingRegion::regionsClear(QQmlListProperty<PendingRegion>* prop) {
	auto* self = static_cast<PendingRegion*>(prop->object); // NOLINT

	for (auto* region: self->mRegions) {
		QObject::disconnect(region, nullptr, self, nullptr);
	}

	self->mRegions.clear(); // NOLINT
	emit self->childrenChanged();
}

qsizetype PendingRegion::regionsCount(QQmlListProperty<PendingRegion>* prop) {
	return static_cast<PendingRegion*>(prop->object)->mRegions.length(); // NOLINT
}

void PendingRegion::regionsRemoveLast(QQmlListProperty<PendingRegion>* prop) {
	auto* self = static_cast<PendingRegion*>(prop->object); // NOLINT

	auto* last = self->mRegions.last();
	if (last != nullptr) QObject::disconnect(last, nullptr, self, nullptr);

	self->mRegions.removeLast();
	emit self->childrenChanged();
}

void PendingRegion::regionsReplace(
    QQmlListProperty<PendingRegion>* prop,
    qsizetype i,
    PendingRegion* region
) {
	auto* self = static_cast<PendingRegion*>(prop->object); // NOLINT

	auto* old = self->mRegions.at(i);
	if (old != nullptr) QObject::disconnect(old, nullptr, self, nullptr);

	self->mRegions.replace(i, region);
	emit self->childrenChanged();
}
