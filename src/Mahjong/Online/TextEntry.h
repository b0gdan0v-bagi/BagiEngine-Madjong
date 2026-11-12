#pragma once

#include <BECore/UI/UIStrings.h>

namespace BECore::Mahjong {

    /**
     * @brief The text of one form field of the online screens: a room code or a player's name. Plain C++, no SDL, ImGui or scene.
     *
     * The engine has no text field, so a field is typed from a pad of keys (`Pad`) and, on a desktop, from the keyboard (`CharFromKeycode`). Both end in `Append(character)`,
     * which takes one UTF-8 character and refuses anything the field cannot hold. A code is upper case of the room alphabet, 5 long; a name is at most 24 bytes (the server's
     * limit, `SanitizePlayerName`) of Latin or Cyrillic letters, digits and spaces, kept as typed. The pad of a name lists the letters of the current language
     * (`online.pad.letters`), but the field takes the letters of both alphabets, so a name typed in one language survives switching to the other.
     *
     * The text is a member that keeps its capacity, so typing allocates nothing once it has grown.
     */
    class TextEntry {
    public:
        enum class Kind : uint8_t { Code, Name };

        /// The most keys a pad has (the node file has this many elements).
        static constexpr size_t kPadMaxKeys = 48;
        /// Keys in one row of the pad's node file, and the rows that make `kPadMaxKeys`.
        static constexpr size_t kPadKeysPerRow = 8;
        static constexpr size_t kPadRowCount = kPadMaxKeys / kPadKeysPerRow;

        explicit TextEntry(Kind kind);

        [[nodiscard]] bool operator==(const TextEntry& other) const = default;

        [[nodiscard]] Kind GetKind() const {
            return _kind;
        }

        /// The keys of this kind's pad in the current language, each one UTF-8 character: the room code alphabet; or the letters of `online.pad.letters`, the ten digits and a space.
        /// At most `kPadMaxKeys`, none twice.
        [[nodiscard]] eastl::vector<eastl::string> Pad(const UIStrings& strings) const;

        /// Appends @p character (exactly one UTF-8 character, a code folded to upper case). False and nothing changed when the field cannot hold it: a character outside
        /// the kind's set, or the text would pass the limit (5 characters, 24 bytes).
        bool Append(eastl::string_view character);

        /// Removes the last whole UTF-8 character. False when the text is empty.
        bool Backspace();

        void Clear();

        /// Replaces the text by @p text when every character of it is accepted and it fits; otherwise nothing changes and the result is false.
        bool Set(eastl::string_view text);

        [[nodiscard]] const eastl::string& Text() const {
            return _text;
        }

        /// A code of 5 characters; a name that `SanitizePlayerName` accepts.
        [[nodiscard]] bool IsComplete() const;

        /// The character a key of the keyboard types, by its SDL keycode (the keycodes of letters, digits and the space are their ASCII values, lower case for a letter, so
        /// `shift` makes it upper case); nothing for any other key.
        [[nodiscard]] static std::optional<char> CharFromKeycode(uint32_t keycode, bool shift);

        /// The longest text `CleanRoomCode` looks at: a hostile clipboard is not scanned.
        static constexpr size_t kMaxPasteBytes = 1024;

        /// The room code in a pasted @p text, or nothing. A code with spaces or dashes ("  ab3k9 ", "AB3-K9") or longer than needed (cut to its length) is the code itself; an
        /// invite line is searched for the one word that can be a code (5 letters or digits of the code alphabet): none, or two (an English word like "hands" can be one too), is
        /// refused and never guessed. Empty text, text over `kMaxPasteBytes`, a letter outside ASCII is refused.
        [[nodiscard]] static std::optional<eastl::string> CleanRoomCode(eastl::string_view text);

        /// The name a player starts with (`online.name.default`).
        [[nodiscard]] static eastl::string DefaultName(const UIStrings& strings);

    private:
        Kind _kind;
        eastl::string _text;
    };

}  // namespace BECore::Mahjong
