#pragma once

#include <BECore/UI/UIStrings.h>
#include <Mahjong/Game/Seats.h>

namespace BECore::Mahjong {

    /// One `{name}` placeholder of a text and the words that replace it.
    using WordArg = std::pair<StringView, StringView>;
    using WordArgs = std::initializer_list<WordArg>;

    /// `text` with every `{name}` replaced by the value of that name in `args`. A placeholder that is not in `args` and an unclosed `{` stay as written.
    [[nodiscard]] String Subst(StringView text, WordArgs args);
    /// The same for a list that is made at run time (the numbers of the rules pages): the values must outlive the call.
    [[nodiscard]] String Subst(StringView text, Span<const WordArg> args);

    /// The text of `key` in the current language of `strings`, else in its default language; the key itself when neither has it (never an empty string).
    /// Pure over the table: no scene and no services, so the code of `Table/` and its tests need none.
    [[nodiscard]] String Tr(const UIStrings& strings, StringView key);

    /// `Tr` with the placeholders of the text replaced: `Tr(strings, "seat.name", {{"n", "2"}, {"wind", "South"}})`. The order of the words in a sentence
    /// belongs to the translation, so a sentence is one key with placeholders and never pieces joined in code.
    [[nodiscard]] String Tr(const UIStrings& strings, StringView key, WordArgs args);
    [[nodiscard]] String Tr(const UIStrings& strings, StringView key, Span<const WordArg> args);

    /// The name of a wind in the current language ("East", "Восток"): the key `wind.<Wind>`; never the id `EnumUtils<Wind>::ToString` gives (that one is the control API's).
    [[nodiscard]] String WindName(const UIStrings& strings, Wind wind);

    /// The one letter of a wind in the current language ("E", "В"): the key `wind.letter.<Wind>`.
    [[nodiscard]] String WindLetter(const UIStrings& strings, Wind wind);

}  // namespace BECore::Mahjong
