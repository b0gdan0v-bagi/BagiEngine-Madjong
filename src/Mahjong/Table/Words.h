#pragma once

#include <BECore/UI/UIStrings.h>
#include <Mahjong/Game/Seats.h>

namespace BECore::Mahjong {

    /// One `{name}` placeholder of a text and the words that replace it.
    using WordArg = std::pair<eastl::string_view, eastl::string_view>;
    using WordArgs = std::initializer_list<WordArg>;

    /// `text` with every `{name}` replaced by the value of that name in `args`. A placeholder that is not in `args` and an unclosed `{` stay as written.
    [[nodiscard]] eastl::string Subst(eastl::string_view text, WordArgs args);

    /// The text of `key` in the current language of `strings`, else in its default language; the key itself when neither has it (never an empty string).
    /// Pure over the table: no scene and no services, so the code of `Table/` and its tests need none.
    [[nodiscard]] eastl::string Tr(const UIStrings& strings, eastl::string_view key);

    /// `Tr` with the placeholders of the text replaced: `Tr(strings, "seat.name", {{"n", "2"}, {"wind", "South"}})`. The order of the words in a sentence
    /// belongs to the translation, so a sentence is one key with placeholders and never pieces joined in code.
    [[nodiscard]] eastl::string Tr(const UIStrings& strings, eastl::string_view key, WordArgs args);

    /// The name of a wind in the current language ("East", "Восток"): the key `wind.<Wind>`; never the id `EnumUtils<Wind>::ToString` gives (that one is the control API's).
    [[nodiscard]] eastl::string WindName(const UIStrings& strings, Wind wind);

}  // namespace BECore::Mahjong
