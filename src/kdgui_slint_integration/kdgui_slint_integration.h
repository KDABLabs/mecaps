#include <slint_size.h>
#include <KDGui/gui_application.h>
#include <KDGui/window.h>

namespace mecaps {

/// Register an create a window and window adapter and register them, so events
/// for the GuiApplication get propagated to slint.
KDGui::Window *createAndIntegrateWindow(slint::PhysicalSize windowSize);

} // namespace mecaps
