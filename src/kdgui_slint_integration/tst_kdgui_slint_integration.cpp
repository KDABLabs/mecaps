#include "kdgui_slint_keys.h"
#include "slint_wrapper_window.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

namespace mecaps {

class SlintWrapperWindowUnitTestHarness
{
  public:
	static std::optional<slint::SharedString> handleKeyPressEvent(KDGui::Key key, uint8_t &lastNativeKeyCodePressed, std::optional<KDGui::Key> &lastKeyPressed) {
		return mecaps::SlintWrapperWindow::handleKeyPressEvent(0, key, lastNativeKeyCodePressed, lastKeyPressed);
	}
	static std::optional<slint::SharedString> handleKeyReleaseEvent(KDGui::Key key) {
		return mecaps::SlintWrapperWindow::handleKeyReleaseEvent(0, key);
	}
	static std::optional<slint::SharedString> handleTextInputEvent(std::string_view text, uint8_t &lastNativeKeyCodePressed, std::optional<KDGui::Key> &lastKeyPressed) {
		return mecaps::SlintWrapperWindow::handleTextInputEvent(text, lastNativeKeyCodePressed, lastKeyPressed);
	}
};

}

struct KDGuiKeyToSlintKeyPair { KDGui::Key key; std::optional<slint::SharedString> text; };
struct KDGuiKeyToStdStringPair { KDGui::Key key; std::string text; };

TEST_SUITE("KDGuiSlintIntegration")
{
	// Do all test assertions for a synthesized single key stroke
	// This makes the following assumptions about KDGui:
	// - single strokes of non-special keys results in keyPress -> inputText -> keyRelease event sequence
	// - KDGui::key of keyPress + keyRelease events matches unicode character of inputText event
	void assertSynthesizedKdGuiEventSequenceForSingleKeyStroke(KDGui::Key key, std::optional<std::string_view> text,
															   bool expectKeyPressEventToBeDispatchedOnKeyPressEvent,
															   bool expectKeyPressEventToBeDispatchedOnTextInputEvent)
	{
		uint8_t lastNativeKeyCodePressed;
		std::optional<KDGui::Key> lastKeyPressed;
		std::optional<slint::SharedString> result;

		bool keyPressEventWillBeDispatched;
		bool keyReleaseEventWillBeDispatched;

		result = mecaps::SlintWrapperWindowUnitTestHarness::handleKeyPressEvent(key, lastNativeKeyCodePressed, lastKeyPressed);
		keyPressEventWillBeDispatched = result.has_value();
		REQUIRE(keyPressEventWillBeDispatched == expectKeyPressEventToBeDispatchedOnKeyPressEvent);
		if (expectKeyPressEventToBeDispatchedOnKeyPressEvent)
			REQUIRE(result.value() == text);

		result = mecaps::SlintWrapperWindowUnitTestHarness::handleTextInputEvent(text.value(), lastNativeKeyCodePressed, lastKeyPressed);
		keyPressEventWillBeDispatched = result.has_value();
		REQUIRE(keyPressEventWillBeDispatched == expectKeyPressEventToBeDispatchedOnTextInputEvent);
		if (expectKeyPressEventToBeDispatchedOnTextInputEvent)
			REQUIRE(result.value() == text);

		result = mecaps::SlintWrapperWindowUnitTestHarness::handleKeyReleaseEvent(key);
		keyReleaseEventWillBeDispatched = result.has_value();
		REQUIRE(keyReleaseEventWillBeDispatched);
		REQUIRE(result.value() == text);
	}

	void assertNormalKeyStroke(KDGui::Key key, std::string_view text)
	{
		// key press event is dispatched delayed, i.e. on KDGui::TextInputEvent
		assertSynthesizedKdGuiEventSequenceForSingleKeyStroke(key, text, false, true);
	}

	void assertSpecialKeyStroke(KDGui::Key key, std::optional<std::string_view> text)
	{
		// key press event is dispatched immediatelly, i.e. on KDGui::KeyPressEvent
		assertSynthesizedKdGuiEventSequenceForSingleKeyStroke(key, text, true, false);
	}

	TEST_CASE("Test KDGui events [keyPress|inputText|keyRelease] trigger correct dispatch_key_[press|release]_event methods of slint")
	{
		SUBCASE("for printable ASCII keys")
		{
			// GIVEN
			std::vector<KDGuiKeyToStdStringPair> printableAsciiKeys;
			for (int i=0x20; i <= 0x7e; ++i) {
				printableAsciiKeys.emplace_back(KDGuiKeyToStdStringPair{ KDGui::Key(i), std::string{char(i)} });
			}

			// THEN
			for (const auto &testData : printableAsciiKeys) {
				assertNormalKeyStroke(testData.key, testData.text);
			}
		}

		SUBCASE("for function keys (1)")
		{
			// GIVEN
			std::vector<KDGuiKeyToSlintKeyPair> functionKeys;
			for (int i=0x100; i <= 0x130; ++i) {
				const auto key = KDGui::Key(i);
				functionKeys.emplace_back(KDGuiKeyToSlintKeyPair{ key, mecaps::specialKeyText(key) });
			}

			// THEN
			for (const auto &testData : functionKeys) {
				assertSpecialKeyStroke(testData.key, testData.text);
			}
		}

		SUBCASE("for numpad keys")
		{
			// GIVEN
			// clang-format off
			std::vector<KDGuiKeyToStdStringPair> numPadKeys = {
				{KDGui::Key_NumPad_0, "0"},
				{KDGui::Key_NumPad_1, "1"},
				{KDGui::Key_NumPad_2, "2"},
				{KDGui::Key_NumPad_3, "3"},
				{KDGui::Key_NumPad_4, "4"},
				{KDGui::Key_NumPad_5, "5"},
				{KDGui::Key_NumPad_6, "6"},
				{KDGui::Key_NumPad_7, "7"},
				{KDGui::Key_NumPad_8, "8"},
				{KDGui::Key_NumPad_9, "9"},
				{KDGui::Key_NumPad_Decimal, ","},
				{KDGui::Key_NumPad_Divide, "/"},
				{KDGui::Key_NumPad_Multiply, "*"},
				{KDGui::Key_NumPad_Subtract, "-"},
				{KDGui::Key_NumPad_Add, "+"},
				{KDGui::Key_NumPad_Enter, "\n"},
				{KDGui::Key_NumPad_Equal, "="}
			};
			// clang-format on

			// THEN
			for (const auto &testData : numPadKeys) {
				assertNormalKeyStroke(testData.key, testData.text);
			}
		}

		SUBCASE("for function keys (2)")
		{
			// GIVEN
			std::vector<KDGuiKeyToSlintKeyPair> functionKeys;
			for (int i=0x160; i <= 0x165; ++i) {
				const auto key = KDGui::Key(i);
				functionKeys.emplace_back(KDGuiKeyToSlintKeyPair{ key, mecaps::specialKeyText(key) });
			}

			// THEN
			for (const auto &testData : functionKeys) {
				assertSpecialKeyStroke(testData.key, testData.text);
			}
		}

		SUBCASE("for function keys (3)")
		{
			// GIVEN
			std::vector<KDGuiKeyToSlintKeyPair> functionKeys;
			for (int i=0x168; i <= 0x16a; ++i) {
				const auto key = KDGui::Key(i);
				functionKeys.emplace_back(KDGuiKeyToSlintKeyPair{ key, mecaps::specialKeyText(key) });
			}

			// THEN
			for (const auto &testData : functionKeys) {
				assertSpecialKeyStroke(testData.key, testData.text);
			}
		}
    }
}
