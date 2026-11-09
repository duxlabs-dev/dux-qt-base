#pragma once

#include <qnamespace.h>
#include <qobject.h>
#include <qproperty.h>
#include <qsize.h>
#include <qstring.h>
#include <qstringfwd.h>
#include <qtmetamacros.h>

#include "proxywindow.hpp"
#include "windowinterface.hpp"

// see #include <qpa/qplatformwindow.h>
static const int QWINDOWSIZE_MAX = ((1 << 24) - 1);

class ProxyFloatingWindow: public ProxyWindowBase {
	Q_OBJECT;

public:
	explicit ProxyFloatingWindow(QObject* parent = nullptr);

	void connectWindow() override;
	void completeWindow() override;
	void postCompleteWindow() override;
	void setVisible(bool visible) override;

	[[nodiscard]] QObject* parentWindow() const;
	void setParentWindow(QObject* window);

	[[nodiscard]] QString foreignParentHandle(QLatin1StringView prefix) const;

	using PlatformHook = void (*)(ProxyFloatingWindow* window);
	static void setPlatformHook(PlatformHook hook);

	// Setting geometry while the window is visible makes the content item shrink but not the window
	// which is awful so we disable it for floating windows.
	void trySetWidth(qint32 implicitWidth) override;
	void trySetHeight(qint32 implicitHeight) override;

signals:
	void minimumSizeChanged();
	void maximumSizeChanged();
	void titleChanged();
	void parentWindowChanged();
	void appIdChanged();
	void foreignParentChanged();

private slots:
	void onParentDestroyed();

private:
	void onMinimumSizeChanged();
	void onMaximumSizeChanged();
	void onTitleChanged();
	void targetVisibleChanged();

	QObject* mParentWindow = nullptr;

	Q_OBJECT_BINDABLE_PROPERTY(ProxyFloatingWindow, ProxyWindowBase*, bParentProxyWindow);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(ProxyFloatingWindow, bool, bWantsVisible, true);

	Q_OBJECT_BINDABLE_PROPERTY(
	    ProxyFloatingWindow,
	    bool,
	    bTargetVisible,
	    &ProxyFloatingWindow::targetVisibleChanged
	);

public:
	Q_OBJECT_BINDABLE_PROPERTY(
	    ProxyFloatingWindow,
	    QString,
	    bTitle,
	    &ProxyFloatingWindow::onTitleChanged
	);

	Q_OBJECT_BINDABLE_PROPERTY(
	    ProxyFloatingWindow,
	    QString,
	    bAppId,
	    &ProxyFloatingWindow::appIdChanged
	);

	Q_OBJECT_BINDABLE_PROPERTY(
	    ProxyFloatingWindow,
	    QString,
	    bForeignParent,
	    &ProxyFloatingWindow::foreignParentChanged
	);

	Q_OBJECT_BINDABLE_PROPERTY(
	    ProxyFloatingWindow,
	    QSize,
	    bMinimumSize,
	    &ProxyFloatingWindow::onMinimumSizeChanged
	);

	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(
	    ProxyFloatingWindow,
	    QSize,
	    bMaximumSize,
	    QSize(QWINDOWSIZE_MAX, QWINDOWSIZE_MAX),
	    &ProxyFloatingWindow::onMaximumSizeChanged
	);
};

///! Standard toplevel operating system window that looks like any other application.
class FloatingWindowInterface: public WindowInterface {
	Q_OBJECT;
	// clang-format off
	/// Window title.
	Q_PROPERTY(QString title READ default WRITE default NOTIFY titleChanged BINDABLE bindableTitle);
	/// Minimum window size given to the window system.
	Q_PROPERTY(QSize minimumSize READ default WRITE default NOTIFY minimumSizeChanged BINDABLE bindableMinimumSize);
	/// Maximum window size given to the window system.
	Q_PROPERTY(QSize maximumSize READ default WRITE default NOTIFY maximumSizeChanged BINDABLE bindableMaximumSize);
	/// Whether the window is currently minimized.
	Q_PROPERTY(bool minimized READ isMinimized WRITE setMinimized NOTIFY minimizedChanged);
	/// Whether the window is currently maximized.
	Q_PROPERTY(bool maximized READ isMaximized WRITE setMaximized NOTIFY maximizedChanged);
	/// Whether the window is currently fullscreen.
	Q_PROPERTY(bool fullscreen READ isFullscreen WRITE setFullscreen NOTIFY fullscreenChanged);
	/// The parent window of this window. Setting this makes the window a child of the parent,
	/// which affects window stacking behavior.
	///
	/// > [!NOTE] This property cannot be changed after the window is visible.
	Q_PROPERTY(QObject* parentWindow READ parentWindow WRITE setParentWindow NOTIFY parentWindowChanged);
	/// Application id the window system uses to group the window, match it to a desktop entry
	/// and apply window rules. Empty uses the instance's app id (`//@ pragma AppId`).
	///
	/// Sets the xdg_toplevel app id on Wayland and `WM_CLASS` on X11. Other platforms ignore it.
	///
	/// > [!NOTE] Window rules decided when the window was first shown may not be re-evaluated
	/// > when this changes while the window is visible.
	Q_PROPERTY(QString appId READ default WRITE default NOTIFY appIdChanged BINDABLE bindableAppId);
	/// Window of another application to make this window a child of, as a window identifier in the
	/// [xdg-desktop-portal format]: `wayland:<handle>` with a handle exported through
	/// xdg-foreign-unstable-v2, or `x11:<window id in hex>`. Identifiers for another platform are
	/// ignored, and so is this property when @@parentWindow is set.
	///
	/// On X11 the window is marked as a dialog so the window manager honors the parent.
	///
	/// [xdg-desktop-portal format]: https://flatpak.github.io/xdg-desktop-portal/docs/window-identifiers.html
	Q_PROPERTY(QString foreignParent READ default WRITE default NOTIFY foreignParentChanged BINDABLE bindableForeignParent);
	// clang-format on
	QML_NAMED_ELEMENT(FloatingWindow);

public:
	explicit FloatingWindowInterface(QObject* parent = nullptr);

	void onReload(QObject* oldInstance) override;

	[[nodiscard]] ProxyWindowBase* proxyWindow() const override;

	[[nodiscard]] QBindable<QSize> bindableMinimumSize() { return &this->window->bMinimumSize; }
	[[nodiscard]] QBindable<QSize> bindableMaximumSize() { return &this->window->bMaximumSize; }
	[[nodiscard]] QBindable<QString> bindableTitle() { return &this->window->bTitle; }
	[[nodiscard]] QBindable<QString> bindableAppId() { return &this->window->bAppId; }
	[[nodiscard]] QBindable<QString> bindableForeignParent() { return &this->window->bForeignParent; }

	[[nodiscard]] bool isMinimized() const;
	void setMinimized(bool minimized);
	[[nodiscard]] bool isMaximized() const;
	void setMaximized(bool maximized);
	[[nodiscard]] bool isFullscreen() const;
	void setFullscreen(bool fullscreen);

	/// Start a system move operation. Must be called during a pointer press/drag.
	Q_INVOKABLE [[nodiscard]] bool startSystemMove() const;
	/// Start a system resize operation. Must be called during a pointer press/drag.
	Q_INVOKABLE [[nodiscard]] bool startSystemResize(Qt::Edges edges) const;

	[[nodiscard]] QObject* parentWindow() const;
	void setParentWindow(QObject* window);

signals:
	void minimumSizeChanged();
	void maximumSizeChanged();
	void titleChanged();
	void minimizedChanged();
	void maximizedChanged();
	void fullscreenChanged();
	void parentWindowChanged();
	void appIdChanged();
	void foreignParentChanged();

private slots:
	void onWindowConnected();
	void onWindowStateChanged();

private:
	ProxyFloatingWindow* window;
	bool mMinimized = false;
	bool mMaximized = false;
	bool mFullscreen = false;
	bool mWasMinimized = false;
	bool mWasMaximized = false;
	bool mWasFullscreen = false;
};
