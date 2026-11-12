#include "ext_toplevel.hpp"

#include <qcontainerfwd.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qwaylandclientextension.h>

#include "../../core/logcat.hpp"
#include "wayland-ext-foreign-toplevel-list-v1-client-protocol.h"

namespace qs::wayland::toplevel::ext {

QS_LOGGING_CATEGORY(logToplevelList, "quickshell.wayland.toplevelList", QtWarningMsg);

ToplevelList::ToplevelList(): QWaylandClientExtensionTemplate(1) { this->initialize(); }

const QVector<ToplevelHandle*>& ToplevelList::readyToplevels() const {
	return this->mReadyToplevels;
}

ToplevelList* ToplevelList::instance() {
	static auto* instance = new ToplevelList(); // NOLINT
	return instance;
}

void ToplevelList::ext_foreign_toplevel_list_v1_toplevel(
    ::ext_foreign_toplevel_handle_v1* toplevel
) {
	auto* handle = new ToplevelHandle();
	QObject::connect(handle, &ToplevelHandle::closed, this, &ToplevelList::onToplevelClosed);
	QObject::connect(handle, &ToplevelHandle::ready, this, &ToplevelList::onToplevelReady);

	qCDebug(logToplevelList) << "Toplevel handle created" << handle;

	// Not done in constructor as a close could technically be picked up immediately on init,
	// making touching the handle a UAF.
	handle->init(toplevel);
}

void ToplevelList::ext_foreign_toplevel_list_v1_finished() {
	qCWarning(logToplevelList) << "ext_foreign_toplevel_list_v1.finished() was received";
}

void ToplevelList::onToplevelReady() {
	auto* handle = qobject_cast<ToplevelHandle*>(this->sender());
	this->mReadyToplevels.push_back(handle);
	emit this->toplevelReady(handle);
}

void ToplevelList::onToplevelClosed() {
	auto* handle = qobject_cast<ToplevelHandle*>(this->sender());
	this->mReadyToplevels.removeOne(handle);
}

QString ToplevelHandle::identifier() const { return this->mIdentifier; }
QString ToplevelHandle::appId() const { return this->mAppId; }
QString ToplevelHandle::title() const { return this->mTitle; }

void ToplevelHandle::ext_foreign_toplevel_handle_v1_closed() {
	qCDebug(logToplevelList) << this << "closed";
	this->destroy();
	emit this->closed();
	delete this;
}

void ToplevelHandle::ext_foreign_toplevel_handle_v1_done() {
	qCDebug(logToplevelList) << this << "got done";

	if (this->pendingAppId != this->mAppId) {
		this->mAppId = this->pendingAppId;
		emit this->appIdChanged();
	}

	if (this->pendingTitle != this->mTitle) {
		this->mTitle = this->pendingTitle;
		emit this->titleChanged();
	}

	if (!this->isReady) {
		this->isReady = true;
		emit this->ready();
	}
}

void ToplevelHandle::ext_foreign_toplevel_handle_v1_title(const QString& title) {
	qCDebug(logToplevelList) << this << "got title" << title;
	this->pendingTitle = title;
}

void ToplevelHandle::ext_foreign_toplevel_handle_v1_app_id(const QString& appId) {
	qCDebug(logToplevelList) << this << "got appid" << appId;
	this->pendingAppId = appId;
}

void ToplevelHandle::ext_foreign_toplevel_handle_v1_identifier(const QString& identifier) {
	qCDebug(logToplevelList) << this << "got identifier" << identifier;
	this->mIdentifier = identifier;
}

} // namespace qs::wayland::toplevel::ext
