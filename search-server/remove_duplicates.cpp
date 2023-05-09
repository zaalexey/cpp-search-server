#include "remove_duplicates.h"
using namespace std;

void RemoveDuplicates(SearchServer& search_server) {
	set<int> id_duplicate;
	set<set<string>> documents;

	for (auto id = search_server.begin(); id != search_server.end(); id++) {
		const auto& word_freq = search_server.GetWordFrequencies(*id);
		set<string> words;
		for (const auto [word, freq] : word_freq) {
			words.insert(static_cast<std::string>(word)); //*all_words_.find(word)
		}
		if (documents.count(words))
			id_duplicate.insert(*id);
		else
			documents.insert(words);
	}

	for (auto id : id_duplicate) {
		cout << "Found duplicate document id "s << id << endl;
		search_server.RemoveDocument(id);
	}
}

