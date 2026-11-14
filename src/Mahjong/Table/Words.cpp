#include "Words.h"

namespace BECore::Mahjong {

    namespace {

        const StringView* FindArg(eastl::span<const WordArg> args, StringView name) {
            for (const WordArg& arg : args) {
                if (arg.first == name) {
                    return &arg.second;
                }
            }
            return nullptr;
        }

    }  // namespace

    String Subst(StringView text, WordArgs args) {
        return Subst(text, eastl::span<const WordArg>(args.begin(), args.size()));
    }

    String Subst(StringView text, eastl::span<const WordArg> args) {
        String out;
        size_t at = 0;
        while (at < text.size()) {
            const size_t open = text.find('{', at);
            if (open == StringView::npos) {
                break;
            }
            out.append(text.data() + at, open - at);
            const size_t close = text.find('}', open);
            const size_t nextOpen = text.find('{', open + 1);
            if (close == StringView::npos || (nextOpen != StringView::npos && nextOpen < close)) {
                out.push_back('{');  // not a placeholder: written as it is, a later '{' may still start one
                at = open + 1;
                continue;
            }
            const StringView name = text.substr(open + 1, close - open - 1);
            if (const StringView* value = FindArg(args, name)) {
                out.append(value->data(), value->size());
            } else {
                out.append(text.data() + open, close - open + 1);
            }
            at = close + 1;
        }
        out.append(text.data() + at, text.size() - at);
        return out;
    }

    String Tr(const UIStrings& strings, StringView key) {
        const Optional<StringView> found = strings.Find(key);
        return found ? String(found->data(), found->size()) : String(key.data(), key.size());
    }

    String Tr(const UIStrings& strings, StringView key, WordArgs args) {
        return Subst(Tr(strings, key), args);
    }

    String Tr(const UIStrings& strings, StringView key, eastl::span<const WordArg> args) {
        return Subst(Tr(strings, key), args);
    }

    String WindName(const UIStrings& strings, Wind wind) {
        return Tr(strings, Format("wind.{}", EnumUtils<Wind>::ToString(wind)).c_str());
    }

    String WindLetter(const UIStrings& strings, Wind wind) {
        return Tr(strings, Format("wind.letter.{}", EnumUtils<Wind>::ToString(wind)).c_str());
    }

}  // namespace BECore::Mahjong
