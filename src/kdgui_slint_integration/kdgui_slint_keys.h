#include <KDGui/kdgui_keys.h>
#include <spdlog/spdlog.h>
#include <slint.h>
#include <array>

using enum KDGui::Key;
namespace sk = slint::platform::key_codes;

namespace mecaps {

struct KDToSlintKeyAssociation
{
	KDGui::Key kdgui;
	std::optional<std::basic_string_view<char8_t>> slintKeyText;
};

static constexpr std::array expectedKeyOrder_1{
	KDToSlintKeyAssociation{Key_Escape, sk::Escape},
	KDToSlintKeyAssociation{Key_Enter, sk::Return},
	KDToSlintKeyAssociation{Key_Tab, sk::Tab},
	KDToSlintKeyAssociation{Key_Backspace, sk::Backspace},
	KDToSlintKeyAssociation{Key_Insert, sk::Insert},
	KDToSlintKeyAssociation{Key_Delete, sk::Delete},
	KDToSlintKeyAssociation{Key_Right, sk::RightArrow},
	KDToSlintKeyAssociation{Key_Left, sk::LeftArrow},
	KDToSlintKeyAssociation{Key_Down, sk::DownArrow},
	KDToSlintKeyAssociation{Key_Up, sk::UpArrow},
	KDToSlintKeyAssociation{Key_PageUp, sk::PageUp},
	KDToSlintKeyAssociation{Key_PageDown, sk::PageDown},
	KDToSlintKeyAssociation{Key_Home, sk::Home},
	KDToSlintKeyAssociation{Key_End, sk::End},
	KDToSlintKeyAssociation{Key_CapsLock, sk::CapsLock},
	KDToSlintKeyAssociation{Key_ScrollLock, sk::ScrollLock},
	KDToSlintKeyAssociation{Key_NumLock, {}},
	KDToSlintKeyAssociation{Key_PrintScreen, {}},
	KDToSlintKeyAssociation{Key_Pause, sk::Pause},
	KDToSlintKeyAssociation{Key_F1, sk::F1},
	KDToSlintKeyAssociation{Key_F2, sk::F2},
	KDToSlintKeyAssociation{Key_F3, sk::F3},
	KDToSlintKeyAssociation{Key_F4, sk::F4},
	KDToSlintKeyAssociation{Key_F5, sk::F5},
	KDToSlintKeyAssociation{Key_F6, sk::F6},
	KDToSlintKeyAssociation{Key_F7, sk::F7},
	KDToSlintKeyAssociation{Key_F8, sk::F8},
	KDToSlintKeyAssociation{Key_F9, sk::F9},
	KDToSlintKeyAssociation{Key_F10, sk::F10},
	KDToSlintKeyAssociation{Key_F11, sk::F11},
	KDToSlintKeyAssociation{Key_F12, sk::F12},
	KDToSlintKeyAssociation{Key_F13, sk::F13},
	KDToSlintKeyAssociation{Key_F14, sk::F14},
	KDToSlintKeyAssociation{Key_F15, sk::F15},
	KDToSlintKeyAssociation{Key_F16, sk::F16},
	KDToSlintKeyAssociation{Key_F17, sk::F17},
	KDToSlintKeyAssociation{Key_F18, sk::F18},
	KDToSlintKeyAssociation{Key_F19, sk::F19},
	KDToSlintKeyAssociation{Key_F20, sk::F20},
	KDToSlintKeyAssociation{Key_F21, sk::F21},
	KDToSlintKeyAssociation{Key_F22, sk::F22},
	KDToSlintKeyAssociation{Key_F23, sk::F23},
	KDToSlintKeyAssociation{Key_F24, sk::F24},
	KDToSlintKeyAssociation{Key_F25, {}},
	KDToSlintKeyAssociation{Key_F26, {}},
	KDToSlintKeyAssociation{Key_F27, {}},
	KDToSlintKeyAssociation{Key_F28, {}},
	KDToSlintKeyAssociation{Key_F29, {}},
	KDToSlintKeyAssociation{Key_F30, {}},
};

static constexpr std::array expectedKeyOrder_2{
	KDToSlintKeyAssociation{Key_LeftShift, sk::Shift},
	KDToSlintKeyAssociation{Key_LeftControl, sk::Control},
	KDToSlintKeyAssociation{Key_LeftAlt, sk::Alt},
	KDToSlintKeyAssociation{Key_LeftSuper, sk::Meta},
	KDToSlintKeyAssociation{Key_RightShift, sk::ShiftR},
	KDToSlintKeyAssociation{Key_RightControl, sk::ControlR},
};

static constexpr std::array expectedKeyOrder_3{
	KDToSlintKeyAssociation{Key_RightAlt, sk::AltGr},
	KDToSlintKeyAssociation{Key_RightSuper, sk::MetaR},
	KDToSlintKeyAssociation{Key_Menu, sk::Menu},
};

/// This function is an assertion that some of the internal details
/// of KDGui have not changed. Specifically the order of the Key enum, which
/// needs to have all of the special keys packed sequentially for isSpecial and
/// specialKeyText to work.
static constexpr bool kdguiKeysMatchExpected()
{
	// assert each item in the key order comes one after the other
	auto check = [](auto keyOrder) {
		if (keyOrder.size() == 1)
			return true;
		for (size_t i = 1; i < keyOrder.size(); ++i) {
			if (keyOrder[i].kdgui != keyOrder[i - 1].kdgui + 1)
				return false;
		}
		return true;
	};

	return check(expectedKeyOrder_1) && check(expectedKeyOrder_2) &&
		   check(expectedKeyOrder_3);
}

template <typename T> inline bool inRange(KDGui::Key key, T keyRange)
{
	return (key >= keyRange[0].kdgui &&
			key <= keyRange[keyRange.size() - 1].kdgui);
};

/// Returns true if the key is represented by one of slint's platform::key_codes
/// constants. This also usually means pressing it doesn't cause a
/// TextInputEvent from KDGui
inline bool isSpecial(KDGui::Key key)
{
	static_assert(kdguiKeysMatchExpected(),
				  "KDGui key enum values do not match expected. We do "
				  "numerical comparisons on these enum values, so they need to "
				  "be declared in the expected order");

	// same ranges as expectedKeyOrders
	return inRange(key, expectedKeyOrder_1) ||
		   inRange(key, expectedKeyOrder_2) || inRange(key, expectedKeyOrder_3);
}

inline slint::SharedString specialKeyText(KDGui::Key key)
{
	using enum KDGui::Key;
	assert(isSpecial(key));
	using KeyInt = std::underlying_type<KDGui::Key>::type;

	static constexpr std::basic_string_view<char8_t> unknownString{};

	auto stringIfInRange =
		[key](auto keyRange) -> std::optional<slint::SharedString> {
		if (inRange(key, keyRange)) {
			KeyInt keyNum = key;
			keyNum -= keyRange[0].kdgui;

			assert(keyNum >= 0 && keyNum < keyRange.size());

			return keyRange[keyNum].slintKeyText;
		}
		return {};
	};

	std::optional<slint::SharedString> result;
	if ((result = stringIfInRange(expectedKeyOrder_1)) ||
		(result = stringIfInRange(expectedKeyOrder_2)) ||
		(result = stringIfInRange(expectedKeyOrder_3)))
		return result.value();

	return unknownString;
}

} // namespace mecaps
