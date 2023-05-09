
#include "process_queries.h"
#include <execution>

std::vector<std::vector<Document>> ProcessQueries(const SearchServer& search_server, const std::vector<std::string>& queries)
{
    vector<std::vector<Document>> result(queries.size());
    transform(execution::par, queries.begin(), queries.end(), result.begin(),
        [&search_server](string query) {return search_server.FindTopDocuments(query); });
    return result;
}

std::list<Document> ProcessQueriesJoined(const SearchServer& search_server, const std::vector<std::string>& queries)
{
    vector<std::vector<Document>> result_find_documents(queries.size());
    list<Document> result;
    transform(execution::par, queries.begin(), queries.end(), result_find_documents.begin(),
        [&search_server](string query) {return search_server.FindTopDocuments(query); });
    for (auto const &documents : result_find_documents) {
        for (auto const &document : documents) {
            result.push_back(document);
        }
    }
    return result;
}
