#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <map>
#include <cmath>

using namespace std;

const int MAX_RESULT_DOCUMENT_COUNT = 5;

string ReadLine() {
    string s;
    getline(cin, s);
    return s;
}

int ReadLineWithNumber() {
    int result = 0;
    cin >> result;
    ReadLine();
    return result;
}

vector<string> SplitIntoWords(const string& text) {
    vector<string> words;
    string word;
    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        }
        else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }

    return words;
}

struct Document {
    int id;
    double relevance;
};

class SearchServer {
public:
    void SetStopWords(const string& text) {
        for (const string& word : SplitIntoWords(text)) {
            stop_words_.insert(word);
        }
    }
    //Создаем список слов из всех документов и пределяем tf
    void AddDocument(int document_id, const string& document) {
        const vector<string> words = SplitIntoWordsNoStop(document);
        document_count_++;
        double tf = 1.0 / words.size();
        for (auto word : words) {
                word_to_document_freqs_[word][document_id] += tf;
        }
    }

    vector<Document> FindTopDocuments(const string& raw_query) const {
        struct Query query_words = ParseQuery(raw_query);
        auto matched_documents = FindAllDocuments(query_words);

        sort(matched_documents.begin(), matched_documents.end(),
            [](const Document& lhs, const Document& rhs) {
                return lhs.relevance > rhs.relevance;
            });
        if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
            matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
        }
        return matched_documents;
    }

private:


    struct Query {
        set<string> words_plus;
        set<string> words_minus;
    };

    map<string, map<int, double>> word_to_document_freqs_;   //Слова из поискового запроса, id докумена в котором оно встречается, TF

    set<string> stop_words_;    //Стоп слова

    int document_count_ = 0;    //Общее кол-во документов

    bool IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }

    vector<string> SplitIntoWordsNoStop(const string& text) const {
        vector<string> words;
        for (const string& word : SplitIntoWords(text)) {
            if (!IsStopWord(word)) {
                words.push_back(word);
            }
        }
        return words;
    }

    struct Query ParseQuery(const string& text) const {
        struct Query query_words;
        for (string& word : SplitIntoWordsNoStop(text)) {
            if (word[0] == '-') {
                query_words.words_minus.insert(word.substr(1)); //Добавляем минус слова
            }
            else
                query_words.words_plus.insert(word); //Добавляем плюс слова
        }
        return query_words;
    }

    vector<Document> FindAllDocuments(const struct Query& query_words) const {
        vector<Document> matched_documents;
        map<int, double> document_to_relevance;
        double idf;
        double size_to_idf;
        for (auto word : query_words.words_plus) {
            if (word_to_document_freqs_.count(word)) {
                size_to_idf = word_to_document_freqs_.at(word).size();
                idf = log(document_count_ / size_to_idf);
                for (auto [id, tf] : word_to_document_freqs_.at(word)) {
                    document_to_relevance[id] += idf * tf;
                }
            }
        }
         for (auto word : query_words.words_minus) {
            if (word_to_document_freqs_.count(word)) {
                for (auto [id, tf] : word_to_document_freqs_.at(word)) {
                    if (document_to_relevance.count(id))
                        document_to_relevance.erase(id);
                }
            }
        }
        for (const auto& [id, relevance] : document_to_relevance) {
            matched_documents.push_back({ id, relevance });
        }
        return matched_documents;
    }

};

SearchServer CreateSearchServer() {
    SearchServer search_server;
    search_server.SetStopWords(ReadLine());

    const int document_count = ReadLineWithNumber();
    for (int document_id = 0; document_id < document_count; ++document_id) {
        search_server.AddDocument(document_id, ReadLine());
    }

    return search_server;
}

int main() {
    const SearchServer search_server = CreateSearchServer();

    const string query = ReadLine();
    for (const auto& [document_id, relevance] : search_server.FindTopDocuments(query)) {
        cout << "{ document_id = "s << document_id << ", "
            << "relevance = "s << relevance << " }"s << endl;
    }
}
