#pragma once

#include <BECore/UI/UIButton.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UILabel.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UI/UIToggleComponent.h>
#include <optional>
#include <string>
#include <vector>

/**
 * A button of the shipped scene in either form: a composed element (`UIElement` with a `UITextComponent` and a `UIInputClicker`, what
 * `engine/tools/migrate` makes of a plain button since content version 2) or a `UIButton` (still the form of the options of a
 * `UISegmented` or a `UIDropdown`, and of an icon button). The tests ask a button its action, value, text and words through these, so
 * they do not depend on the form. Header only.
 */
namespace BECore::Mahjong::Tests {

    /// The action the button sends; empty when the element is no button.
    inline PoolString ButtonAction(const UIElement* element) {
        if (element == nullptr) {
            return {};
        }
        if (const auto* clicker = element->GetComponent<UIInputClicker>()) {
            return clicker->action;
        }
        const auto* button = element->Cast<UIButton>();
        return button != nullptr ? button->action : PoolString{};
    }

    /// The value sent with the action; empty when there is none.
    inline PoolString ButtonValue(const UIElement* element) {
        if (element == nullptr) {
            return {};
        }
        if (const auto* clicker = element->GetComponent<UIInputClicker>()) {
            return clicker->value;
        }
        const auto* button = element->Cast<UIButton>();
        return button != nullptr ? button->value : PoolString{};
    }

    /// Whether the element is a button of either form.
    inline bool IsButton(const UIElement* element) {
        return element != nullptr && (element->GetComponent<UIInputClicker>() != nullptr || element->Cast<UIButton>() != nullptr);
    }

    /// The text as written in the file (`{#key}` not expanded), of a label, a button or an element with a text component.
    inline eastl::string ElementText(const UIElement* element) {
        if (element == nullptr) {
            return {};
        }
        if (const auto* text = element->GetComponent<UITextComponent>()) {
            return text->text;
        }
        if (const auto* button = element->Cast<UIButton>()) {
            return button->text;
        }
        const auto* label = element->Cast<UILabel>();
        return label != nullptr ? label->text : eastl::string{};
    }

    /// Whether the element shows a text: a label, a button or an element with a text component.
    inline bool ShowsText(const UIElement& element) {
        return element.Cast<UILabel>() != nullptr || element.Cast<UIButton>() != nullptr || element.GetComponent<UITextComponent>() != nullptr;
    }

    /// Every label and button of the tree, shown or not, in tree order. A toggle row (`UIToggleComponent`) is one control: its caption
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
        if (element == nullptr) {
            return std::nullopt;
        }
        eastl::string_view shown;
        if (const auto* text = element->GetComponent<UITextComponent>()) {
            shown = text->GetDisplayText();
        } else if (const UILabel* label = element->Cast<UILabel>() != nullptr ? element->Cast<UILabel>() : element->Cast<UIButton>()) {
            shown = label->GetDisplayText();  // a button is a label by inheritance, but `Cast` is exact
        } else {
            return std::nullopt;
        }
        return std::string(shown.data(), shown.size());
    }

    /// The text as shown, or `fallback` when the element shows none.
    inline std::string DisplayText(const UIElement* element, const char* fallback = "<no label>") {
        return ShownText(element).value_or(fallback);
    }

}  // namespace BECore::Mahjong::Tests
