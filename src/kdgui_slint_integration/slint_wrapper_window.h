#pragma once
#include <slint_string.h>
#include <KDGui/gui_events.h>
#include <KDGui/kdgui_keys.h>
#include <KDGui/window.h>
#include <KDFoundation/constexpr_sort.h>
#include <array>

namespace mecaps {

class KDWindowAdapter;

class SlintWrapperWindow : public KDGui::Window
{
  friend class SlintWrapperWindowUnitTestHarness;

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

	static std::optional<slint::SharedString> handleKeyPressEvent(uint8_t nativeKeyCode, KDGui::Key key, uint8_t &lastNativeKeyCodePressed, std::optional<KDGui::Key> &lastKdGuiKeyPressed);
	static std::optional<slint::SharedString> handleKeyReleaseEvent(uint8_t nativeKeyCode, KDGui::Key key);
	static std::optional<slint::SharedString> handleTextInputEvent(std::string_view text, uint8_t &lastNativeKeyCodePressed, std::optional<KDGui::Key> &lastKdGuiKeyPressed);

	static bool isUnknownKey(KDGui::Key key)
	{
		return (key == KDGui::Key_Unknown);
	}

	// NOTE: this stuff exists because KDGui only provides a TextInputEvent
	// (which gives us the unicode representation of a key) on key *press*.
	// Could be removed with the addition of some TextInputReleased event from
	// KDGui, or something like that.
	// FIXME: this is potentially platform specific code. on wayland, the max
	// bytes is 16. on X, KDGui internally uses a buffer of 16 bytes. For
	// windows, its dynamically sized.
	static constexpr size_t s_maxUnicodeCharacterForKey = 16;
	static std::vector<std::optional<std::array<char, s_maxUnicodeCharacterForKey>>> s_unicodeForNativeKeyCode;

	// used to connect keycodes to the unicode strings in the proceeding text
	// events.
	uint8_t m_lastNativeKeyCodePressed;
	std::optional<KDGui::Key> m_lastKdGuiKeyPressed;

	KDWindowAdapter *m_adapter;
};

} // namespace mecaps
