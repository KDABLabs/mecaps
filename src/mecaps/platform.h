#include "slint_platform.h"
#include "window_adapter.h"

namespace mecaps {
namespace slint_platform = slint::experimental::platform;
class KDSlintPlatform : public slint_platform::Platform
{
  public:
	inline void
	aquireWindowAdapter(std::unique_ptr<KDWindowAdapter> &&window) const
	{
		m_windowAdapter.swap(window);
	}

  protected:
	inline std::unique_ptr<slint_platform::WindowAdapter>
	create_window_adapter() const override
	{
		return std::move(m_windowAdapter);
	}

  private:
	mutable std::unique_ptr<KDWindowAdapter> m_windowAdapter;
};
} // namespace mecaps
