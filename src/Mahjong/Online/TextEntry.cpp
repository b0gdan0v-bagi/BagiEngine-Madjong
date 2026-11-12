#include "TextEntry.h"

#include <BECore/Text/Utf8.h>
#include <Mahjong/Table/Words.h>
#include <Rooms/RoomLimits.h>
#include <Rooms/RoomProtocol.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kPadLettersKey = "online.pad.letters";
        constexpr eastl::string_view kDefaultNameKey = "online.name.default";
        constexpr eastl::string_view kDigits = "0123456789";
        constexpr char kSpace = ' ';

        constexpr char32_t kCyrillicYoUpper = 0x0401;
        constexpr char32_t kCyrillicYoLower = 0x0451;
        constexpr char32_t kCyrillicFirst = 0x0410;
        constexpr char32_t kCyrillicLast = 0x044F;

        /// The only code point of @p character; nothing when it holds none, several or a malformed sequence.
        std::optional<char32_t> SingleCodePoint(eastl::string_view character) {
            if (character.empty()) {
                return std::nullopt;
            }
            size_t pos = 0;
            const char32_t codePoint = Utf8::DecodeNext(character, pos);
            if (pos != character.size() || codePoint == Utf8::kReplacement) {
                return std::nullopt;
            }
            return codePoint;
        }

        bool IsAsciiLetter(char32_t codePoint) {
            return (codePoint >= U'a' && codePoint <= U'z') || (codePoint >= U'A' && codePoint <= U'Z');
        }

        bool IsAsciiDigit(char32_t codePoint) {
            return codePoint >= U'0' && codePoint <= U'9';
        }

        bool IsCyrillicLetter(char32_t codePoint) {
            return (codePoint >= kCyrillicFirst && codePoint <= kCyrillicLast) || codePoint == kCyrillicYoUpper || codePoint == kCyrillicYoLower;
        }

        bool IsNameCharacter(char32_t codePoint) {
            return IsAsciiLetter(codePoint) || IsAsciiDigit(codePoint) || codePoint == static_cast<char32_t>(kSpace) || IsCyrillicLetter(codePoint);
        }

        /// The code alphabet is upper case ASCII: a typed lower-case letter is folded to it.
        char32_t FoldCodeCharacter(char32_t codePoint) {
            if (codePoint >= U'a' && codePoint <= U'z') {
                return codePoint - U'a' + U'A';
            }
            return codePoint;
        }

        bool IsCodeCharacter(char32_t codePoint) {
            return codePoint < 0x80 && RoomLimits::kRoomCodeAlphabet.find(static_cast<char>(codePoint)) != eastl::string_view::npos;
        }

        bool IsAsciiAlphanumeric(char c) {
            return IsAsciiLetter(static_cast<char32_t>(static_cast<unsigned char>(c))) || IsAsciiDigit(static_cast<char32_t>(static_cast<unsigned char>(c)));
        }

        bool IsPasteSeparator(char c) {
            return c == ' ' || c == '-' || (c >= '\t' && c <= '\r');
        }

        /// @p c folded to upper case when it belongs to the room code alphabet; nothing otherwise.
        std::optional<char> CodeLetter(char c) {
            const char folded = static_cast<char>(FoldCodeCharacter(static_cast<char32_t>(static_cast<unsigned char>(c))));
            if (!IsCodeCharacter(static_cast<char32_t>(static_cast<unsigned char>(folded)))) {
                return std::nullopt;
            }
            return folded;
        }

    }  // namespace

    TextEntry::TextEntry(Kind kind) : _kind(kind) {}

    eastl::vector<eastl::string> TextEntry::Pad(const UIStrings& strings) const {
        eastl::vector<eastl::string> keys;
        keys.reserve(kPadMaxKeys);
        if (_kind == Kind::Code) {
            for (const char c : RoomLimits::kRoomCodeAlphabet) {
                keys.emplace_back(1, c);
            }
            return keys;
        }
        const eastl::string letters = Tr(strings, kPadLettersKey);
        size_t pos = 0;
        while (pos < letters.size() && keys.size() < kPadMaxKeys) {
            const size_t start = pos;
            const char32_t codePoint = Utf8::DecodeNext(letters, pos);
            if (codePoint == Utf8::kReplacement || !IsNameCharacter(codePoint) || codePoint == static_cast<char32_t>(kSpace) || IsAsciiDigit(codePoint)) {
                continue;
            }
            keys.emplace_back(letters.data() + start, pos - start);
        }
        for (const char digit : kDigits) {
            if (keys.size() < kPadMaxKeys) {
                keys.emplace_back(1, digit);
            }
        }
        if (keys.size() < kPadMaxKeys) {
            keys.emplace_back(1, kSpace);
        }
        return keys;
    }

    bool TextEntry::Append(eastl::string_view character) {
        const std::optional<char32_t> codePoint = SingleCodePoint(character);
        if (!codePoint) {
            return false;
        }
        if (_kind == Kind::Code) {
            const char32_t folded = FoldCodeCharacter(*codePoint);
            if (!IsCodeCharacter(folded) || _text.size() >= RoomLimits::kRoomCodeLength) {
                return false;
            }
            _text.push_back(static_cast<char>(folded));
            return true;
        }
        if (!IsNameCharacter(*codePoint) || _text.size() + character.size() > RoomLimits::kMaxNameBytes) {
            return false;
        }
        _text.append(character.data(), character.size());
        return true;
    }

    bool TextEntry::Backspace() {
        if (_text.empty()) {
            return false;
        }
        constexpr unsigned char kContinuationMask = 0xC0;
        constexpr unsigned char kContinuation = 0x80;
        size_t last = _text.size() - 1;
        while (last > 0 && (static_cast<unsigned char>(_text[last]) & kContinuationMask) == kContinuation) {
            --last;
        }
        _text.resize(last);
        return true;
    }

    void TextEntry::Clear() {
        _text.clear();
    }

    bool TextEntry::Set(eastl::string_view text) {
        TextEntry candidate(_kind);
        size_t pos = 0;
        while (pos < text.size()) {
            const size_t start = pos;
            static_cast<void>(Utf8::DecodeNext(text, pos));  // only how far the character reaches
            if (!candidate.Append(text.substr(start, pos - start))) {
                return false;
            }
        }
        _text = eastl::move(candidate._text);
        return true;
    }

    bool TextEntry::IsComplete() const {
        if (_kind == Kind::Code) {
            return _text.size() == RoomLimits::kRoomCodeLength;
        }
        return SanitizePlayerName(_text).has_value();
    }

    std::optional<char> TextEntry::CharFromKeycode(uint32_t keycode, bool shift) {
        if (keycode >= 'a' && keycode <= 'z') {
            return static_cast<char>(shift ? keycode - 'a' + 'A' : keycode);
        }
        if (keycode >= '0' && keycode <= '9') {
            return static_cast<char>(keycode);
        }
        if (keycode == static_cast<uint32_t>(kSpace)) {
            return kSpace;
        }
        return std::nullopt;
    }

    std::optional<eastl::string> TextEntry::CleanRoomCode(eastl::string_view text) {
        if (text.empty() || text.size() > kMaxPasteBytes) {
            return std::nullopt;
        }

        // The whole text is one code, written with spaces or dashes, maybe longer than a code
        eastl::string squashed;
        bool onlyCode = true;
        for (const char c : text) {
            if (IsPasteSeparator(c)) {
                continue;
            }
            const std::optional<char> letter = IsAsciiAlphanumeric(c) ? CodeLetter(c) : std::nullopt;
            if (!letter) {
                onlyCode = false;
                break;
            }
            squashed.push_back(*letter);
        }
        if (onlyCode && !squashed.empty()) {
            if (squashed.size() < RoomLimits::kRoomCodeLength) {
                return std::nullopt;
            }
            return eastl::string(squashed.data(), RoomLimits::kRoomCodeLength);
        }

        // A sentence: the one word of the code's length that is made of the code's characters
        std::optional<eastl::string> found;
        size_t pos = 0;
        while (pos < text.size()) {
            if (!IsAsciiAlphanumeric(text[pos])) {
                ++pos;
                continue;
            }
            const size_t start = pos;
            while (pos < text.size() && IsAsciiAlphanumeric(text[pos])) {
                ++pos;
            }
            if (pos - start != RoomLimits::kRoomCodeLength) {
                continue;
            }
            eastl::string word;
            for (size_t i = start; i < pos; ++i) {
                const std::optional<char> letter = CodeLetter(text[i]);
                if (!letter) {
                    break;
                }
                word.push_back(*letter);
            }
            if (word.size() != RoomLimits::kRoomCodeLength) {
                continue;
            }
            if (found) {
                return std::nullopt;
            }
            found = eastl::move(word);
        }
        return found;
    }

    eastl::string TextEntry::DefaultName(const UIStrings& strings) {
        return Tr(strings, kDefaultNameKey);
    }

}  // namespace BECore::Mahjong
