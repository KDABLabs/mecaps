#pragma once
#include <KDGui/kdgui_keys.h>
#include <KDGui/window.h>

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

	enum KeyEventType
	{
		Press,
		Release
	};

	template <KeyEventType EventType>
	void dispatchEventsForModifiers(KDGui::KeyboardModifiers mods);

	KDWindowAdapter *m_adapter;
	KDGui::KeyboardModifiers m_keyboardModifiers = 0;
};

} // namespace mecaps
