#include "slint_wrapper_window.h"
#include "window_adapter.h"
#include <KDFoundation/event.h>
#include <KDGui/gui_events.h>
#include "kdgui_slint_keys.h"

namespace mecaps {
using PointerEventButton = slint::cbindgen_private::PointerEventButton;

static PointerEventButton kdGuiToPointerEventButton(KDGui::MouseButton button)
{
	if (button == KDGui::MouseButton::NoButton) {
		return PointerEventButton::Other;
	}
	else if (button == KDGui::MouseButton::LeftButton) {
		return PointerEventButton::Left;
	}
	else if (button == KDGui::MouseButton::RightButton) {
		return PointerEventButton::Right;
	}
	else if (button == KDGui::MouseButton::MiddleButton) {
		return PointerEventButton::Middle;
	}
	else {
		SPDLOG_WARN("Unknown pointer button type.");
		return PointerEventButton::Other;
	}
}

std::vector<std::optional<std::array<char, SlintWrapperWindow::s_maxUnicodeCharacterForKey>>> SlintWrapperWindow::s_unicodeForNativeKeyCode(256, std::nullopt);

SlintWrapperWindow::SlintWrapperWindow(KDWindowAdapter *adapter)
	: m_adapter(adapter)
{
	scaleFactor.valueChanged().connect([this](float newScale) {
		SPDLOG_DEBUG(
			"Slint window scale does not match OS window scale. Updating...");
		m_adapter->window().dispatch_scale_factor_change_event(newScale);
	});
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
	const auto text = event->text();
	auto result = handleTextInputEvent(text, m_lastNativeKeyCodePressed, m_lastKdGuiKeyPressed);

	if (result)
		m_adapter->window().dispatch_key_press_event(result.value());

	event->setAccepted(true);
}

void SlintWrapperWindow::keyReleaseEvent(KDGui::KeyReleaseEvent *event)
{
	const auto nativeKeyCode = event->nativeKeycode();
	const auto key = event->key();
	auto result = handleKeyReleaseEvent(nativeKeyCode, key);

	if (result)
		m_adapter->window().dispatch_key_release_event(result.value());

	event->setAccepted(true);
}

void SlintWrapperWindow::keyPressEvent(KDGui::KeyPressEvent *event)
{
	const auto nativeKeyCode = event->nativeKeycode();
	const auto key = event->key();
	auto result = handleKeyPressEvent(nativeKeyCode, key, m_lastNativeKeyCodePressed, m_lastKdGuiKeyPressed);

	if (result)
		m_adapter->window().dispatch_key_press_event(result.value());

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

std::optional<slint::SharedString> SlintWrapperWindow::handleKeyPressEvent(uint8_t nativeKeyCode, KDGui::Key key, uint8_t &lastNativeKeyCodePressed, std::optional<KDGui::Key> &lastKdGuiKeyPressed)
{
	std::optional<slint::SharedString> result = {};

	lastNativeKeyCodePressed = nativeKeyCode;
	lastKdGuiKeyPressed = key;

	if (isSpecial(key)) {
		result = specialKeyText(key);
	}

	return result;
}

std::optional<slint::SharedString> SlintWrapperWindow::handleKeyReleaseEvent(uint8_t nativeKeyCode, KDGui::Key key)
{
	std::optional<slint::SharedString> result = {};

	if (isSpecial(key)) {
		result = specialKeyText(key);
	}
	else {
		const auto &optionalUnicode = s_unicodeForNativeKeyCode[nativeKeyCode];

		if (!optionalUnicode) {
			SPDLOG_WARN("Key released, but we don't know its unicode representation.");
		}
		else {
			const auto &unicode = optionalUnicode.value();
			result = slint::SharedString(unicode.data());
		}
	}

	return result;
}

std::optional<slint::SharedString> SlintWrapperWindow::handleTextInputEvent(std::string_view text, uint8_t &lastNativeKeyCodePressed, std::optional<KDGui::Key> &lastKdGuiKeyPressed)
{
	std::optional<slint::SharedString> result = {};

	if (isSpecial(lastKdGuiKeyPressed.value())) {
		SPDLOG_WARN("TextInputEvent recieved, but last key is special key.");
	}
	else {
		auto &optionalUnicode = s_unicodeForNativeKeyCode[lastNativeKeyCodePressed];

		// Initialize each time as there might be remnants of different text for this keycode if it was
		// invoked with different modifiers
		optionalUnicode.emplace();

		// NOTE: this check relies on the event's text and the text buffer
		// containing the same size chars
		if (text.size() > s_maxUnicodeCharacterForKey) {
			SPDLOG_WARN("Unicode key representation longer than expected, aborting.");
		}
		else {
			std::memcpy(optionalUnicode.value().data(), text.data(), text.size());
			result = slint::SharedString(optionalUnicode.value().data());
		}
	}

	return result;
}

} // namespace mecaps
