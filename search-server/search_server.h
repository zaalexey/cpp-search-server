#pragma once

#include <map>
#include <set>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <numeric>
#include <execution>
#include <future>

#include "document.h"
#include "string_processing.h"
#include "concurrent_map.h"

using namespace std;

const int MAX_RESULT_DOCUMENT_COUNT = 5;
const double ACCURACY = 1e-6;
const int MAX_THREAD = 100; // максимальное кол-во потоков выполенения

class SearchServer {
public:
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words);
    explicit SearchServer(const string& stop_words_text);
    explicit SearchServer(string_view stop_words);

    void AddDocument(int document_id, const string_view document, DocumentStatus status, const vector<int>& ratings);

    template <typename DocumentPredicate>
    vector<Document> FindTopDocuments(string_view raw_query, DocumentPredicate document_predicate) const;
    vector<Document> FindTopDocuments(string_view raw_query, DocumentStatus status) const;
    vector<Document> FindTopDocuments(string_view raw_query) const;

    template <typename DocumentPredicate, typename ExecutionPolicy>
    vector<Document> FindTopDocuments(ExecutionPolicy&& policy, string_view raw_query, DocumentPredicate document_predicate) const;
    template <typename ExecutionPolicy>
    vector<Document> FindTopDocuments(ExecutionPolicy&& policy, string_view raw_query, DocumentStatus status) const;
    template <typename ExecutionPolicy>
    vector<Document> FindTopDocuments(ExecutionPolicy&& policy, string_view raw_query) const;

    int GetDocumentCount() const;

    set<int>::const_iterator begin() const;
    set<int>::const_iterator end() const;
    const map<string_view, double>& GetWordFrequencies(int document_id) const;
    
    template <typename ExecutionPolicy>
    void RemoveDocument(ExecutionPolicy&& policy, int document_id);
    void RemoveDocument(int document_id);

    tuple<vector<string_view>, DocumentStatus> MatchDocument(string_view raw_query, int document_id) const;
    tuple<vector<string_view>, DocumentStatus> MatchDocument(const execution::sequenced_policy&, string_view raw_query, int document_id) const;
    tuple<vector<string_view>, DocumentStatus> MatchDocument(const execution::parallel_policy&, string_view raw_query, int document_id) const;
  
    
private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };
    struct QueryWord {
        string_view data;
        bool is_minus;
        bool is_stop;
    };
    struct Query {
        vector<string_view> plus_words;
        vector<string_view> minus_words;
    };

    const set<string> stop_words_;
    map<string_view, map<int, double>> word_to_document_freqs_;
    map<int, DocumentData> documents_;
    set<int> document_ids_;
    map<int, map<string_view, double>> document_to_word_freqs_;
    set<string, less<>> all_words_;

    bool IsStopWord(string_view word) const;
    static bool IsValidWord(string_view word);
    vector<string_view> SplitIntoWordsNoStop(string_view text) const;
    static int ComputeAverageRating(const vector<int>& ratings);
    QueryWord ParseQueryWord(string_view text) const;
    Query ParseQuery(string_view text, bool is_seq) const;
    // Existence required
    double ComputeWordInverseDocumentFreq(string_view word) const;

    template <typename DocumentPredicate, class ExecutionPolicy>
    vector<Document> FindAllDocuments(ExecutionPolicy&& policy, const Query& query, DocumentPredicate document_predicate) const;
};

template <typename StringContainer>
SearchServer::SearchServer(const StringContainer& stop_words)
    : stop_words_(MakeUniqueNonEmptyStrings(stop_words))  // Extract non-empty stop words
    {
        if (!all_of(stop_words_.begin(), stop_words_.end(), IsValidWord)) {
            throw invalid_argument("Some of stop words are invalid"s);
        }
    }

template <typename DocumentPredicate, typename ExecutionPolicy>
vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy&& policy, string_view raw_query, DocumentPredicate document_predicate) const {
    const auto query = ParseQuery(raw_query, true);

    auto matched_documents = FindAllDocuments(policy, query, document_predicate);

    sort(policy, matched_documents.begin(), matched_documents.end(), [](const Document& lhs, const Document& rhs) {
        if (std::abs(lhs.relevance - rhs.relevance) < ACCURACY) {
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

template <typename DocumentPredicate>
vector<Document> SearchServer::FindTopDocuments(string_view raw_query, DocumentPredicate document_predicate) const {
    return FindTopDocuments(execution::seq, raw_query, document_predicate);
}

template <typename ExecutionPolicy>
vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy&& policy, string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(policy, raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
        return document_status == status;
        });
}
template <typename ExecutionPolicy>
vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy&& policy, string_view raw_query) const {
    return FindTopDocuments(policy, raw_query, DocumentStatus::ACTUAL);
}


template<typename ExecutionPolicy>
inline void SearchServer::RemoveDocument(ExecutionPolicy&& policy, int document_id) {
    if (document_to_word_freqs_.count(document_id) == 0) {
        return;
    }
    vector<const std::string*> words_to_remove(document_to_word_freqs_.at(document_id).size());
    
    transform(
        policy,
        document_to_word_freqs_.at(document_id).begin(),
        document_to_word_freqs_.at(document_id).end(),
        words_to_remove.begin(),
        [](auto &word_freqs) { return &word_freqs.first; });
    
    for_each(
        policy,
        words_to_remove.begin(),
        words_to_remove.end(),
        [this, &document_id](const string* word) { word_to_document_freqs_.at(*word).erase(document_id); });
    
    documents_.erase(document_id);
    document_ids_.erase(document_id);
    document_to_word_freqs_.erase(document_id);
}


template <typename DocumentPredicate, class ExecutionPolicy>
vector<Document> SearchServer::FindAllDocuments(ExecutionPolicy&& policy, const Query& query, DocumentPredicate document_predicate) const {
    
    ConcurrentMap<int, double> document_to_relevance(MAX_THREAD);
    
    auto f_plus = [this, &document_predicate, &document_to_relevance](const string_view word) {
        if (word_to_document_freqs_.count(word) == 0) {
            return;
        }
        const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
        for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
            const auto& document_data = documents_.at(document_id);
            if (document_predicate(document_id, document_data.status, document_data.rating)) {
                document_to_relevance[document_id].ref_to_value += term_freq * inverse_document_freq;
            }
        }
    };
    for_each(policy, query.plus_words.begin(), query.plus_words.end(), f_plus);


    auto f_minus = [this, &document_predicate, &document_to_relevance](const string_view word) {
        if (word_to_document_freqs_.count(word) == 0) {
            return;
        }
        for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
            document_to_relevance.erase(document_id);
        }
    };

    for_each(policy, query.minus_words.begin(), query.minus_words.end(), f_minus);

    vector<Document> matched_documents;
    for (const auto [document_id, relevance] : document_to_relevance.BuildOrdinaryMap()) {
        matched_documents.push_back({ document_id, relevance, documents_.at(document_id).rating });
    }

    return matched_documents;
}