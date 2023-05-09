#include "paginator.h"
#include "search_server.h"
#include "request_queue.h"
#include "test_example_functions.h"
#include "process_queries.h"
#include "log_duration.h"

#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <execution>

#include "test_framework.h"
#include "concurrent_map.h"

using namespace std;

int main() {
    SearchServer search_server("and with"s);
    int id = 0;
    for (
        const string& text : {
            "white cat and yellow hat"s,
            "curly cat curly tail"s,
            "nasty dog with big eyes"s,
            "nasty pigeon john"s,
        }
        ) {
        search_server.AddDocument(++id, text, DocumentStatus::ACTUAL, { 1, 2 });
    }
    cout << "ACTUAL by default:"s << endl;
    // последовательная версия
    for (const Document& document : search_server.FindTopDocuments("curly nasty cat"s)) {
        PrintDocument(document);
    }
    cout << "BANNED:"s << endl;
    // последовательная версия
    for (const Document& document : search_server.FindTopDocuments(execution::seq, "curly nasty cat"s, DocumentStatus::BANNED)) {
        PrintDocument(document);
    }
    cout << "Even ids:"s << endl;
    // параллельная версия
    for (const Document& document : search_server.FindTopDocuments(execution::par, "curly nasty cat"s, [](int document_id, DocumentStatus status, int rating) { return document_id % 2 == 0; })) {
        PrintDocument(document);
    }
    return 0;
}


/*

string GenerateWord(mt19937& generator, int max_length) {
    const int length = uniform_int_distribution(1, max_length)(generator);
    string word;
    word.reserve(length);
    for (int i = 0; i < length; ++i) {
        word.push_back(uniform_int_distribution(0, 26)(generator) + 'a');
    }
    return word;
}

vector<string> GenerateDictionary(mt19937& generator, int word_count, int max_length) {
    vector<string> words;
    words.reserve(word_count);
    for (int i = 0; i < word_count; ++i) {
        words.push_back(GenerateWord(generator, max_length));
    }
    sort(words.begin(), words.end());
    words.erase(unique(words.begin(), words.end()), words.end());
    return words;
}

string GenerateQuery(mt19937& generator, const vector<string>& dictionary, int word_count, double minus_prob = 0) {
    string query;
    for (int i = 0; i < word_count; ++i) {
        if (!query.empty()) {
            query.push_back(' ');
        }
        if (uniform_real_distribution<>(0, 1)(generator) < minus_prob) {
            query.push_back('-');
        }
        query += dictionary[uniform_int_distribution<int>(0, dictionary.size() - 1)(generator)];
    }
    return query;
}

vector<string> GenerateQueries(mt19937& generator, const vector<string>& dictionary, int query_count, int max_word_count) {
    vector<string> queries;
    queries.reserve(query_count);
    for (int i = 0; i < query_count; ++i) {
        queries.push_back(GenerateQuery(generator, dictionary, max_word_count));
    }
    return queries;
}

template <typename ExecutionPolicy>
void Test(string_view mark, SearchServer search_server, const string& query, ExecutionPolicy&& policy) {
    LOG_DURATION(mark);
    const int document_count = search_server.GetDocumentCount();
    int word_count = 0;
    for (int id = 0; id < document_count; ++id) {
        const auto [words, status] = search_server.MatchDocument(policy, query, id);
        word_count += words.size();
    }
    cout << word_count << endl;
}

#define TEST(policy) Test(#policy, search_server, query, execution::policy)

int main() {
    mt19937 generator;

    const auto dictionary = GenerateDictionary(generator, 1000, 10);
    const auto documents = GenerateQueries(generator, dictionary, 10'000, 70);

    const string query = GenerateQuery(generator, dictionary, 500, 0.1);

    SearchServer search_server(dictionary[0]);
    for (size_t i = 0; i < documents.size(); ++i) {
        search_server.AddDocument(i, documents[i], DocumentStatus::ACTUAL, { 1, 2, 3 });
    }

    TEST(seq);
    TEST(par);
}


void PrintMatchDocumentResultUTest(int document_id, const std::vector<std::string_view>& words,
    DocumentStatus status) {
    std::cout << "{ "
        << "document_id = " << document_id << ", "
        << "status = " << static_cast<int>(status) << ", "
        << "words =";
    for (const auto& word : words) {
        std::cout << ' ' << word;
    }
    std::cout << "}" << std::endl;
}

void PrintDocumentUTest(const Document& document) {
    std::cout << "{ "
        << "document_id = " << document.id << ", "
        << "relevance = " << document.relevance << ", "
        << "rating = " << document.rating << " }" << std::endl;
}

void TestMatch() {
    const std::vector<int> ratings1 = { 1, 2, 3, 4, 5 };
    const std::vector<int> ratings2 = { -1, -2, 30, -3, 44, 5 };
    const std::vector<int> ratings3 = { 12, -20, 80, 0, 8, 0, 0, 9, 67 };
    const std::vector<int> ratings4 = { 7, 0, 3, -49, 5 };
    const std::vector<int> ratings5 = { 81, -6, 7, 94, -7 };
    const std::vector<int> ratings6 = { 41, 8, -7, 897, 5 };
    const std::vector<int> ratings7 = { 543, 0, 43, 4, -5 };
    const std::vector<int> ratings8 = { 91, 7, 3, -88, 56 };
    const std::vector<int> ratings9 = { 0, -87, 93, 66, 5 };
    const std::vector<int> ratings10 = { 11, 2, -43, 4, 895 };
    std::string stop_words = "и в на";
    SearchServer search_server(stop_words);

    search_server.AddDocument(0, "белый кот и модный ошейник", DocumentStatus::ACTUAL, ratings1);
    search_server.AddDocument(1, "пушистый кот пушистый хвост", DocumentStatus::ACTUAL, ratings2);
    search_server.AddDocument(2, "ухоженный пёс выразительные глаза", DocumentStatus::ACTUAL,
        ratings3);
    search_server.AddDocument(3, "белый модный кот", DocumentStatus::IRRELEVANT, ratings1);
    search_server.AddDocument(4, "пушистый кот пёс", DocumentStatus::IRRELEVANT, ratings2);
    search_server.AddDocument(5, "ухоженный ошейник выразительные глаза",
        DocumentStatus::IRRELEVANT, ratings3);
    search_server.AddDocument(6, "кот и ошейник", DocumentStatus::BANNED, ratings1);
    search_server.AddDocument(7, "пёс и хвост", DocumentStatus::BANNED, ratings2);
    search_server.AddDocument(8, "модный пёс пушистый хвост", DocumentStatus::BANNED, ratings3);
    search_server.AddDocument(9, "кот пушистый ошейник", DocumentStatus::REMOVED, ratings1);
    search_server.AddDocument(10, "ухоженный кот и пёс", DocumentStatus::REMOVED, ratings2);
    search_server.AddDocument(11, "хвост и выразительные глаза", DocumentStatus::REMOVED, ratings3);

    const std::string query = "пушистый ухоженный кот -ошейник";
    const auto documents = search_server.FindTopDocuments(query);

    std::cout << "Top documents for query:" << std::endl;
    for (const Document& document : documents) {
        PrintDocumentUTest(document);
    }

    std::cout << "Documents' statuses:" << std::endl;
    const int document_count = search_server.GetDocumentCount();
    for (int document_id = 0; document_id < document_count; ++document_id) {
        const auto [words, status] = search_server.MatchDocument(execution::seq, query, document_id);
        PrintMatchDocumentResultUTest(document_id, words, status);
    }
}

int main() {
    setlocale(LC_ALL, "Russian");
    TestMatch();
}
*/