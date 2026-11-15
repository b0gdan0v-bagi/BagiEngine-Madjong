#pragma once

#include <BECore/UI/UIStrings.h>

namespace BECore::Mahjong {

    /**
     * @brief The text of one form field of the online screens: a room code or a player's name. Plain C++, no SDL, ImGui or scene.
     *
     * Typed from a pad of keys (`Pad`) and, on a desktop, from the keyboard (`CharFromKeycode`); both end in `Append`, which refuses
     * anything the field cannot hold. A code is upper case of the room alphabet (`RoomLimits::kRoomCodeAlphabet`); a name is bounded by the server's limit
     * (`RoomLimits::kMaxNameBytes`, `SanitizePlayerName`) and kept as typed. The pad of a name lists the letters of the current language
     * but the field takes the letters of both alphabets, so a name typed in one language survives switching to the other.
     *
     * The text keeps its capacity, so typing allocates nothing once it has grown.
     */
    class TextEntry {
    public:
        enum class Kind : uint8_t { Code, Name };

        /// The pad's node file has this many key elements, in rows of `kPadKeysPerRow`.
        static constexpr size_t kPadMaxKeys = 48;
        static constexpr size_t kPadKeysPerRow = 8;
        static constexpr size_t kPadRowCount = kPadMaxKeys / kPadKeysPerRow;

        explicit TextEntry(Kind kind);

        [[nodiscard]] bool operator==(const TextEntry& other) const = default;

        [[nodiscard]] Kind GetKind() const {
            return _kind;
        }

        /// Each key is one UTF-8 character: the room code alphabet; or the letters of `online.pad.letters`, the digits and a space. At most
        /// `kPadMaxKeys`, none twice.
        [[nodiscard]] Vector<String> Pad(const UIStrings& strings) const;

        /// @p character is exactly one UTF-8 character, folded to upper case for a code. False, nothing changed, for a character outside the
        /// kind's set or past the limit.
        bool Append(StringView character);

        /// Removes the last whole UTF-8 character.
        bool Backspace();

        void Clear();

        /// All or nothing: false and no change unless every character is accepted and it fits.
        bool Replace(StringView text);

        [[nodiscard]] const String& Text() const {
            return _text;
        }

        /// A code of `RoomLimits::kRoomCodeLength`; a name that `SanitizePlayerName` accepts.
        [[nodiscard]] bool IsComplete() const;

        /// By SDL keycode: letters (lower case, `shift` makes it upper), digits and the space are their ASCII values; nothing for any other key.
        [[nodiscard]] static Optional<char> CharFromKeycode(uint32_t keycode, bool shift);

        /// A hostile clipboard is not scanned beyond this.
        static constexpr size_t kMaxPasteBytes = 1024;

        /// The room code in a pasted @p text, or nothing. A code with spaces or dashes, or longer than needed (cut), is the code itself; a
        /// sentence is searched for the one word that can be a code. **None, or two, is refused, never guessed**: an English word like
        /// "hands" can be one, and a wrong guess joins a stranger's room. Text over `kMaxPasteBytes` and a non-ASCII letter are refused.
        [[nodiscard]] static Optional<String> CleanRoomCode(StringView text);

        [[nodiscard]] static String DefaultName(const UIStrings& strings);

    private:
        Kind _kind;
        String _text;
    };

}  // namespace BECore::Mahjong
