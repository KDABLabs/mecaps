#include "slint_wrapper_window.h"
#include "window_adapter.h"
#include <KDFoundation/event.h>
#include <KDGui/gui_events.h>
#include "kdgui_slint_keys.h"

namespace mecaps {
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
		SPDLOG_WARN("Unknown pointer button type.");
		return PointerEventButton::Other;
		break;
	}
}

SlintWrapperWindow::SlintWrapperWindow(KDWindowAdapter *adapter)
	: m_adapter(adapter)
{
	scaleFactor.valueChanged().connect([this](float newScale) {
		SPDLOG_DEBUG(
			"Slint window scale does not match OS window scale. Updating...");
		m_adapter->window().dispatch_scale_factor_change_event(newScale);
	});
	m_unicodeForKey.reserve(kdguiKeyMax);
	for (int i = 0; i < kdguiKeyMax; ++i) {
		m_unicodeForKey.emplace_back(std::nullopt);
	}
}

void SlintWrapperWindow::event(EventReceiver *target,
							   KDFoundation::Event *event)
{
	// process key/mouse events, timer events, and user events.
	KDGui::Window::event(target, event);

	if (event->isAccepted())
		return;

	switch (event->type()) {
	case KDFoundation::Event::Type::Update:
		if (target == this)
			m_adapter->render();
		break;
	case KDFoundation::Event::Type::TextInput:
		textInputEvent(static_cast<KDGui::TextInputEvent *>(event));
		break;
	default:
		break;
	}
}

void SlintWrapperWindow::textInputEvent(KDGui::TextInputEvent *event)
{
	// this happens with backspace, which is a special key which also sends a
	// textInputEvent
	if (!m_lastPressed) {
		SPDLOG_INFO("TextInputEvent recieved, but physical key is unknown or a "
					"special key.");
		return;
	}

	assert(!isSpecial(m_lastPressed.value()));

	if (unknownKey(m_lastPressed.value())) {
		SPDLOG_WARN("Unknown key");
		return;
	}

	auto &optional = m_unicodeForKey[m_lastPressed.value()];

	// optional will only be null once. every time a key is pressed it will be
	// written to.
	if (!optional)
		optional.emplace();

	auto &textBuffer = optional.value();
	// NOTE: this check relies on the event's text and the text buffer
	// containing the same size chars
	if (event->text().size() > textBuffer.size()) {
		SPDLOG_WARN(
			"Unicode key representation longer than expected, aborting.");
		return;
	}

	std::memcpy(textBuffer.data(), event->text().data(), event->text().size());

	m_adapter->window().dispatch_key_press_event(
		slint::SharedString(optional.value().data()));

	event->setAccepted(true);
}

void SlintWrapperWindow::keyReleaseEvent(KDGui::KeyReleaseEvent *event)
{
	auto key = event->key();

	if (unknownKey(key)) {
		SPDLOG_WARN("Unknown key");
		return;
	}

	if (isSpecial(key)) {
		m_adapter->window().dispatch_key_release_event(specialKeyText(key));
	} else {
		auto &optionalUnicode = m_unicodeForKey[event->key()];

		if (!optionalUnicode) {
			SPDLOG_WARN(
				"Key released, but we don't know its unicode representation.");
			return;
		}

		auto &unicode = optionalUnicode.value();

		m_adapter->window().dispatch_key_release_event(
			slint::SharedString(unicode.data()));
	}

	event->setAccepted(true);
}

void SlintWrapperWindow::keyPressEvent(KDGui::KeyPressEvent *event)
{
	auto key = event->key();
	if (unknownKey(key)) {
		SPDLOG_WARN("Unknown key");
		return;
	}

	if (isSpecial(key)) {
		m_adapter->window().dispatch_key_press_event(specialKeyText(key));
		// special keys may send text events. those will bail if no lastPressed
		m_lastPressed = std::nullopt;
	} else {
		// capture this key and actually send the press event when we get its
		// input text
		m_lastPressed = key;
	}

	event->setAccepted(true);
}

// an alternative would be to connect to width.valueChanged() and
// height.valueChanged()
void SlintWrapperWindow::resizeEvent(KDFoundation::ResizeEvent *event)
{
	KDGui::Window::resizeEvent(event);

	assert(m_adapter->initialized());

	const float scale = m_adapter->window().scale_factor();

	slint::LogicalSize windowSizeLogical({
		.width = static_cast<float>(event->width()) * scale,
		.height = static_cast<float>(event->height()) * scale,
	});

	m_adapter->window().dispatch_resize_event(windowSizeLogical);

	event->setAccepted(true);
}

void SlintWrapperWindow::mousePressEvent(KDGui::MousePressEvent *event)
{
	KDGui::Window::mousePressEvent(event);
	assert(m_adapter->initialized());

	float scale = m_adapter->window().scale_factor();

	m_adapter->window().dispatch_pointer_press_event(
		slint::LogicalPosition({
			.x = static_cast<float>(event->xPos()) * scale,
			.y = static_cast<float>(event->yPos()) * scale,
		}),
		kdGuiToPointerEventButton(event->button()));
	event->setAccepted(true);
}

void SlintWrapperWindow::mouseReleaseEvent(KDGui::MouseReleaseEvent *event)
{
	KDGui::Window::mouseReleaseEvent(event);
	assert(m_adapter->initialized());
	SPDLOG_DEBUG("Mouse release event received.");

	const float scale = m_adapter->window().scale_factor();

	m_adapter->window().dispatch_pointer_release_event(
		slint::LogicalPosition({
			.x = static_cast<float>(event->xPos()) * scale,
			.y = static_cast<float>(event->yPos()) * scale,
		}),
		kdGuiToPointerEventButton(event->button()));
	event->setAccepted(true);
}

void SlintWrapperWindow::mouseMoveEvent(KDGui::MouseMoveEvent *event)
{
	KDGui::Window::mouseMoveEvent(event);
	assert(m_adapter->initialized());

	slint_platform::update_timers_and_animations();

	const float scale = m_adapter->window().scale_factor();

	m_adapter->window().dispatch_pointer_move_event(slint::LogicalPosition({
		.x = static_cast<float>(event->xPos()) * scale,
		.y = static_cast<float>(event->yPos()) * scale,
	}));

	event->setAccepted(true);
}

void SlintWrapperWindow::mouseWheelEvent(KDGui::MouseWheelEvent *event)
{
	KDGui::Window::mouseWheelEvent(event);
	assert(m_adapter->initialized());
	SPDLOG_DEBUG("Mouse wheel event received.");

	const auto &pos = cursorPosition.get();
	const float scale = m_adapter->window().scale_factor();

	const auto logicalPos = slint::LogicalPosition({
		.x = static_cast<float>(pos.x) * scale,
		.y = static_cast<float>(pos.y) * scale,
	});

	m_adapter->window().dispatch_pointer_scroll_event(
		logicalPos, static_cast<float>(event->xDelta()) * scale,
		static_cast<float>(event->yDelta()) * scale);

	event->setAccepted(true);
}
} // namespace mecaps
