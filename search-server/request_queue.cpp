#include "request_queue.h"

RequestQueue::RequestQueue(const SearchServer& search_server)
    :search_server_(search_server) {}

vector<Document> RequestQueue::AddFindRequest(const string& raw_query, DocumentStatus status) {
    auto result = search_server_.FindTopDocuments(raw_query, status);
    CheckRequests(result, raw_query);
    return result;
}
vector<Document> RequestQueue::AddFindRequest(const string& raw_query) {
    auto result = search_server_.FindTopDocuments(raw_query);
    CheckRequests(result, raw_query);
    return result;
}
int RequestQueue::GetNoResultRequests() const {
    return empty_requests_;
}

void RequestQueue::CheckRequests(vector<Document> result, const string& raw_query) {
    requests_.push_back({ raw_query, !result.empty() });

    if (!requests_.back().is_result) {
        empty_requests_++;
    }

    if (requests_.size() - 1 == min_in_day_) {
        if (!requests_.front().is_result)
            empty_requests_--;
        requests_.pop_front();
    }
}
