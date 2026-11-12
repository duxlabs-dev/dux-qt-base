#include "ext_qml.hpp"

#include <qobject.h>
#include <qstring.h>
#include <qtmetamacros.h>

#include "../../core/model.hpp"
#include "ext_toplevel.hpp"

namespace qs::wayland::toplevel {

ExtToplevel::ExtToplevel(ext::ToplevelHandle* handle, QObject* parent)
    : QObject(parent)
    , handle(handle) {
	// clang-format off
	QObject::connect(handle, &ext::ToplevelHandle::closed, this, &ExtToplevel::onClosed);
	QObject::connect(handle, &ext::ToplevelHandle::appIdChanged, this, &ExtToplevel::appIdChanged);
	QObject::connect(handle, &ext::ToplevelHandle::titleChanged, this, &ExtToplevel::titleChanged);
	// clang-format on
}

void ExtToplevel::onClosed() {
	emit this->closed();
	delete this;
}

QString ExtToplevel::identifier() const { return this->handle->identifier(); }
QString ExtToplevel::appId() const { return this->handle->appId(); }
QString ExtToplevel::title() const { return this->handle->title(); }

ExtToplevelList::ExtToplevelList() {
	auto* list = ext::ToplevelList::instance();

	QObject::connect(
	    list,
	    &ext::ToplevelList::toplevelReady,
	    this,
	    &ExtToplevelList::onToplevelReady
	);

	for (auto* handle: list->readyToplevels()) {
		this->onToplevelReady(handle);
	}
}

ObjectModel<ExtToplevel>* ExtToplevelList::toplevels() { return &this->mToplevels; }

void ExtToplevelList::onToplevelReady(ext::ToplevelHandle* handle) {
	auto* toplevel = new ExtToplevel(handle, this);
	QObject::connect(toplevel, &ExtToplevel::closed, this, &ExtToplevelList::onToplevelClosed);
	this->mToplevels.insertObject(toplevel);
}

void ExtToplevelList::onToplevelClosed() {
	auto* toplevel = qobject_cast<ExtToplevel*>(this->sender());
	this->mToplevels.removeObject(toplevel);
}

ExtToplevelList* ExtToplevelList::instance() {
	static auto* instance = new ExtToplevelList(); // NOLINT
	return instance;
}

ObjectModel<ExtToplevel>* ExtToplevelListQml::toplevels() {
	return ExtToplevelList::instance()->toplevels();
}

} // namespace qs::wayland::toplevel
