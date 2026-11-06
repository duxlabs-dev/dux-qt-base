# rpkg macros, built from the git checkout

%global version {{{
set -e
if [ "$(git rev-parse --is-shallow-repository)" = "true" ]; then
    git fetch --unshallow --quiet
fi
if [ "$(git rev-parse --is-shallow-repository)" = "true" ]; then
    echo "clone is still shallow; refusing a truncated commit count" >&2
    exit 1
fi
printf '%s^%s.git%s\n' \
    "$(sed -n 's/^project(.*VERSION "\([^"]*\)".*/\1/p' CMakeLists.txt)" \
    "$(git rev-list --count HEAD)" \
    "$(git rev-parse --short=8 HEAD)"
}}}

Name:           dux-qt-base-git
Version:        %{version}
Release:        1%{?dist}
Summary:        Quickshell QML runtime as a shared library with a C entry point (git)

License:        LGPL-3.0-only
URL:            https://github.com/duxlabs-dev/dux-qt-base
VCS:            {{{ git_repo_vcs }}}
Source0:        {{{ git_repo_pack }}}

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  git-core
BuildRequires:  ninja-build
BuildRequires:  pkgconfig
BuildRequires:  cmake(Qt6Core) >= 6.8.0
BuildRequires:  cmake(Qt6DBus)
BuildRequires:  cmake(Qt6Network)
BuildRequires:  cmake(Qt6Qml)
BuildRequires:  cmake(Qt6Quick)
BuildRequires:  cmake(Qt6QuickControls2)
BuildRequires:  cmake(Qt6ShaderTools)
BuildRequires:  cmake(Qt6WaylandClient)
BuildRequires:  cmake(Qt6Widgets)
BuildRequires:  qt6-qtbase-private-devel
BuildRequires:  pkgconfig(CLI11)
BuildRequires:  pkgconfig(egl)
BuildRequires:  pkgconfig(gbm)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(gobject-2.0)
BuildRequires:  pkgconfig(libdrm)
BuildRequires:  pkgconfig(libpipewire-0.3)
BuildRequires:  pkgconfig(pam)
BuildRequires:  pkgconfig(polkit-agent-1)
BuildRequires:  pkgconfig(polkit-gobject-1)
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-protocols) >= 1.41
BuildRequires:  pkgconfig(xcb)
BuildRequires:  pkgconfig(xkbcommon)
BuildRequires:  spirv-tools
BuildRequires:  vulkan-headers
BuildRequires:  wayland-devel

Requires:       qt6-qtwayland%{?_isa}
Recommends:     qt6-qtsvg%{?_isa}

Provides:       dux-qt-base = %{version}-%{release}
Conflicts:      dux-qt-base

%description
Quickshell QML runtime, built as libdux-qt-base.so from the master branch.
Exposes the quickshell command line as dqb_main() so Go and C programs can
embed it. QML modules are compiled into the library.

%package        devel
Summary:        Development files for %{name}
Requires:       %{name}%{?_isa} = %{version}-%{release}
Requires:       pkgconfig
Provides:       dux-qt-base-devel = %{version}-%{release}
Conflicts:      dux-qt-base-devel

%description    devel
Header and pkg-config file for linking against libdux-qt-base. Go consumers
use `#cgo pkg-config: dux-qt-base`.

%prep
{{{ git_repo_setup_macro }}}

%build
COMMIT=$(echo "%{version}" | sed -n 's/.*git\([0-9a-f]*\)$/\1/p')
%cmake -GNinja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_INSTALL_LIBDIR=%{_lib} \
        -DDISTRIBUTOR="Fedora COPR (%{name})" \
        -DGIT_REVISION="${COMMIT:-unknown}"
%cmake_build

%install
%cmake_install

%files
%license LICENSE LICENSE-GPL
%doc README.md UPSTREAM.md
%{_libdir}/libdux-qt-base.so.0
%{_libdir}/libdux-qt-base.so.0.*

%files devel
%doc BUILD.md
%{_includedir}/dux-qt-base/
%{_libdir}/libdux-qt-base.so
%{_libdir}/pkgconfig/dux-qt-base.pc

%changelog
{{{ git_repo_changelog }}}
