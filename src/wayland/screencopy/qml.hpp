#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

namespace qs::wayland::screencopy {

///! Reports which windows a ScreencopyView can capture.
/// Window capture depends on the protocols the compositor supports.
/// The properties do not update if the compositor adds support while running.
class ScreencopyQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(Screencopy);
	QML_SINGLETON;
	// clang-format off
	/// If a @@Quickshell.Wayland.Toplevel can be used as a @@ScreencopyView.captureSource.
	/// Requires a compositor that supports `hyprland-toplevel-export-v1`.
	Q_PROPERTY(bool canCaptureToplevels READ canCaptureToplevels CONSTANT);
	/// If a @@Quickshell.Wayland.ExtToplevel can be used as a @@ScreencopyView.captureSource.
	/// Requires a compositor that supports `ext-image-copy-capture-v1` and the
	/// foreign toplevel source of `ext-image-capture-source-v1`.
	Q_PROPERTY(bool canCaptureExtToplevels READ canCaptureExtToplevels CONSTANT);
	// clang-format on

public:
	explicit ScreencopyQml(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] static bool canCaptureToplevels();
	[[nodiscard]] static bool canCaptureExtToplevels();
};

} // namespace qs::wayland::screencopy
