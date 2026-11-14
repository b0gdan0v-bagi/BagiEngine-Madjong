#include "UploadPrefs.h"

#include <BECore/Save/SaveStorage.h>
#include <Mahjong/Online/TextEntry.h>

namespace BECore::Mahjong {

    namespace {

        constexpr StringView kConsentKey = "consent";
        constexpr StringView kNicknameKey = "nickname";
        constexpr StringView kUnasked = "unasked";
        constexpr StringView kGranted = "granted";
        constexpr StringView kDeclined = "declined";
        constexpr StringView kTempSuffix = ".tmp";
        constexpr char kLineEnd = '\n';
        constexpr char kCarriageReturn = '\r';
        constexpr char kAssign = '=';

        Optional<UploadConsent> ConsentFromText(StringView text) {
            if (text == kUnasked) {
                return UploadConsent::Unasked;
            }
            if (text == kGranted) {
                return UploadConsent::Granted;
            }
            if (text == kDeclined) {
                return UploadConsent::Declined;
            }
            return std::nullopt;
        }

        StringView ConsentText(UploadConsent consent) {
            switch (consent) {
                case UploadConsent::Granted:
                    return kGranted;
                case UploadConsent::Declined:
                    return kDeclined;
                case UploadConsent::Unasked:
                    break;
            }
            return kUnasked;
        }

        /// The nickname as the name field would keep it: empty when the field would refuse it
        String CleanNickname(StringView text) {
            TextEntry entry(TextEntry::Kind::Name);
            if (text.size() > kMaxMatchLogNicknameBytes || !entry.Replace(text)) {
                return {};
            }
            return entry.Text();
        }

        std::filesystem::path FileOf(const std::filesystem::path& directory) {
            return directory / ToStd(kUploadPrefsFile);
        }

    }  // namespace

    UploadPrefs ParseUploadPrefs(StringView text) {
        if (text.size() > kMaxUploadPrefsBytes) {
            return {};
        }
        Optional<UploadConsent> consent;
        Optional<String> nickname;
        size_t position = 0;
        while (position < text.size()) {
            size_t end = text.find(kLineEnd, position);
            if (end == StringView::npos) {
                end = text.size();
            }
            StringView line = text.substr(position, end - position);
            position = end + 1;
            if (line.ends_with(kCarriageReturn)) {
                line.remove_suffix(1);
            }
            if (line.empty()) {
                continue;
            }
            const size_t assign = line.find(kAssign);
            if (assign == StringView::npos) {
                return {};
            }
            const StringView key = line.substr(0, assign);
            const StringView value = line.substr(assign + 1);
            if (key == kConsentKey && !consent.has_value()) {
                consent = ConsentFromText(value);
                if (!consent.has_value()) {
                    return {};
                }
            } else if (key == kNicknameKey && !nickname.has_value()) {
                nickname = CleanNickname(value);
            } else {
                return {};
            }
        }
        if (!consent.has_value()) {
            return {};
        }
        UploadPrefs prefs;
        prefs.consent = *consent;
        if (nickname.has_value()) {
            prefs.nickname = *nickname;
        }
        return prefs;
    }

    String FormatUploadPrefs(const UploadPrefs& prefs) {
        String text;
        text.append(kConsentKey.data(), kConsentKey.size());
        text.push_back(kAssign);
        const StringView consentText = ConsentText(prefs.consent);
        text.append(consentText.data(), consentText.size());
        text.push_back(kLineEnd);
        const String nickname = CleanNickname(prefs.nickname);
        if (!nickname.empty()) {
            text.append(kNicknameKey.data(), kNicknameKey.size());
            text.push_back(kAssign);
            text.append(nickname);
            text.push_back(kLineEnd);
        }
        return text;
    }

    UploadPrefs LoadUploadPrefs(const std::filesystem::path& directory) {
        if (directory.empty()) {
            return {};
        }
        std::ifstream file(FileOf(directory), std::ios::binary);
        if (!file) {
            return {};
        }
        // One byte more than the limit tells a file that is too big from one that just fits
        String bytes(kMaxUploadPrefsBytes + 1, '\0');
        file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<size_t>(file.gcount()));
        return ParseUploadPrefs(bytes);
    }

    bool StoreUploadPrefs(const std::filesystem::path& directory, const UploadPrefs& prefs) {
        if (directory.empty()) {
            return false;
        }
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        const std::filesystem::path target = FileOf(directory);
        std::filesystem::path temp = target;
        temp += ToStd(kTempSuffix);
        const String text = FormatUploadPrefs(prefs);
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (!file) {
                return false;
            }
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            file.close();
            if (file.fail()) {
                std::filesystem::remove(temp, error);
                return false;
            }
        }
        std::filesystem::rename(temp, target, error);
        if (error) {
            std::filesystem::remove(temp, error);
            return false;
        }
        NotifySaveChanged();
        return true;
    }

    std::filesystem::path UploadPrefsFolder() {
        return PersistentSaveDirectory();
    }

}  // namespace BECore::Mahjong
