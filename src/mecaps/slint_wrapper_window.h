#pragma once
#include <KDGui/gui_events.h>
#include <KDGui/kdgui_keys.h>
#include <KDGui/window.h>
#include <KDFoundation/constexpr_sort.h>
#include <array>

namespace mecaps {

class KDWindowAdapter;

class SlintWrapperWindow : public KDGui::Window
{
  public:
	SlintWrapperWindow(KDWindowAdapter *adapter);

  protected:
	void event(EventReceiver *target, KDFoundation::Event *event) override;

  private:
	void resizeEvent(KDFoundation::ResizeEvent *) override final;
	void mousePressEvent(KDGui::MousePressEvent *) override final;
	void mouseReleaseEvent(KDGui::MouseReleaseEvent *) override final;
	void mouseMoveEvent(KDGui::MouseMoveEvent *) override final;
	void mouseWheelEvent(KDGui::MouseWheelEvent *) override final;
	void keyPressEvent(KDGui::KeyPressEvent *) override final;
	void keyReleaseEvent(KDGui::KeyReleaseEvent *) override final;
	void textInputEvent(KDGui::TextInputEvent *);

	inline bool unknownKey(KDGui::Key key) const
	{
		return key < 0 || key > m_unicodeForKey.size();
	}

	// NOTE: this stuff exists because KDGui only provides a TextInputEvent
	// (which gives us the unicode representation of a key) on key *press*.
	// Could be removed with the addition of some TextInputReleased event from
	// KDGui, or something like that.
	// FIXME: this is potentially platform specific code. on wayland, the max
	// bytes is 16. on X, KDGui internally uses a buffer of 16 bytes. For
	// windows, its dynamically sized.
	static constexpr size_t maxUnicodeCharacterForKey = 16;
	// FIXME: could change with kdgui
	static constexpr KDGui::Key kdguiKeyMax = KDGui::Key_Menu;
	std::vector<std::optional<std::array<char, maxUnicodeCharacterForKey>>>
		m_unicodeForKey;

	// used to connect keycodes to the unicode strings in the proceeding text
	// events.
	std::optional<KDGui::Key> m_lastPressed;

	KDWindowAdapter *m_adapter;
};

} // namespace mecaps
