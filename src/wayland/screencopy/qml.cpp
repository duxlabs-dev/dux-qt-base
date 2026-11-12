#include "qml.hpp"

#include "manager.hpp"

namespace qs::wayland::screencopy {

bool ScreencopyQml::canCaptureToplevels() { return ScreencopyManager::canCaptureToplevels(); }

bool ScreencopyQml::canCaptureExtToplevels() { return ScreencopyManager::canCaptureExtToplevels(); }

} // namespace qs::wayland::screencopy
