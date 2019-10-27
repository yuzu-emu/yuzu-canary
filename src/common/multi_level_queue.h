// Copyright 2019 TuxSH
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <iterator>
#include <list>
#include <utility>

#include "common/bit_util.h"
#include "common/common_types.h"

namespace Common {

/**
 * A MultiLevelQueue is a type of priority queue which has the following characteristics:
 * - iteratable through each of its elements.
 * - back can be obtained.
 * - O(1) add, lookup (both front and back)
 * - discrete priorities and a max of 64 priorities (limited domain)
 * This type of priority queue is normaly used for managing threads within an scheduler
 */
template <typename T, std::size_t Depth>
class MultiLevelQueue {
public:
    using value_type = T;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;

    using difference_type = typename std::pointer_traits<pointer>::difference_type;
    using size_type = std::size_t;

    template <bool is_constant>
    class iterator_impl {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = T;
        using pointer = std::conditional_t<is_constant, T*, const T*>;
        using reference = std::conditional_t<is_constant, const T&, T&>;
        using difference_type = typename std::pointer_traits<pointer>::difference_type;

        friend bool operator==(const iterator_impl& lhs, const iterator_impl& rhs) {
            if (lhs.IsEnd() && rhs.IsEnd())
                return true;
            return std::tie(lhs.current_priority, lhs.it) == std::tie(rhs.current_priority, rhs.it);
        }

        friend bool operator!=(const iterator_impl& lhs, const iterator_impl& rhs) {
            return !operator==(lhs, rhs);
        }

        reference operator*() const {
            return *it;
        }

        pointer operator->() const {
            return it.operator->();
        }

        iterator_impl& operator++() {
            if (IsEnd()) {
                return *this;
            }

            ++it;

            if (it == GetEndItForPrio()) {
                u64 prios = mlq.used_priorities;
                prios &= ~((1ULL << (current_priority + 1)) - 1);
                if (prios == 0) {
                    current_priority = static_cast<u32>(mlq.depth());
                } else {
                    current_priority = CountTrailingZeroes64(prios);
                    it = GetBeginItForPrio();
                }
            }
            return *this;
        }

        iterator_impl& operator--() {
            if (IsEnd()) {
                if (mlq.used_priorities != 0) {
                    current_priority = 63 - CountLeadingZeroes64(mlq.used_priorities);
                    it = GetEndItForPrio();
                    --it;
                }
            } else if (it == GetBeginItForPrio()) {
                u64 prios = mlq.used_priorities;
                prios &= (1ULL << current_priority) - 1;
                if (prios != 0) {
                    current_priority = CountTrailingZeroes64(prios);
                    it = GetEndItForPrio();
                    --it;
                }
            } else {
                --it;
            }
            return *this;
        }

        iterator_impl operator++(int) {
            const iterator_impl v{*this};
            ++(*this);
            return v;
        }

        iterator_impl operator--(int) {
            const iterator_impl v{*this};
            --(*this);
            return v;
        }

        // allow implicit const->non-const
        iterator_impl(const iterator_impl<false>& other)
            : mlq(other.mlq), it(other.it), current_priority(other.current_priority) {}

        iterator_impl(const iterator_impl<true>& other)
            : mlq(other.mlq), it(other.it), current_priority(other.current_priority) {}

        iterator_impl& operator=(const iterator_impl<false>& other) {
            mlq = other.mlq;
            it = other.it;
            current_priority = other.current_priority;
            return *this;
        }

        friend class iterator_impl<true>;
        iterator_impl() = default;

    private:
        friend class MultiLevelQueue;
        using container_ref =
            std::conditional_t<is_constant, const MultiLevelQueue&, MultiLevelQueue&>;
        using list_iterator = std::conditional_t<is_constant, typename std::list<T>::const_iterator,
                                                 typename std::list<T>::iterator>;

        explicit iterator_impl(container_ref mlq, list_iterator it, u32 current_priority)
            : mlq(mlq), it(it), current_priority(current_priority) {}
        explicit iterator_impl(container_ref mlq, u32 current_priority)
            : mlq(mlq), it(), current_priority(current_priority) {}

        bool IsEnd() const {
            return current_priority == mlq.depth();
        }

        list_iterator GetBeginItForPrio() const {
            return mlq.levels[current_priority].begin();
        }

        list_iterator GetEndItForPrio() const {
            return mlq.levels[current_priority].end();
        }

        container_ref mlq;
        list_iterator it;
        u32 current_priority;
    };

    using iterator = iterator_impl<false>;
    using const_iterator = iterator_impl<true>;

    void add(const T& element, u32 priority, bool send_back = true) {
        if (send_back)
            levels[priority].push_back(element);
        else
            levels[priority].push_front(element);
        used_priorities |= 1ULL << priority;
    }

    void remove(const T& element, u32 priority) {
        auto it = ListIterateTo(levels[priority], element);
        if (it == levels[priority].end())
            return;
        levels[priority].erase(it);
        if (levels[priority].empty()) {
            used_priorities &= ~(1ULL << priority);
        }
    }

    void adjust(const T& element, u32 old_priority, u32 new_priority, bool adjust_front = false) {
        remove(element, old_priority);
        add(element, new_priority, !adjust_front);
    }
    void adjust(const_iterator it, u32 old_priority, u32 new_priority, bool adjust_front = false) {
        adjust(*it, old_priority, new_priority, adjust_front);
    }

    void transfer_to_front(const T& element, u32 priority, MultiLevelQueue& other) {
        ListSplice(other.levels[priority], other.levels[priority].begin(), levels[priority],
                   ListIterateTo(levels[priority], element));

        other.used_priorities |= 1ULL << priority;

        if (levels[priority].empty()) {
            used_priorities &= ~(1ULL << priority);
        }
    }

    void transfer_to_front(const_iterator it, u32 priority, MultiLevelQueue& other) {
        transfer_to_front(*it, priority, other);
    }

    void transfer_to_back(const T& element, u32 priority, MultiLevelQueue& other) {
        ListSplice(other.levels[priority], other.levels[priority].end(), levels[priority],
                   ListIterateTo(levels[priority], element));

        other.used_priorities |= 1ULL << priority;

        if (levels[priority].empty()) {
            used_priorities &= ~(1ULL << priority);
        }
    }

    void transfer_to_back(const_iterator it, u32 priority, MultiLevelQueue& other) {
        transfer_to_back(*it, priority, other);
    }

    void yield(u32 priority, std::size_t n = 1) {
        ListShiftForward(levels[priority], n);
    }

    std::size_t depth() const {
        return Depth;
    }

    std::size_t size(u32 priority) const {
        return levels[priority].size();
    }

    std::size_t size() const {
        u64 priorities = used_priorities;
        std::size_t size = 0;
        while (priorities != 0) {
            const u64 current_priority = CountTrailingZeroes64(priorities);
            size += levels[current_priority].size();
            priorities &= ~(1ULL << current_priority);
        }
        return size;
    }

    bool empty() const {
        return used_priorities == 0;
    }

    bool empty(u32 priority) const {
        return (used_priorities & (1ULL << priority)) == 0;
    }

    u32 highest_priority_set(u32 max_priority = 0) const {
        const u64 priorities =
            max_priority == 0 ? used_priorities : (used_priorities & ~((1ULL << max_priority) - 1));
        return priorities == 0 ? Depth : static_cast<u32>(CountTrailingZeroes64(priorities));
    }

    u32 lowest_priority_set(u32 min_priority = Depth - 1) const {
        const u64 priorities = min_priority >= Depth - 1
                                   ? used_priorities
                                   : (used_priorities & ((1ULL << (min_priority + 1)) - 1));
        return priorities == 0 ? Depth : 63 - CountLeadingZeroes64(priorities);
    }

    const_iterator cbegin(u32 max_prio = 0) const {
        const u32 priority = highest_priority_set(max_prio);
        return priority == Depth ? cend()
                                 : const_iterator{*this, levels[priority].cbegin(), priority};
    }
    const_iterator begin(u32 max_prio = 0) const {
        return cbegin(max_prio);
    }
    iterator begin(u32 max_prio = 0) {
        const u32 priority = highest_priority_set(max_prio);
        return priority == Depth ? end() : iterator{*this, levels[priority].begin(), priority};
    }

    const_iterator cend(u32 min_prio = Depth - 1) const {
        return min_prio == Depth - 1 ? const_iterator{*this, Depth} : cbegin(min_prio + 1);
    }
    const_iterator end(u32 min_prio = Depth - 1) const {
        return cend(min_prio);
    }
    iterator end(u32 min_prio = Depth - 1) {
        return min_prio == Depth - 1 ? iterator{*this, Depth} : begin(min_prio + 1);
    }

    T& front(u32 max_priority = 0) {
        const u32 priority = highest_priority_set(max_priority);
        return levels[priority == Depth ? 0 : priority].front();
    }
    const T& front(u32 max_priority = 0) const {
        const u32 priority = highest_priority_set(max_priority);
        return levels[priority == Depth ? 0 : priority].front();
    }

    T back(u32 min_priority = Depth - 1) {
        const u32 priority = lowest_priority_set(min_priority); // intended
        return levels[priority == Depth ? 63 : priority].back();
    }
    const T& back(u32 min_priority = Depth - 1) const {
        const u32 priority = lowest_priority_set(min_priority); // intended
        return levels[priority == Depth ? 63 : priority].back();
    }

    void clear() {
        used_priorities = 0;
        for (std::size_t i = 0; i < Depth; i++) {
            levels[i].clear();
        }
    }

private:
    using const_list_iterator = typename std::list<T>::const_iterator;

    static void ListShiftForward(std::list<T>& list, const std::size_t shift = 1) {
        if (shift >= list.size()) {
            return;
        }

        const auto begin_range = list.begin();
        const auto end_range = std::next(begin_range, shift);
        list.splice(list.end(), list, begin_range, end_range);
    }

    static void ListSplice(std::list<T>& in_list, const_list_iterator position,
                           std::list<T>& out_list, const_list_iterator element) {
        in_list.splice(position, out_list, element);
    }

    static const_list_iterator ListIterateTo(const std::list<T>& list, const T& element) {
        auto it = list.cbegin();
        while (it != list.cend() && *it != element) {
            ++it;
        }
        return it;
    }

    std::array<std::list<T>, Depth> levels;
    u64 used_priorities = 0;
};

} // namespace Common
