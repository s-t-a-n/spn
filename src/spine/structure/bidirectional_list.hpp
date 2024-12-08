#pragma once

#include <spine/core/debugging.hpp>

#include <cstddef>
#include <type_traits>

namespace spn::structure {

template<typename T>
/// A link to be inherited from for use with a BidirectionalList
class BidirectionalLink {
public:
    BidirectionalLink() = default;
    BidirectionalLink(const BidirectionalLink&) = delete;
    BidirectionalLink& operator=(const BidirectionalLink&) = delete;
    ~BidirectionalLink() { unlink(); }

    /// Returns total length of nodes on the left of this node until and including the leaf node or `term`
    [[nodiscard]] constexpr std::size_t depth_to_back(BidirectionalLink* term = nullptr) const {
        std::size_t distance = 1;
        for (auto* node = next(); node; node = node->next(), ++distance)
            if (node == term) break;
        return distance;
    }

    /// Returns total length of nodes on the right of this node until and including the leaf node or `term`
    [[nodiscard]] constexpr std::size_t depth_to_front(BidirectionalLink* term = nullptr) const {
        std::size_t distance = 1;
        for (auto* node = prev(); node && node != term; node = node->prev(), ++distance) {
        }
        return distance;
    }

    /// Returns the leaf node all the way on the left
    T& leaf_front() {
        auto* node = this;
        while (node->prev())
            node = node->prev();
        return static_cast<T&>(*node);
    }

    /// Returns the leaf node all the way on the right
    T& leaf_back() {
        auto* node = this;
        while (node->next())
            node = node->next();
        return static_cast<T&>(*node);
    }

    constexpr T* prev() const { return (_prev && !_prev->_is_sentinel) ? static_cast<T*>(_prev) : nullptr; }
    constexpr T* next() const { return (_next && !_next->_is_sentinel) ? static_cast<T*>(_next) : nullptr; }

    bool has_prev() const { return prev(); }
    bool has_next() const { return next(); }

    /// Inserts this node before the `anchor` node
    void insert_before(BidirectionalLink& anchor) {
        spn_assert(&anchor != this);
        link_between(anchor._prev, &anchor);
    }

    /// Inserts this node after the `anchor` node
    void insert_after(BidirectionalLink& anchor) {
        spn_assert(&anchor != this);
        link_between(&anchor, anchor._next);
    }

    /// Detach this node it's neighbours
    void unlink() {
        if (_prev) _prev->_next = _next;
        if (_next) _next->_prev = _prev;
        _prev = _next = nullptr;
    }

    /// Attach `node` to the left of current node
    void attach_prev(BidirectionalLink* node) noexcept {
        if (!node || node == this) return;
        node->link_between(_prev, this);
    }

    /// Attach `node` to the right of current node
    void attach_next(BidirectionalLink* node) noexcept {
        if (!node || node == this) return;
        node->link_between(this, _next);
    }

    template<typename>
    friend class BidirectionalList;

private:
    void link_between(BidirectionalLink* left, BidirectionalLink* right) noexcept {
        _prev = left;
        _next = right;
        if (left) left->_next = this;
        if (right) right->_prev = this;
    }

    BidirectionalLink* _prev{nullptr};
    BidirectionalLink* _next{nullptr};
    bool _is_sentinel{false};

    struct SentinelTag {};
    explicit BidirectionalLink(SentinelTag) : _is_sentinel(true) {}
};

template<typename T>
/** A list for bidirectional traversal of nodes that may dynamically unlink.
 * Note: Since nodes may dynamically unlink, getting the size() of the list is always O(N)
 * Note: This list is designed for ordering nodes, not for preserving their lifetime.
 **/
class BidirectionalList {
    static_assert(std::is_base_of_v<BidirectionalLink<T>, T>, "T must inherit from BidirectionalLink<T>");

    using Node = BidirectionalLink<T>;

public:
    class iterator {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        iterator() = default;

        reference operator*() const noexcept { return *static_cast<T*>(_node); }
        pointer operator->() const noexcept { return static_cast<T*>(_node); }

        iterator& operator++() noexcept {
            if (_node != _sentinel) _node = _node->_next;
            return *this;
        }
        iterator operator++(int) noexcept {
            auto tmp = *this;
            ++(*this);
            return tmp;
        }

        iterator& operator--() noexcept {
            if (_node == _sentinel) {
                _node = _sentinel->_prev;
            } else if (_node->_prev != _sentinel) {
                _node = _node->_prev;
            }
            return *this;
        }
        iterator operator--(int) noexcept {
            auto tmp = *this;
            --(*this);
            return tmp;
        }

        friend bool operator==(const iterator& a, const iterator& b) noexcept { return a._node == b._node; }
        friend bool operator!=(const iterator& a, const iterator& b) noexcept { return !(a == b); }

        friend bool operator==(const iterator& it, const T* p) noexcept { return it._node == p; }
        friend bool operator==(const T* p, const iterator& it) noexcept { return p == it._node; }
        friend bool operator!=(const iterator& it, const T* p) noexcept { return it._node != p; }
        friend bool operator!=(const T* p, const iterator& it) noexcept { return p != it._node; }

    private:
        friend class BidirectionalList;

        iterator(Node* n, Node* s) : _node(n), _sentinel(s) {}

        Node* _node{nullptr};
        Node* _sentinel{nullptr};
    };

    class reverse_iterator {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        reverse_iterator() = default;
        explicit reverse_iterator(const iterator& it) : _sentinel(it._sentinel) {
            if (it._node == _sentinel) _node = (_sentinel->_prev == _sentinel) ? nullptr : _sentinel->_prev;
            else if (it._node->_prev == _sentinel)
                _node = nullptr;
            else
                _node = it._node->_prev;
        }

        iterator base() const noexcept { return iterator(_node ? _node->_next : _sentinel->_next, _sentinel); }

        reference operator*() const noexcept {
            spn_assert(_node);
            return *static_cast<T*>(_node);
        }
        pointer operator->() const noexcept {
            spn_assert(_node);
            return static_cast<T*>(_node);
        }

        reverse_iterator& operator++() noexcept {
            if (_node) _node = (_node->_prev == _sentinel) ? nullptr : _node->_prev;
            return *this;
        }
        reverse_iterator operator++(int) noexcept {
            auto tmp = *this;
            ++(*this);
            return tmp;
        }

        reverse_iterator& operator--() noexcept {
            _node = _node ? _node->_next : _sentinel->_prev;
            return *this;
        }
        reverse_iterator operator--(int) noexcept {
            auto tmp = *this;
            --(*this);
            return tmp;
        }

        friend bool operator==(const reverse_iterator& a, const reverse_iterator& b) noexcept {
            return a._node == b._node;
        }
        friend bool operator!=(const reverse_iterator& a, const reverse_iterator& b) noexcept { return !(a == b); }

    private:
        friend class BidirectionalList;

        reverse_iterator(Node* n, Node* s) : _node(n), _sentinel(s) {}

        Node* _node{nullptr};
        Node* _sentinel{nullptr};
    };

    using const_iterator = iterator;
    using const_reverse_iterator = reverse_iterator;

    BidirectionalList() noexcept { clear(); }

    template<typename U>
    explicit BidirectionalList(U& root) noexcept {
        clear();
        init_from_chain(&root.leaf_front(), &root.leaf_back());
    }

    template<typename U, typename V>
    BidirectionalList(U& head, V& tail) noexcept {
        clear();
        init_from_chain(&head, &tail);
    }

    BidirectionalList(const BidirectionalList&) = delete;
    BidirectionalList& operator=(const BidirectionalList&) = delete;

    BidirectionalList(BidirectionalList&& other) noexcept { move_from(other); }
    BidirectionalList& operator=(BidirectionalList&& other) noexcept {
        if (this != &other) {
            clear();
            move_from(other);
        }
        return *this;
    }

    ~BidirectionalList() = default;

    iterator begin() noexcept { return {_sentinel._next, &_sentinel}; }
    iterator end() noexcept { return {&_sentinel, &_sentinel}; }
    const_iterator begin() const noexcept { return {_sentinel._next, const_cast<Node*>(&_sentinel)}; }
    const_iterator end() const noexcept { return {const_cast<Node*>(&_sentinel), const_cast<Node*>(&_sentinel)}; }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }

    reverse_iterator rbegin() noexcept { return {_sentinel._prev, &_sentinel}; }
    reverse_iterator rend() noexcept { return {nullptr, &_sentinel}; }
    const_reverse_iterator rbegin() const noexcept { return {_sentinel._prev, const_cast<Node*>(&_sentinel)}; }
    const_reverse_iterator rend() const noexcept { return {nullptr, const_cast<Node*>(&_sentinel)}; }
    const_reverse_iterator crbegin() const noexcept { return rbegin(); }
    const_reverse_iterator crend() const noexcept { return rend(); }

    bool empty() const noexcept { return _sentinel._next == &_sentinel; }
    size_t size() const noexcept {
        size_t count = 0;
        for (auto* p = _sentinel._next; p != &_sentinel; p = p->_next)
            ++count;
        return count;
    }

    T& front() noexcept { return *static_cast<T*>(_sentinel._next); }
    T& back() noexcept { return *static_cast<T*>(_sentinel._prev); }
    const T& front() const noexcept { return *static_cast<const T*>(_sentinel._next); }
    const T& back() const noexcept { return *static_cast<const T*>(_sentinel._prev); }

    void clear() noexcept { _sentinel._next = _sentinel._prev = &_sentinel; }

    void push_front(Node& n) noexcept {
        ensure_detached(n);
        insert_after(_sentinel, n);
    }
    void push_back(Node& n) noexcept {
        ensure_detached(n);
        insert_before(_sentinel, n);
    }

    void pop_front() noexcept {
        spn_assert(!empty());
        erase(*_sentinel._next);
    }
    void pop_back() noexcept {
        spn_assert(!empty());
        erase(*_sentinel._prev);
    }

    iterator insert_before(const iterator& pos, Node& n) noexcept {
        ensure_detached(n);
        insert_before(*pos._node, n);
        return {&n, &_sentinel};
    }
    iterator insert_before(const reverse_iterator& rpos, Node& n) noexcept {
        const_iterator fwd = rpos.base();
        if (fwd == end()) {
            push_back(n);
            return iterator(_sentinel._prev, &_sentinel);
        }
        --fwd;
        return insert_after(fwd, n);
    }

    iterator insert_after(const iterator& pos, Node& n) noexcept {
        ensure_detached(n);
        insert_after(*pos._node, n);
        return {&n, &_sentinel};
    }
    iterator insert_after(const reverse_iterator& rpos, Node& n) noexcept {
        const_iterator fwd = rpos.base();
        if (fwd == begin()) {
            push_front(n);
            return begin();
        }
        return insert_before(fwd, n);
    }

    iterator erase(const iterator& it) noexcept {
        Node* current = it._node;
        Node* next = current->_next;
        current->unlink();
        return {next, &_sentinel};
    }
    iterator erase(Node& n) noexcept { return erase(iterator(&n, &_sentinel)); }
    iterator erase(Node* n) noexcept { return erase(iterator(n, &_sentinel)); }

private:
    static void ensure_detached(Node& n) noexcept {
        if (n._prev || n._next) n.unlink();
    }

    static void insert_after(Node& anchor, Node& node) noexcept {
        node._prev = &anchor;
        node._next = anchor._next;
        anchor._next->_prev = &node;
        anchor._next = &node;
    }
    static void insert_before(Node& anchor, Node& node) noexcept { insert_after(*anchor._prev, node); }

    void init_from_chain(Node* head, Node* tail) noexcept {
        _sentinel._next = head;
        _sentinel._prev = tail;
        head->_prev = &_sentinel;
        tail->_next = &_sentinel;
    }

    void move_from(BidirectionalList& other) noexcept {
        if (other.empty()) {
            clear();
            return;
        }
        _sentinel._next = other._sentinel._next;
        _sentinel._prev = other._sentinel._prev;
        _sentinel._next->_prev = &_sentinel;
        _sentinel._prev->_next = &_sentinel;
        other.clear();
    }

    Node _sentinel{typename Node::SentinelTag{}};
};

} // namespace spn::structure
