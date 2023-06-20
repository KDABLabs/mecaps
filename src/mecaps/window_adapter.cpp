#include "window_adapter.h"
#include "slint_wrapper_window.h"

#include <KDFoundation/config.h> // for KD_PLATFORM
#include <KDGui/gui_application.h>
#if defined(KD_PLATFORM_LINUX)
#include <KDGui/platform/linux/wayland/linux_wayland_platform_window.h>
#include <KDGui/platform/linux/wayland/linux_wayland_platform_integration.h>
#include <KDGui/platform/linux/xcb/linux_xcb_platform_integration.h>
#elif defined(KD_PLATFORM_MACOS)
#elif defined(KD_PLATFORM_WIN32)
#include <KDGui/platform/win32/win32_platform_window.h>
// TODO: wrap this is an ifdef, or remove it. Reliably finds the HISTANCE of
// this module regardless of whether it is loaded as a library or not. Requires
// MS linker (use HINST_THISCOMPONENT instead of GetModuleHandle(nullptr))
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)
#endif

namespace slint_platform = slint::experimental::platform;

namespace mecaps {

#ifdef KD_PLATFORM_LINUX
// private helpers (not class members to keep headers private)
static void
createXcbRenderer(std::optional<slint_platform::SkiaRenderer> &output,
				  const KDGui::LinuxXcbPlatformWindow &window,
				  KDGui::LinuxXcbPlatformIntegration &integration,
				  const slint::PhysicalSize &size);

static void
createWaylandRenderer(std::optional<slint_platform::SkiaRenderer> &output,
					  const KDGui::LinuxWaylandPlatformWindow &window,
					  const slint::PhysicalSize &size);
#endif

KDWindowAdapter::KDWindowAdapter() noexcept
{
	// KDGui::Window uses GuiApplication::instance and gets its platform
	// integration.
	m_kdWindow = std::make_unique<SlintWrapperWindow>(this);
	m_kdWindow->create();

	auto slintSize = physical_size();

	auto *app = KDGui::GuiApplication::instance();
	assert(app != nullptr);
	auto *platformIntegration = app->guiPlatformIntegration();

	// construct the renderer. platform-specific
#if defined(KD_PLATFORM_LINUX)

	// internally, KDGui runs this same check when deciding wayland or xcb
	if (KDGui::LinuxWaylandPlatformIntegration::checkAvailable()) {
		auto *waylandPlatformWindow =
			dynamic_cast<KDGui::LinuxWaylandPlatformWindow *>(
				m_kdWindow->platformWindow());

		if (waylandPlatformWindow == nullptr) {
			SPDLOG_INFO(
				"Unexpected windowing platform. Should have been wayland.");
		}

		createWaylandRenderer(m_renderer, *waylandPlatformWindow, slintSize);
	} else {
		auto *xcbLinuxPlatformIntegration =
			dynamic_cast<KDGui::LinuxXcbPlatformIntegration *>(
				platformIntegration);

		auto *xcbPlatformWindow = dynamic_cast<KDGui::LinuxXcbPlatformWindow *>(
			m_kdWindow->platformWindow());

		// wayland is not available, but the platform integration is not
		// xcb as expected
		if (xcbLinuxPlatformIntegration == nullptr ||
			xcbPlatformWindow == nullptr) {
			SPDLOG_ERROR(
				"Unknown linux platform in use (not xcb nor wayland).");
			std::abort();
		}

		createXcbRenderer(m_renderer, *xcbPlatformWindow,
						  *xcbLinuxPlatformIntegration, slintSize);
	}

#elif defined(KD_PLATFORM_MACOS)
	{
		// TODO: this (figure out how to get appkit handles from kdgui cocoa
		// window.
		// static NativeWindowHandle from_appkit(void *nsview, void
		// *nswindow)
	}
#elif defined(KD_PLATFORM_WIN32)
	{
		auto *win32PlatformWindow = dynamic_cast<KDGui::Win32PlatformWindow *>(
			m_kdWindow->platformWindow());

		m_renderer.emplace(
			slint_platform::NativeWindowHandle::from_win32(
				win32PlatformWindow->handle(), GetModuleHandle(nullptr)),
			slintSize);
	}
#endif
#ifndef NDEBUG
	m_initialized = true;
#endif
}

void KDWindowAdapter::show() const
{
	// visible must be called first to map the platform window
	m_kdWindow->visible = true;
	// now we can change the title
	m_kdWindow->title = KDGui::GuiApplication::instance()->applicationName();
	m_renderer->show();
}

void KDWindowAdapter::hide() const
{
	m_renderer->hide();
	m_kdWindow->destroy();
}

void KDWindowAdapter::request_redraw() const
{
	// TODO: I *think* that there is some benefit to going through the platform
	// event loop (the window manager should know that we are redrawing?). for
	// that, we would need a requestRedraw implemented in KDGui, which would
	// create a platform-specific event (like InvalidateRect seen here). we
	// would also need a KDGui PaintEvent which we could then handle by calling
	// render()
	//
	// check out LinuxXcbPlatformEventLoop::processXcbEvents
	// or the windowProc in win32_platform_window.cpp for where this code would
	// go.
#if defined(KD_PLATFORM_LINUX)
	// should use xcb_damage_add I think?
#elif defined(KD_PLATFORM_MACOS)
		// CFRunLoopWakeUp(CFRunLoopGetMain());
#elif defined(KD_PLATFORM_WIN32)
	// auto *win32PlatformWindow = dynamic_cast<KDGui::Win32PlatformWindow *>(
	// 	m_kdWindow->platformWindow());
	// InvalidateRect(win32PlatformWindow->handle(), nullptr, false);
#endif

	KDFoundation::CoreApplication::instance()->postEvent(
		m_kdWindow.get(), std::make_unique<KDFoundation::UpdateEvent>());
}

void KDWindowAdapter::render() const
{
	assert(window().size() == physical_size());
	slint_platform::update_timers_and_animations();
	m_renderer->render(window());
}

// called from SlintWrapperWindow after dispatching resize event to slint
void KDWindowAdapter::resize(slint::LogicalSize windowSize)
{
	slint::PhysicalSize size(slint::Size<uint32_t>{
		.width = (uint32_t)std::round(windowSize.width *
									  m_kdWindow->scaleFactor.get()),
		.height = (uint32_t)std::round(windowSize.height *
									   m_kdWindow->scaleFactor.get())});

	m_renderer->resize(size);
}

slint::PhysicalSize KDWindowAdapter::physical_size() const
{
	return slint::PhysicalSize(
		{m_kdWindow->width.get(), m_kdWindow->height.get()});
}

slint_platform::AbstractRenderer &KDWindowAdapter::renderer()
{
	return m_renderer.value();
}

#ifdef KD_PLATFORM_LINUX
// private helpers
static void
createXcbRenderer(std::optional<slint_platform::SkiaRenderer> &output,
				  const KDGui::LinuxXcbPlatformWindow &window,
				  KDGui::LinuxXcbPlatformIntegration &integration,
				  const slint::PhysicalSize &size)
{
	// getting the connection and the screen are non-const ops
	auto *xcbConnection = integration.connection();
	auto *xcbScreen = integration.screen();
	auto xcbWindowHandle = window.handle();

	// getting the visual_id of the window requires xcb interface
	xcb_visualid_t xcbVisual;
	{
		auto windowAttributesCookie =
			xcb_get_window_attributes(xcbConnection, xcbWindowHandle);
		xcb_generic_error_t err;
		auto *errHandle = &err;

		auto *windowAttributesReply = xcb_get_window_attributes_reply(
			xcbConnection, windowAttributesCookie, &errHandle);

		if (windowAttributesReply == nullptr) {
			integration.logger()->critical("X11 Error {}", err.error_code);
			std::abort();
		}
		xcbVisual = windowAttributesReply->visual;
	}

	output.emplace(slint_platform::NativeWindowHandle::from_x11(
					   xcbWindowHandle, xcbVisual, xcbConnection, 0),
				   size);
}

static void
createWaylandRenderer(std::optional<slint_platform::SkiaRenderer> &output,
					  const KDGui::LinuxWaylandPlatformWindow &window,
					  const slint::PhysicalSize &size)
{
	output.emplace(slint_platform::NativeWindowHandle::from_wayland(
					   window.surface(), window.display()),
				   size);
}
#endif
} // namespace mecaps
