#include "slint_wrapper_window.h"
#include "window_adapter.h"
#include <KDFoundation/event.h>
#include <KDGui/gui_events.h>

namespace mecaps {
using MouseEvent = slint::cbindgen_private::MouseEvent;
using Tag = slint::cbindgen_private::MouseEvent::Tag;
using PointerEventButton = slint::cbindgen_private::PointerEventButton;

static PointerEventButton kdGuiToPointerEventButton(KDGui::MouseButton button)
{
	switch (button) {
	case KDGui::MouseButton::NoButton:
		return PointerEventButton::Other;
		break;
	case KDGui::MouseButton::LeftButton:
		return PointerEventButton::Left;
		break;
	case KDGui::MouseButton::RightButton:
		return PointerEventButton::Right;
		break;
	case KDGui::MouseButton::MiddleButton:
		return PointerEventButton::Middle;
		break;
	default:
		SPDLOG_ERROR("Unknown pointer type. Defaulting to right click.");
		return PointerEventButton::Right;
		break;
	}
}

template <SlintWrapperWindow::KeyEventType EventType>
void SlintWrapperWindow::dispatchEventsForModifiers(
	KDGui::KeyboardModifiers mods)
{
	using Mod = KDGui::KeyboardModifier;

	struct KeyCode
	{
		const char16_t *str;
		Mod mod;
	};

	// from internal/common/key_codes.rs in slint
	static constexpr std::array codes{
		KeyCode{.str = u"0010", .mod = Mod::Mod_Shift},
		KeyCode{.str = u"0011", .mod = Mod::Mod_Control},
		KeyCode{.str = u"0012", .mod = Mod::Mod_Alt},
		KeyCode{.str = u"0017", .mod = Mod::Mod_Logo},
		KeyCode{.str = u"0014", .mod = Mod::Mod_CapsLock},
		// not used by slint?
		KeyCode{.str = nullptr, .mod = Mod::Mod_NumLock},
	};

	for (const auto &code : codes) {
		if (code.str == nullptr)
			continue;
		if ((mods & code.mod) != 0u) {
			if constexpr (EventType == Release) {
				m_adapter->window().dispatch_key_release_event(
					slint::SharedString((char *)code.str));
			} else {
				m_adapter->window().dispatch_key_press_event(
					slint::SharedString((char *)code.str));
			}
		}
	}
}

SlintWrapperWindow::SlintWrapperWindow(KDWindowAdapter *adapter)
	: m_adapter(adapter)
{
	scaleFactor.valueChanged().connect([this](float newScale) {
		SPDLOG_DEBUG(
			"Slint window scale does not match OS window scale. Updating...");
		m_adapter->dispatch_scale_factor_change_event(newScale);
	});
}

void SlintWrapperWindow::event(EventReceiver *target,
							   KDFoundation::Event *event)
{
	if (target == this && event->type() == KDFoundation::Event::Type::Update) {
		m_adapter->render();
	}

	KDGui::Window::event(target, event);
}

void SlintWrapperWindow::resizeEvent(KDFoundation::ResizeEvent *event)
{
	assert(m_adapter->initialized());
	SPDLOG_DEBUG("Resize even received");

	// TODO: check if kdfoundation resize event is actually logical size
	slint::LogicalSize windowSizeLogical(slint::Size<float>{
		.width = static_cast<float>(event->width()),
		.height = static_cast<float>(event->height()),
	});

	m_adapter->resize(windowSizeLogical);

	m_adapter->dispatch_resize_event(windowSizeLogical);

	SPDLOG_DEBUG("Resing to X: {}, Y: {}", event->width(), event->height());

	event->setAccepted(true);
}

void SlintWrapperWindow::mousePressEvent(KDGui::MousePressEvent *event)
{
	assert(m_adapter->initialized());
	SPDLOG_DEBUG("Mouse press event received");

	MouseEvent slintEvent;
	slintEvent.tag = Tag::Pressed;
	slintEvent.pressed.button = kdGuiToPointerEventButton(event->button());
	slintEvent.pressed.position.x = event->xPos();
	slintEvent.pressed.position.y = event->yPos();
	// NOTE: click count is not supported by kdutils. defaults to 1 click
	slintEvent.pressed.click_count = 1;

	m_adapter->dispatch_pointer_event(slintEvent);
	event->setAccepted(true);

	SPDLOG_DEBUG("At X: {}, Y: {}", event->xPos(), event->yPos());
}

void SlintWrapperWindow::mouseReleaseEvent(KDGui::MouseReleaseEvent *event)
{
	assert(m_adapter->initialized());
	SPDLOG_DEBUG("Mouse release event received.");

	MouseEvent slintEvent;
	slintEvent.tag = Tag::Released;
	// NOTE: click count is not supported by kdutils. defaults to 1 click
	slintEvent.released.click_count = 1;
	slintEvent.released.button = kdGuiToPointerEventButton(event->button());
	slintEvent.pressed.position.x = event->xPos();
	slintEvent.pressed.position.y = event->yPos();

	m_adapter->dispatch_pointer_event(slintEvent);
	event->setAccepted(true);
}

void SlintWrapperWindow::mouseMoveEvent(KDGui::MouseMoveEvent *event)
{
	assert(m_adapter->initialized());

	MouseEvent slintEvent;
	slintEvent.tag = Tag::Moved;
	// TODO: the slint event takes floats, not ints. might need to be converted
	// to logical size
	slintEvent.moved.position.x = (float)event->xPos();
	slintEvent.moved.position.y = (float)event->yPos();

	slint_platform::update_timers_and_animations();
	m_adapter->dispatch_pointer_event(slintEvent);
	event->setAccepted(true);
}

void SlintWrapperWindow::mouseWheelEvent(KDGui::MouseWheelEvent *event)
{
	assert(m_adapter->initialized());
	SPDLOG_DEBUG("Mouse wheel event received.");
	MouseEvent slintEvent;
	slintEvent.tag = Tag::Wheel;
	// TODO: the slint event takes floats, not ints. might need to be converted
	// to logical size
	slintEvent.wheel.delta_x = (float)event->xDelta();
	slintEvent.wheel.delta_y = (float)event->yDelta();
	KDGui::Position cPos = cursorPosition.get();
	slintEvent.wheel.position.x = (float)cPos.x;
	slintEvent.wheel.position.y = (float)cPos.y;

	m_adapter->dispatch_pointer_event(slintEvent);
	event->setAccepted(true);
}

void SlintWrapperWindow::keyPressEvent(KDGui::KeyPressEvent *event)
{
	{
		auto newModifiers = event->modifiers();
		auto changedMods = m_keyboardModifiers ^ newModifiers;

		if (changedMods != 0u) {
			SPDLOG_DEBUG("PRESSED, CHANGED BITS: {:#b}", changedMods);
			dispatchEventsForModifiers<Press>(changedMods);
			// assert that all the changed (pressed) keys are 1s in the new
			// state
			if ((newModifiers & changedMods) == changedMods) {
				// FIXME: shouldn't happen
				SPDLOG_WARN("Old keyboard state does not account for all the "
							"changed bits (pressed keys). New bits are as "
							"follows: {:#b}",
							newModifiers & changedMods);
			}
			// this will handle dispatching events on change
			m_keyboardModifiers = newModifiers;
		}
	}

	// create a null-terminated string out of the event's key
	// FIXME: figure out how the encoding actually works. currently this does
	// caps-only and special keys like backspace and arrow keys don't work
	KDGui::Key key = event->key();
	std::array<char, sizeof(KDGui::Key) + 1> string;
	string.fill(0);
	std::memcpy(string.data(), &key, sizeof(key));

	m_adapter->window().dispatch_key_press_event(
		slint::SharedString(string.data()));

	event->setAccepted(true);
}

void SlintWrapperWindow::keyReleaseEvent(KDGui::KeyReleaseEvent *event)
{
	{
		auto newModifiers = event->modifiers();
		auto changedMods = m_keyboardModifiers ^ newModifiers;

		if (changedMods != 0u) {
			SPDLOG_DEBUG("RELEASED, CHANGED BITS: {:#b}", changedMods);
			dispatchEventsForModifiers<Release>(changedMods);
			// assert that all the changed (released) keys were 1s in the old
			// state
			if ((m_keyboardModifiers & changedMods) == changedMods) {
				// FIXME: shouldn't happen
				SPDLOG_WARN("New keyboard state does not account for all the "
							"changed bits (released keys). Old bits are as "
							"follows: {:#b}",
							m_keyboardModifiers & changedMods);
			}
			// this will handle dispatching events on change
			m_keyboardModifiers = newModifiers;
		}
	}

	// create a null-terminated string out of the event's key
	// FIXME: figure out how the encoding actually works. currently this does
	// caps-only and special keys like backspace and arrow keys don't work
	KDGui::Key key = event->key();
	std::array<char, sizeof(KDGui::Key) + 1> string;
	string.fill(0);
	std::memcpy(string.data(), &key, sizeof(key));

	m_adapter->window().dispatch_key_release_event(
		slint::SharedString(string.data()));

	event->setAccepted(true);
}

} // namespace mecaps
