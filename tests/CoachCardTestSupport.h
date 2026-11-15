#pragma once

#include <Mahjong/Table/HintCard.h>
#include <string>
#include <tests/MahjongTableTestSupport.h>
#include <tests/StringsTestSupport.h>
#include <vector>

namespace BECore::Mahjong::Tests {

    inline Vector<String> CardVarNames() {
        Vector<String> names{"cardTitle", "cardNow"};
        for (const char* cell : {"cardWho", "cardMove", "cardSteps", "cardUseful"}) {
            for (size_t i = 0; i < kMaxCardRows; ++i) {
                names.push_back(Format("{}{}", cell, i));
            }
        }
        for (size_t i = 0; i < kMaxCardFooter; ++i) {
            names.push_back(Format("cardFoot{}", i));
        }
        return names;
    }

    /// What `card` puts into the variables, in the order of `CardVarNames`, empty where it has nothing.
    inline Vector<String> CardTexts(const HintCard& card) {
        const auto text = [](const String& value) { return String(value.data(), value.size()); };
        Vector<String> texts{text(card.title), text(card.now)};
        const auto cells = [&](auto member) {
            for (size_t i = 0; i < kMaxCardRows; ++i) {
                texts.push_back(i < card.rows.size() ? text(card.rows[i].*member) : String());
            }
        };
        cells(&CardRow::who);
        cells(&CardRow::move);
        cells(&CardRow::steps);
        cells(&CardRow::useful);
        for (size_t i = 0; i < kMaxCardFooter; ++i) {
            texts.push_back(i < card.footer.size() ? text(card.footer[i]) : String());
        }
        return texts;
    }

    inline Vector<String> CardVars() {
        Vector<String> values;
        for (const String& name : CardVarNames()) {
            values.push_back(Var(name.c_str()));
        }
        return values;
    }

    inline bool NoCardText() {
        for (const String& value : CardVars()) {
            if (!value.empty()) {
                return false;
            }
        }
        return true;
    }

    /// Sets every variable of the card to the longest text it takes over `cards` (each cell of every row to the longest of that cell).
    inline void SetLongestCardTexts(const Vector<HintCard>& cards) {
        const Vector<String> names = CardVarNames();
        Vector<Vector<String>> candidates(names.size());
        for (const HintCard& card : cards) {
            const Vector<String> texts = CardTexts(card);
            for (size_t at = 0; at < texts.size(); ++at) {
                candidates[at].emplace_back(texts[at].c_str());
            }
        }
        // A cell of a row takes the longest of that cell in any row, a line under the rows the longest of any line
        const auto widen = [&](size_t first, size_t count) {
            Vector<String> all;
            for (size_t at = first; at < first + count; ++at) {
                all.insert(all.end(), candidates[at].begin(), candidates[at].end());
            }
            for (size_t at = first; at < first + count; ++at) {
                candidates[at] = all;
            }
        };
        for (size_t cell = 0; cell < 4; ++cell) {
            widen(2 + cell * kMaxCardRows, kMaxCardRows);
        }
        widen(2 + 4 * kMaxCardRows, kMaxCardFooter);
        for (size_t at = 0; at < names.size(); ++at) {
            const String longest = LongestText(candidates[at]);
            CoreManager::GetUIVars().Set(PoolString::Intern(names[at]), StringView(longest));
        }
    }

}  // namespace BECore::Mahjong::Tests
