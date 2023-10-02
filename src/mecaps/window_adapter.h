#pragma once
#include <slint-platform.h>
#include <KDGui/window.h>

namespace slint_platform = slint::platform;

namespace mecaps {

class KDWindowAdapter : public slint_platform::WindowAdapter
{
  public:
	KDWindowAdapter() noexcept;
	inline KDGui::Window &kdGuiWindow() const { return *m_kdWindow; }
	slint_platform::AbstractRenderer &renderer() override;
	slint::PhysicalSize size() override;

	void set_visible(bool) override;
	void request_redraw() override;

	void render();

  private:
	std::optional<slint_platform::SkiaRenderer> m_renderer;
	std::unique_ptr<KDGui::Window> m_kdWindow;
#ifndef NDEBUG
  public:
	inline bool initialized() const { return m_initialized; }

  private:
	bool m_initialized = false;
#endif
};

} // namespace mecaps
