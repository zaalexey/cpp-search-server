#include "search_server.h"


SearchServer::SearchServer(const string& stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text))  // Invoke delegating constructor
                                                        // from string container
{
}

SearchServer::SearchServer(string_view stop_words)
    : SearchServer(SplitIntoWords(stop_words))
{
}


void SearchServer::AddDocument(int document_id, string_view document, DocumentStatus status, const vector<int>& ratings) {
    if ((document_id < 0) || (documents_.count(document_id) > 0)) {
        throw invalid_argument("Invalid document_id"s);
    }
    
    auto element = all_words_.emplace(document);
    const auto words = SplitIntoWordsNoStop(*element.first);
    const double inv_word_count = 1.0 / words.size();
    for (const auto word : words) {
        word_to_document_freqs_[word][document_id] += inv_word_count;
        document_to_word_freqs_[document_id][word] += inv_word_count;
    }
    documents_.emplace(document_id, DocumentData{ ComputeAverageRating(ratings), status });
    document_ids_.insert(document_id);
}


vector<Document> SearchServer::FindTopDocuments(string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(execution::seq, raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
        return document_status == status;
        });
}

vector<Document> SearchServer::FindTopDocuments(string_view raw_query) const {
    return FindTopDocuments(execution::seq, raw_query, DocumentStatus::ACTUAL);
}

int SearchServer::GetDocumentCount() const {
    return documents_.size();
}

set<int>::const_iterator SearchServer::begin() const {
    return document_ids_.begin();
}

set<int>::const_iterator SearchServer::end() const {
    return document_ids_.end();
}

const map<string_view, double>& SearchServer::GetWordFrequencies(int document_id) const {
    static map<string_view, double> word_frequencies = {};
        if (document_to_word_freqs_.count(document_id))
            return document_to_word_freqs_.at(document_id);
        else
            return word_frequencies;
}

void SearchServer::RemoveDocument(int document_id) {
    for (auto [word, freq] : document_to_word_freqs_.at(document_id)) {
        word_to_document_freqs_.at(*all_words_.find(word)).erase(document_id);
    }

    documents_.erase(document_id);
    document_ids_.erase(document_id);
    document_to_word_freqs_.erase(document_id);
}

tuple<vector<string_view>, DocumentStatus> SearchServer::MatchDocument(string_view raw_query, int document_id) const {
    const auto query = ParseQuery(raw_query, true);

    vector<string_view> matched_words;
    for (const auto word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        if (word_to_document_freqs_.at(word).count(document_id)) {
            return { {}, documents_.at(document_id).status };
        }
    }

    for (const auto word : query.plus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        if (word_to_document_freqs_.at(word).count(document_id)) {
            matched_words.push_back(word);
        }
    }

    return { matched_words, documents_.at(document_id).status };
}

tuple<vector<string_view>, DocumentStatus> SearchServer::MatchDocument(const execution::sequenced_policy&, string_view raw_query, int document_id) const {
    return MatchDocument(raw_query, document_id);
}

tuple<vector<string_view>, DocumentStatus> SearchServer::MatchDocument(const execution::parallel_policy&, string_view raw_query, int document_id) const {
    if (documents_.count(document_id) == 0) {
        throw out_of_range("No document with id "s + to_string(document_id));
    }

    Query query = ParseQuery(raw_query, false);
    vector<string_view> matched_words;
    auto policy = execution::par;

    if (any_of(
        policy,
        query.minus_words.begin(),
        query.minus_words.end(),
        [this, document_id](auto word) {
            return (document_to_word_freqs_.at(document_id).count(word) != 0);
        })) {

        return { {}, documents_.at(document_id).status };
    }

    matched_words.resize(query.plus_words.size());

    auto it = copy_if(
        policy,
        query.plus_words.begin(),
        query.plus_words.end(),
        matched_words.begin(),
        [this, document_id](auto word) {
            return (document_to_word_freqs_.at(document_id).count(word) != 0);
        }
    );

    sort(policy, matched_words.begin(), it);
    it = unique(policy, matched_words.begin(), it);
    matched_words.erase(it, matched_words.end());

    return { matched_words, documents_.at(document_id).status };
}

bool SearchServer::IsStopWord(string_view word) const {
    return stop_words_.count(static_cast<string>(word)) > 0;
}

bool SearchServer::IsValidWord(string_view word) {
    // A valid word must not contain special characters
    return none_of(word.begin(), word.end(), [](char c) {
        return c >= '\0' && c < ' ';
        });
}

vector<string_view> SearchServer::SplitIntoWordsNoStop(string_view text) const {
    vector<string_view> words;
    for (const auto word : SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            throw invalid_argument("Word "s + static_cast<string>(word) + " is invalid"s);
        }
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    return words;
}

int SearchServer::ComputeAverageRating(const vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }
    int rating_sum = accumulate(ratings.begin(), ratings.end(), 0);
    return rating_sum / static_cast<int>(ratings.size());
}

SearchServer::QueryWord SearchServer::ParseQueryWord(string_view text) const {
    if (text.empty()) {
        throw invalid_argument("Query word is empty"s);
    }
    string_view word = text;
    bool is_minus = false;
    if (word[0] == '-') {
        is_minus = true;
        word = word.substr(1);
    }
    if (word.empty() || word[0] == '-' || !IsValidWord(word)) {
        throw invalid_argument("Query word "s + static_cast<string>(text) + " is invalid");
    }

    return { word, is_minus, IsStopWord(word) };
}

SearchServer::Query SearchServer::ParseQuery(string_view text, bool is_seq) const {
    Query result;
    for (const auto word : SplitIntoWords(text)) {
        const auto query_word = ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.push_back(query_word.data);
            }
            else {
                result.plus_words.push_back(query_word.data);
            }
        }
    }

    if (is_seq) {
        sort(result.minus_words.begin(), result.minus_words.end());
        auto new_end_minus = unique(result.minus_words.begin(), result.minus_words.end());
        result.minus_words.erase(new_end_minus, result.minus_words.end());

        sort(result.plus_words.begin(), result.plus_words.end());
        auto new_end_plus = unique(result.plus_words.begin(), result.plus_words.end());
        result.plus_words.erase(new_end_plus, result.plus_words.end());
    }

    return result;
}

// Existence required
double SearchServer::ComputeWordInverseDocumentFreq(string_view word) const {
    return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
}