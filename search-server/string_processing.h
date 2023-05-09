#pragma once

#include <map>
#include <set>
#include <vector>
#include <string>

using namespace std;

vector<string_view> SplitIntoWords(string_view text);

template <typename StringContainer>
set<string> MakeUniqueNonEmptyStrings(const StringContainer& strings) {
    set<string> non_empty_strings;
    for (const auto str : strings) {
        if (!str.empty()) {
            non_empty_strings.insert(static_cast<string>(str));
        }
    }
    return non_empty_strings;
}