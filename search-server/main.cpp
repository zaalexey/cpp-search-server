
#include <iostream>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>


using namespace std;

const int MAX_RESULT_DOCUMENT_COUNT = 5;

string ReadLine() {
    string s;
    getline(cin, s);
    return s;
}

int ReadLineWithNumber() {
    int result;
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
    int rating;
};

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

class SearchServer {
public:
    void SetStopWords(const string& text) {
        for (const string& word : SplitIntoWords(text)) {
            stop_words_.insert(word);
        }
    }

    void AddDocument(int document_id, const string& document, DocumentStatus status, const vector<int>& ratings) {
        const vector<string> words = SplitIntoWordsNoStop(document);
        const double inv_word_count = 1.0 / words.size();
        for (const string& word : words) {
            word_to_document_freqs_[word][document_id] += inv_word_count;
        }
        documents_.emplace(document_id, DocumentData{ ComputeAverageRating(ratings), status });
    }

    template <typename KeyMapper>
    vector<Document> FindTopDocuments(const string& raw_query, KeyMapper key_mapper) const {
        const Query query = ParseQuery(raw_query);
        auto matched_documents = FindAllDocuments(query, key_mapper);

        sort(matched_documents.begin(), matched_documents.end(),
            [](const Document& lhs, const Document& rhs) {
                if (abs(lhs.relevance - rhs.relevance) < 1e-6) {
                    return lhs.rating > rhs.rating;
                }
                else {
                    return lhs.relevance > rhs.relevance;
                }
            });
        if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
            matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
        }
        return matched_documents;
    }

    vector<Document> FindTopDocuments(const string& raw_query, DocumentStatus status_from_query = DocumentStatus::ACTUAL) const {
        return FindTopDocuments(raw_query, [status_from_query](int document_id, DocumentStatus status, int rating)
            {
                return status_from_query == status;
            });
    }

    int GetDocumentCount() const {
        return documents_.size();
    }

    tuple<vector<string>, DocumentStatus> MatchDocument(const string& raw_query,
        int document_id) const {
        const Query query = ParseQuery(raw_query);
        vector<string> matched_words;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                break;
            }
        }
        return { matched_words, documents_.at(document_id).status };
    }

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };

    set<string> stop_words_;
    map<string, map<int, double>> word_to_document_freqs_;
    map<int, DocumentData> documents_;

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

    static int ComputeAverageRating(const vector<int>& ratings) {
        if (ratings.empty()) {
            return 0;
        }
        int rating_sum = 0;
        for (const int rating : ratings) {
            rating_sum += rating;
        }
        return rating_sum / static_cast<int>(ratings.size());
    }

    struct QueryWord {
        string data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(string text) const {
        bool is_minus = false;
        // Word shouldn't be empty
        if (text[0] == '-') {
            is_minus = true;
            text = text.substr(1);
        }
        return { text, is_minus, IsStopWord(text) };
    }

    struct Query {
        set<string> plus_words;
        set<string> minus_words;
    };

    Query ParseQuery(const string& text) const {
        Query query;
        for (const string& word : SplitIntoWords(text)) {
            const QueryWord query_word = ParseQueryWord(word);
            if (!query_word.is_stop) {
                if (query_word.is_minus) {
                    query.minus_words.insert(query_word.data);
                }
                else {
                    query.plus_words.insert(query_word.data);
                }
            }
        }
        return query;
    }

    // Existence required
    double ComputeWordInverseDocumentFreq(const string& word) const {
        return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
    }

    template <typename KeyMapper>
    vector<Document> FindAllDocuments(const Query& query, KeyMapper key_mapper) const {
        map<int, double> document_to_relevance;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
            for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                auto dociments_at_id = documents_.at(document_id);
                if (key_mapper(document_id, dociments_at_id.status, dociments_at_id.rating)) {
                    document_to_relevance[document_id] += term_freq * inverse_document_freq;
                }
            }
        }

        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
                document_to_relevance.erase(document_id);
            }
        }

        vector<Document> matched_documents;
        for (const auto [document_id, relevance] : document_to_relevance) {
            matched_documents.push_back(
                { document_id, relevance, documents_.at(document_id).rating });
        }
        return matched_documents;
    }
};

//---------Макросы ---------------------------------------------
template <typename T, typename U>
void AssertEqualImpl(const T& t, const U& u, const string& t_str, const string& u_str, const string& file,
    const string& func, unsigned line, const string& hint) {
    if (t != u) {
        cout << boolalpha;
        cout << file << "("s << line << "): "s << func << ": "s;
        cout << "ASSERT_EQUAL("s << t_str << ", "s << u_str << ") failed: "s;
        cout << t << " != "s << u << "."s;
        if (!hint.empty()) {
            cout << " Hint: "s << hint;
        }
        cout << endl;
        abort();
    }
}
#define ASSERT_EQUAL(a, b) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, ""s)
#define ASSERT_EQUAL_HINT(a, b, hint) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, (hint))


void AssertImpl(bool value, const string& expr_str, const string& file, const string& func, unsigned line,
    const string& hint) {
    if (!value) {
        cout << file << "("s << line << "): "s << func << ": "s;
        cout << "ASSERT("s << expr_str << ") failed."s;
        if (!hint.empty()) {
            cout << " Hint: "s << hint;
        }
        cout << endl;
        abort();
    }
}
#define ASSERT(expr) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, ""s)
#define ASSERT_HINT(expr, hint) AssertImpl(!!(expr), #expr, __FILE__, __FUNCTION__, __LINE__, (hint))


template <typename T>
void RunTestImpl(T func, string name) {
    func();
    cerr << name << " OK" << endl;
}
#define RUN_TEST(func)  RunTestImpl((func), #func)

//---------Окончание макросы-----------------------------------

// -------- Начало модульных тестов поисковой системы ----------

// Тест Добавление документов
void TestAddDocument() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = { 1, 2, 3 };
    // Поиск слова входящего в документ
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("in"s);
        ASSERT_EQUAL(found_docs.size(), 1u);
    }

    // Поиск слова не входящего в документ
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("dog"s);
        ASSERT_EQUAL(found_docs.size(), 0u);
    }
}

// Тест проверяет, что поисковая система исключает стоп-слова при добавлении документов
void TestExcludeStopWordsFromAddedDocumentContent() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = { 1, 2, 3 };
    // Сначала убеждаемся, что поиск слова, не входящего в список стоп-слов,
    // находит нужный документ
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("in"s);
        ASSERT_EQUAL(found_docs.size(), 1u);
        const Document& doc0 = found_docs[0];
        ASSERT_EQUAL(doc0.id, doc_id);
    }

    // Затем убеждаемся, что поиск этого же слова, входящего в список стоп-слов,
    // возвращает пустой результат
    {
        SearchServer server;
        server.SetStopWords("in the"s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        ASSERT_HINT(server.FindTopDocuments("in"s).empty(), "Stop words must be excluded from documents"s);
    }
}

// Тест Поддержка минус-слов
void TestMinusWord() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = { 1, 2, 3 };

    // Документ не содержащий минус слова включается в результат поиска
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("cat"s);
        ASSERT_EQUAL(found_docs.size(), 1u);
    }

    // Документ содержащий минус слова не должен включаться в результат поиска
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("cat  -city"s);
        ASSERT_EQUAL(found_docs.size(), 0u);
    }
}

// Тест Матчинг документов.
void TestMatchDocument() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = { 1, 2, 3 };

    // Проверяем возвращаемые слова и статус документа
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const tuple<vector<string>, DocumentStatus> match_document = server.MatchDocument("cat"s, doc_id);
        ASSERT_EQUAL(get<0>(match_document)[0], "cat"s);
        ASSERT_EQUAL(static_cast<int>(get<1>(match_document)), static_cast<int>(DocumentStatus::ACTUAL));
    }

    // Проверяем возвращаемые слова при наличии минус слов
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const tuple<vector<string>, DocumentStatus> match_document = server.MatchDocument("cat -city"s, doc_id);
        ASSERT_EQUAL(get<0>(match_document).size(), 0u);
    }
}

// Тест сортировки документов по релевантности
void TestSortRelevance() {
    SearchServer server;
    server.AddDocument(23, "cat in the city"s, DocumentStatus::ACTUAL, { 2, 3, 5 });
    server.AddDocument(14, "blue cat in the city"s, DocumentStatus::ACTUAL, { 4, 3 });
    const auto found_docs = server.FindTopDocuments("blue cat"s);
    // релевантность документа с id = 14 поисковому запросу должна быть выше
    ASSERT_EQUAL(found_docs[0].id, 14u);
    ASSERT_EQUAL(found_docs[1].id, 23u);
}

// Тест рейтинга вычисления рейтинга документа
void TestCalcRatings() {
    SearchServer server;
    server.AddDocument(23, "cat in the city"s, DocumentStatus::ACTUAL, { 2, 3, 4, -1 });
    const auto found_docs = server.FindTopDocuments("blue cat"s);

    // Рейтинг документа должен быть (2+3+4-1)/4 = 2
    ASSERT_EQUAL(found_docs[0].rating, 2u);
}

// Тест Фильтрация результатов поиска с использованием предиката, задаваемого пользователем.
void TestFilteringResultPredicate() {
    SearchServer server;
    server.AddDocument(23, "cat in the city"s, DocumentStatus::ACTUAL, { 2, 2 });
    server.AddDocument(15, "dog in the city"s, DocumentStatus::BANNED, { 5, 3 });
    const auto found_docs = server.FindTopDocuments("city"s, [](int document_id, DocumentStatus status, int rating) { return rating >= 4; });

    // Рейтин документа должен быть >= 4
    ASSERT(found_docs[0].rating >= 4);
    ASSERT(found_docs[0].id == 15);
}

//Тест Поиск документов, имеющих заданный статус
void TestSearchDocWithStatus() {
    SearchServer server;
    server.AddDocument(23, "cat in the city"s, DocumentStatus::ACTUAL, { 2, 3 });
    server.AddDocument(31, "dog in the city"s, DocumentStatus::BANNED, { 4, -1 });
    const auto found_docs = server.FindTopDocuments("city"s, DocumentStatus::BANNED);

    // id документа должен быть 31
    ASSERT(found_docs[0].id == 31);
}

// Тест Корректное вычисление релевантности найденных документов.
void TestCalcRelevance() {
    SearchServer server;
    //server.SetStopWords("in the"s);
    server.AddDocument(23, "white cat big city"s, DocumentStatus::ACTUAL, { 2, 3, 5 });
    server.AddDocument(14, "fluffy cat blue tail"s, DocumentStatus::ACTUAL, { 1, 3 });
    server.AddDocument(2, "groomed dog small ears"s, DocumentStatus::ACTUAL, { -2, 3 });
    const auto found_docs = server.FindTopDocuments("cat"s);

    // Вычисляем релевантность
    double tf_idf = 0;
    double doc_count = 3;
    double doc_count_with_word = 2;
    double all_word_in_doc = 4;
    double query_word = 1;
    tf_idf = (query_word / all_word_in_doc) * log(doc_count / doc_count_with_word);

    ASSERT((found_docs[0].id == 23 && found_docs[0].relevance == tf_idf));
    ASSERT((found_docs[1].id == 14 && found_docs[1].relevance == tf_idf));
}

// Функция TestSearchServer является точкой входа для запуска тестов
void TestSearchServer() {
    RUN_TEST(TestExcludeStopWordsFromAddedDocumentContent);
    RUN_TEST(TestAddDocument);
    RUN_TEST(TestMinusWord);
    RUN_TEST(TestMatchDocument);
    RUN_TEST(TestSortRelevance);
    RUN_TEST(TestCalcRatings);
    RUN_TEST(TestFilteringResultPredicate);
    RUN_TEST(TestSearchDocWithStatus);
    RUN_TEST(TestCalcRelevance);
    // Не забудьте вызывать остальные тесты здесь
}

// --------- Окончание модульных тестов поисковой системы -----------

int main() {
    TestSearchServer();
    // Если вы видите эту строку, значит все тесты прошли успешно
    cout << "Search server testing finished"s << endl;
}
