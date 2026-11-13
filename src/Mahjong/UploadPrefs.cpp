#include "UploadPrefs.h"

#include <BECore/Save/SaveStorage.h>
#include <Mahjong/Online/TextEntry.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kConsentKey = "consent";
        constexpr eastl::string_view kNicknameKey = "nickname";
        constexpr eastl::string_view kUnasked = "unasked";
        constexpr eastl::string_view kGranted = "granted";
        constexpr eastl::string_view kDeclined = "declined";
        constexpr eastl::string_view kTempSuffix = ".tmp";
        constexpr char kLineEnd = '\n';
        constexpr char kCarriageReturn = '\r';
        constexpr char kAssign = '=';

        std::optional<UploadConsent> ConsentFromText(eastl::string_view text) {
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

        eastl::string_view ConsentText(UploadConsent consent) {
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
        eastl::string CleanNickname(eastl::string_view text) {
            TextEntry entry(TextEntry::Kind::Name);
            if (text.size() > kMaxMatchLogNicknameBytes || !entry.Set(text)) {
                return {};
            }
            return entry.Text();
        }

        std::filesystem::path FileOf(const std::filesystem::path& directory) {
            return directory / std::string(kUploadPrefsFile.data(), kUploadPrefsFile.size());
        }

    }  // namespace

    UploadPrefs ParseUploadPrefs(eastl::string_view text) {
        if (text.size() > kMaxUploadPrefsBytes) {
            return {};
        }
        std::optional<UploadConsent> consent;
        std::optional<eastl::string> nickname;
        size_t position = 0;
        while (position < text.size()) {
            size_t end = text.find(kLineEnd, position);
            if (end == eastl::string_view::npos) {
                end = text.size();
            }
            eastl::string_view line = text.substr(position, end - position);
            position = end + 1;
            if (line.ends_with(kCarriageReturn)) {
                line.remove_suffix(1);
            }
            if (line.empty()) {
                continue;
            }
            const size_t assign = line.find(kAssign);
            if (assign == eastl::string_view::npos) {
                return {};
            }
            const eastl::string_view key = line.substr(0, assign);
            const eastl::string_view value = line.substr(assign + 1);
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

    eastl::string FormatUploadPrefs(const UploadPrefs& prefs) {
        eastl::string text;
        text.append(kConsentKey.data(), kConsentKey.size());
        text.push_back(kAssign);
        const eastl::string_view consentText = ConsentText(prefs.consent);
        text.append(consentText.data(), consentText.size());
        text.push_back(kLineEnd);
        const eastl::string nickname = CleanNickname(prefs.nickname);
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
        std::string bytes(kMaxUploadPrefsBytes + 1, '\0');
        file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<size_t>(file.gcount()));
        return ParseUploadPrefs(eastl::string_view(bytes.data(), bytes.size()));
    }

    bool StoreUploadPrefs(const std::filesystem::path& directory, const UploadPrefs& prefs) {
        if (directory.empty()) {
            return false;
        }
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        const std::filesystem::path target = FileOf(directory);
        std::filesystem::path temp = target;
        temp += std::string(kTempSuffix.data(), kTempSuffix.size());
        const eastl::string text = FormatUploadPrefs(prefs);
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

    bool UploadChoiceIsRemembered() {
        return GetSaveStorageState() == SaveStorageState::Persistent;
    }

}  // namespace BECore::Mahjong
