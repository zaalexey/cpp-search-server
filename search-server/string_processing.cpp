#include "string_processing.h"

vector<string_view> SplitIntoWords(string_view text) {
	vector <string_view> result;
	int64_t pos = 0;
	const int64_t pos_end = text.npos;
	while (true) {
		int64_t space = text.find(' ', pos);
		result.push_back(space == pos_end ? text.substr(pos) : text.substr(pos, space - pos));
		if (space == pos_end) {
			break;
		}
		else {
			pos = space + 1;
		}
	}
	return result;
}