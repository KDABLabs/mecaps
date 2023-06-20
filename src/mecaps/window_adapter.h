#pragma once
#include "slint_platform.h"
#include <KDGui/window.h>

namespace slint_platform = slint::experimental::platform;

namespace mecaps {

class KDWindowAdapter : public slint_platform::WindowAdapter
{
  public:
	KDWindowAdapter() noexcept;
	inline KDGui::Window &kdGuiWindow() const { return *m_kdWindow; }
	slint_platform::AbstractRenderer &renderer() override;
	slint::PhysicalSize physical_size() const override;

	void show() const override;
	void hide() const override;
	void request_redraw() const override;

	void render() const;
	void resize(slint::LogicalSize windowSize);

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
