#pragma once

#include <qcontainerfwd.h>
#include <qobject.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qwayland-ext-foreign-toplevel-list-v1.h>
#include <qwaylandclientextension.h>

#include "../../core/logcat.hpp"
#include "wayland-ext-foreign-toplevel-list-v1-client-protocol.h"

namespace qs::wayland::toplevel::ext {

QS_DECLARE_LOGGING_CATEGORY(logToplevelList);

// NOLINTNEXTLINE(misc-multiple-inheritance)
class ToplevelHandle
    : public QObject
    , public QtWayland::ext_foreign_toplevel_handle_v1 {
	Q_OBJECT;

public:
	[[nodiscard]] QString identifier() const;
	[[nodiscard]] QString appId() const;
	[[nodiscard]] QString title() const;

signals:
	// sent after the first done event.
	void ready();
	// sent right before delete this.
	void closed();

	void appIdChanged();
	void titleChanged();

protected:
	void ext_foreign_toplevel_handle_v1_closed() override;
	void ext_foreign_toplevel_handle_v1_done() override;
	void ext_foreign_toplevel_handle_v1_title(const QString& title) override;
	void ext_foreign_toplevel_handle_v1_app_id(const QString& appId) override;
	void ext_foreign_toplevel_handle_v1_identifier(const QString& identifier) override;

private:
	bool isReady = false;
	QString mIdentifier;
	QString mAppId;
	QString mTitle;
	QString pendingAppId;
	QString pendingTitle;
};

// NOLINTNEXTLINE(misc-multiple-inheritance)
class ToplevelList
    : public QWaylandClientExtensionTemplate<ToplevelList>
    , public QtWayland::ext_foreign_toplevel_list_v1 {
	Q_OBJECT;

public:
	[[nodiscard]] const QVector<ToplevelHandle*>& readyToplevels() const;

	static ToplevelList* instance();

signals:
	void toplevelReady(qs::wayland::toplevel::ext::ToplevelHandle* toplevel);

protected:
	explicit ToplevelList();

	void ext_foreign_toplevel_list_v1_toplevel(::ext_foreign_toplevel_handle_v1* toplevel) override;
	void ext_foreign_toplevel_list_v1_finished() override;

private slots:
	void onToplevelReady();
	void onToplevelClosed();

private:
	QVector<ToplevelHandle*> mReadyToplevels;
};

} // namespace qs::wayland::toplevel::ext
