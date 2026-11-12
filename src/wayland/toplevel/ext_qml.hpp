#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>

#include "../../core/doc.hpp"
#include "../../core/model.hpp"

Q_MOC_INCLUDE("ext_toplevel.hpp")

namespace qs::wayland::toplevel {

namespace ext {
class ToplevelHandle;
} // namespace ext

///! Window from another application, as listed by ext-foreign-toplevel-list-v1.
/// A window/toplevel from another application, retrievable from
/// the @@ExtToplevelList.
///
/// An ExtToplevel and the @@Toplevel of the same window are separate objects.
/// Neither protocol provides a way to tell which ones belong together.
class ExtToplevel: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("ExtToplevels must be acquired from the ExtToplevelList.");
	/// Identifier given to the window by the compositor. Unique to this window
	/// and never reused.
	Q_PROPERTY(QString identifier READ identifier CONSTANT);
	Q_PROPERTY(QString appId READ appId NOTIFY appIdChanged);
	Q_PROPERTY(QString title READ title NOTIFY titleChanged);

public:
	explicit ExtToplevel(ext::ToplevelHandle* handle, QObject* parent);

	[[nodiscard]] QString identifier() const;
	[[nodiscard]] QString appId() const;
	[[nodiscard]] QString title() const;

	[[nodiscard]] ext::ToplevelHandle* implHandle() const { return this->handle; }

signals:
	void closed();
	void appIdChanged();
	void titleChanged();

private slots:
	void onClosed();

private:
	ext::ToplevelHandle* handle;
};

class ExtToplevelList: public QObject {
	Q_OBJECT;

public:
	[[nodiscard]] ObjectModel<ExtToplevel>* toplevels();

	static ExtToplevelList* instance();

private slots:
	void onToplevelReady(qs::wayland::toplevel::ext::ToplevelHandle* handle);
	void onToplevelClosed();

private:
	explicit ExtToplevelList();

	ObjectModel<ExtToplevel> mToplevels {this};
};

///! Exposes a list of ExtToplevels.
/// Exposes a list of windows from other applications as @@ExtToplevel$s via the
/// [ext-foreign-toplevel-list-v1](https://wayland.app/protocols/ext-foreign-toplevel-list-v1)
/// wayland protocol.
///
/// The list stays empty if the compositor does not support the protocol.
class ExtToplevelListQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(ExtToplevelList);
	QML_SINGLETON;
	// clang-format off
	/// All toplevel windows exposed by the compositor.
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::wayland::toplevel::ExtToplevel>*);
	Q_PROPERTY(UntypedObjectModel* toplevels READ toplevels CONSTANT);
	// clang-format on

public:
	explicit ExtToplevelListQml(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] static ObjectModel<ExtToplevel>* toplevels();
};

} // namespace qs::wayland::toplevel
