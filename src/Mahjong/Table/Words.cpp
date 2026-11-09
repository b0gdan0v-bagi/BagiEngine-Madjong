#include "Words.h"

namespace BECore::Mahjong {

    namespace {

        const eastl::string_view* FindArg(std::span<const WordArg> args, eastl::string_view name) {
            for (const WordArg& arg : args) {
                if (arg.first == name) {
                    return &arg.second;
                }
            }
            return nullptr;
        }

    }  // namespace

    eastl::string Subst(eastl::string_view text, WordArgs args) {
        return Subst(text, std::span<const WordArg>(args.begin(), args.size()));
    }

    eastl::string Subst(eastl::string_view text, std::span<const WordArg> args) {
        eastl::string out;
        size_t at = 0;
        while (at < text.size()) {
            const size_t open = text.find('{', at);
            if (open == eastl::string_view::npos) {
                break;
            }
            out.append(text.data() + at, open - at);
            const size_t close = text.find('}', open);
            const size_t nextOpen = text.find('{', open + 1);
            if (close == eastl::string_view::npos || (nextOpen != eastl::string_view::npos && nextOpen < close)) {
                out.push_back('{');  // not a placeholder: written as it is, a later '{' may still start one
                at = open + 1;
                continue;
            }
            const eastl::string_view name = text.substr(open + 1, close - open - 1);
            if (const eastl::string_view* value = FindArg(args, name)) {
                out.append(value->data(), value->size());
            } else {
                out.append(text.data() + open, close - open + 1);
            }
            at = close + 1;
        }
        out.append(text.data() + at, text.size() - at);
        return out;
    }

    eastl::string Tr(const UIStrings& strings, eastl::string_view key) {
        const std::optional<eastl::string_view> found = strings.Find(key);
        return found ? eastl::string(found->data(), found->size()) : eastl::string(key.data(), key.size());
    }

    eastl::string Tr(const UIStrings& strings, eastl::string_view key, WordArgs args) {
        return Subst(Tr(strings, key), args);
    }

    eastl::string Tr(const UIStrings& strings, eastl::string_view key, std::span<const WordArg> args) {
        return Subst(Tr(strings, key), args);
    }

    eastl::string WindName(const UIStrings& strings, Wind wind) {
        return Tr(strings, Format("wind.{}", EnumUtils<Wind>::ToString(wind)).c_str());
    }

}  // namespace BECore::Mahjong
