#ifndef PPSTEP_UTILS_HPP
#define PPSTEP_UTILS_HPP

#include <algorithm>
#include <utility>
#include <vector>
#include <optional>

namespace ppstep {

    template <class Iterator, class Printer, class Delimiter>
    std::ostream& print_with_delimiter(std::ostream& os, Iterator& it, Iterator end, Printer printer, Delimiter const& delimiter) {
        if (it == end) return os;

        printer(os, *it++);
        for (; it != end; ++it) {
            os << delimiter;
            printer(os, *it);
        }
        return os;
    }

    template <class Token>
    std::ostream& print_token(std::ostream& os, Token const& token) {
        os << token.get_value().c_str();
        return os;
    }
    
    template <class Iterator>
    std::ostream& print_token_range(std::ostream& os, Iterator& it, Iterator end) {
        return print_with_delimiter(os, it, std::move(end), [](auto& os, auto const& token) { print_token(os, token); }, ' ');
    }

    template <class Container>
    std::ostream& print_token_container(std::ostream& os, Container const& data) {
        auto it = std::begin(data);
        return print_token_range(os, it, std::end(data));
    }

    template <class Container, class T>
    auto join_lists(Container const& lists, T const& separator) {
        auto acc = std::vector<T>();

        auto it = std::begin(lists);
        {
            auto const& list = *it++;
            acc.insert(std::end(acc), std::begin(list), std::end(list));
        }

        auto end = std::end(lists);
        for (; it != end; ++it) {
            acc.push_back(separator);
            acc.insert(std::end(acc), std::begin(*it), std::end(*it));
        }

        return acc;
    }

    template <class Container>
    std::optional<std::pair<typename Container::const_iterator, typename Container::const_iterator>>
    find_sublist(Container const& data, Container const& pattern, typename Container::const_iterator it) {
        auto end = std::end(data);
        auto match = std::search(it, end, std::begin(pattern), std::end(pattern));
        if (match != end) {
            return {{match, std::next(match, pattern.size())}};
        } else {
            return {};
        }
    }
}

#endif // PPSTEP_UTILS_HPP