#pragma once

#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UI/UIToggleComponent.h>
#include <vector>

/**
 * A button of the shipped scene is one composed element: a `UIElement` with a `UIInputClicker` (the action and the value) and, when it
 * shows words, a `UITextComponent`; the options of a `UISegmented` or a `UIDropdown` are the same form. A text is a `UIElement` with a
 * `UITextComponent`. The tests ask a control its action, value, text and words through these helpers. Header only.
 */
namespace BECore::Mahjong::Tests {

    /// The action the button sends; empty when the element is no button.
    inline PoolString ButtonAction(const UIElement* element) {
        if (element == nullptr) {
            return {};
        }
        const auto* clicker = element->GetComponent<UIInputClicker>();
        return clicker != nullptr ? clicker->action : PoolString{};
    }

    /// The value sent with the action; empty when there is none.
    inline PoolString ButtonValue(const UIElement* element) {
        if (element == nullptr) {
            return {};
        }
        const auto* clicker = element->GetComponent<UIInputClicker>();
        return clicker != nullptr ? clicker->value : PoolString{};
    }

    /// Whether the element is a button (carries a `UIInputClicker`).
    inline bool IsButton(const UIElement* element) {
        return element != nullptr && element->GetComponent<UIInputClicker>() != nullptr;
    }

    /// The text component of the element; null when it shows no text.
    inline UITextComponent* TextOf(const UIElement* element) {
        return element != nullptr ? element->GetComponent<UITextComponent>() : nullptr;
    }

    /// The text as written in the file (`{#key}` not expanded); empty when the element shows none.
    inline eastl::string ElementText(const UIElement* element) {
        const auto* text = TextOf(element);
        return text != nullptr ? text->text : eastl::string{};
    }

    /// Whether the element shows a text (carries a `UITextComponent`). The header of a `UIDropdown` is not asked: it keeps the whole width
    /// less its arrow and has its own check in the tests that hold it.
    inline bool ShowsText(const UIElement& element) {
        return element.GetComponent<UITextComponent>() != nullptr && element.Cast<UIDropdown>() == nullptr;
    }

    /// Every element with a text of the tree, shown or not, in tree order. A toggle row (`UIToggleComponent`) is one control: its caption
    /// is not asked, since the switch beside it may cut it (the look the owner keeps for `Declare`; the class toggle flagged a caption
    /// only when it had no room at all).
    inline void CollectTextControls(const UIElement& element, std::vector<const UIElement*>& found) {
        if (ShowsText(element)) {
            found.push_back(&element);
        }
        if (element.GetComponent<UIToggleComponent>() != nullptr) {
            return;
        }
        for (const auto& child : element.children) {
            CollectTextControls(*child, found);
        }
    }

    /// The text as shown (`{#key}` and `{name}` expanded); nothing when the element shows none.
    inline std::optional<std::string> ShownText(const UIElement* element) {
        const auto* text = TextOf(element);
        if (text == nullptr) {
            return std::nullopt;
        }
        const eastl::string_view shown = text->GetDisplayText();
        return std::string(shown.data(), shown.size());
    }

    /// The text as shown, or `fallback` when the element shows none.
    inline std::string DisplayText(const UIElement* element, const char* fallback = "<no label>") {
        return ShownText(element).value_or(fallback);
    }

}  // namespace BECore::Mahjong::Tests
