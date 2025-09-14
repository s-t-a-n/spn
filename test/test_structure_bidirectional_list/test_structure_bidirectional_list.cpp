#include <spine/structure/bidirectional_list.hpp>
#include <unity.h>

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace spn::structure;

namespace {

class Node : public BidirectionalLink<Node> {
public:
    Node(int value = 0) : _value(value) {}

    int value() const { return _value; }

private:
    int _value = 0;
};

// verify bidirectional link pointer integrity
void ut_bidirectional_link_pointers() {
    Node node1(1);
    Node node2(2);
    Node node3(3);

    // link nodes: node1 <-> node2 <-> node3
    node1.attach_next(&node2);
    node2.attach_next(&node3);

    // verify node1 pointers
    TEST_ASSERT_FALSE(node1.has_prev());
    TEST_ASSERT_TRUE(node1.has_next());
    TEST_ASSERT_EQUAL_PTR(nullptr, node1.prev());
    TEST_ASSERT_EQUAL_PTR(&node2, node1.next());

    // verify node2 pointers
    TEST_ASSERT_TRUE(node2.has_prev());
    TEST_ASSERT_TRUE(node2.has_next());
    TEST_ASSERT_EQUAL_PTR(&node1, node2.prev());
    TEST_ASSERT_EQUAL_PTR(&node3, node2.next());

    // verify node3 pointers
    TEST_ASSERT_TRUE(node3.has_prev());
    TEST_ASSERT_FALSE(node3.has_next());
    TEST_ASSERT_EQUAL_PTR(&node2, node3.prev());
    TEST_ASSERT_EQUAL_PTR(nullptr, node3.next());
}

// verify attach_prev / attach_next maintain relationships
void ut_bidirectional_link_attach_prev_next() {
    Node node1(1), node2(2), node3(3), node4(4), node5(5), node0(0);

    // attach node2 to node1 (node1 <-> node2)
    node1.attach_next(&node2);

    // verify pointers after attaching node2
    TEST_ASSERT_EQUAL_PTR(&node2, node1.next());
    TEST_ASSERT_EQUAL_PTR(&node1, node2.prev());

    // attach node3 after node2 (node1 <-> node2 <-> node3)
    node2.attach_next(&node3);

    // verify pointers
    TEST_ASSERT_EQUAL_PTR(&node2, node1.next());
    TEST_ASSERT_EQUAL_PTR(&node3, node2.next());
    TEST_ASSERT_EQUAL_PTR(&node2, node3.prev());
    TEST_ASSERT_EQUAL_PTR(nullptr, node1.prev());
    TEST_ASSERT_EQUAL_PTR(nullptr, node3.next());

    // attach node4 after node3 (node1 <-> node2 <-> node3 <-> node4)
    node3.attach_next(&node4);

    // verify pointers
    TEST_ASSERT_EQUAL_PTR(&node4, node3.next());
    TEST_ASSERT_EQUAL_PTR(&node3, node4.prev());
    TEST_ASSERT_EQUAL_PTR(nullptr, node4.next());

    // attach node5 after node4 (node1 <-> node2 <-> node3 <-> node4 <-> node5)
    node4.attach_next(&node5);

    // verify pointers
    TEST_ASSERT_EQUAL_PTR(&node5, node4.next());
    TEST_ASSERT_EQUAL_PTR(&node4, node5.prev());
    TEST_ASSERT_EQUAL_PTR(nullptr, node5.next());

    // attach node0 before node1 (node0 <-> node1 ...)
    node1.attach_prev(&node0);

    // verify node0 / node1 pointers
    TEST_ASSERT_EQUAL_PTR(&node0, node1.prev());
    TEST_ASSERT_EQUAL_PTR(&node1, node0.next());
    TEST_ASSERT_EQUAL_PTR(nullptr, node0.prev());

    // walk chain forward and verify values
    Node* current = &node0;
    std::vector<int> actual;
    while (current) {
        actual.push_back(current->value());
        current = current->next();
    }
    const std::vector<int> expected = {0, 1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL_INT_ARRAY(expected.data(), actual.data(), expected.size());
}

// verify unlink on head, middle, and tail
void ut_bidirectional_link_unlink() {
    Node node1(1), node2(2), node3(3), node4(4);

    // link nodes: node1 <-> node2 <-> node3 <-> node4
    node1.attach_next(&node2);
    node2.attach_next(&node3);
    node3.attach_next(&node4);

    // unlink head
    node1.unlink();
    TEST_ASSERT_NULL(node1.next());
    TEST_ASSERT_NULL(node1.prev());
    TEST_ASSERT_EQUAL_PTR(&node3, node2.next());
    TEST_ASSERT_EQUAL_PTR(&node2, node3.prev());

    // unlink middle (node3)
    node3.unlink();
    TEST_ASSERT_NULL(node3.next());
    TEST_ASSERT_NULL(node3.prev());
    TEST_ASSERT_EQUAL_PTR(&node4, node2.next());
    TEST_ASSERT_EQUAL_PTR(&node2, node4.prev());

    // unlink tail
    node4.unlink();
    TEST_ASSERT_NULL(node4.next());
    TEST_ASSERT_NULL(node4.prev());
    TEST_ASSERT_EQUAL_PTR(nullptr, node2.next());
}

// verify leaf_back / leaf_front helpers
void ut_bidirectional_link_leaf_accessors() {
    Node node1(1), node2(2), node3(3);

    // link nodes: node1 <-> node2 <-> node3
    node1.attach_next(&node2);
    node2.attach_next(&node3);

    // verify front leaves
    TEST_ASSERT_EQUAL_PTR(&node1, &node1.leaf_front());
    TEST_ASSERT_EQUAL_PTR(&node1, &node2.leaf_front());
    TEST_ASSERT_EQUAL_PTR(&node1, &node3.leaf_front());

    // verify back leaves
    TEST_ASSERT_EQUAL_PTR(&node3, &node1.leaf_back());
    TEST_ASSERT_EQUAL_PTR(&node3, &node2.leaf_back());
    TEST_ASSERT_EQUAL_PTR(&node3, &node3.leaf_back());
}

// verify default-constructed list is empty
void ut_bidirectional_list_initialization() {
    BidirectionalList<Node> list;
    TEST_ASSERT_TRUE(list.empty());
    TEST_ASSERT_EQUAL_size_t(0, list.size());
}

// verify list constructors
void ut_bidirectional_list_constructors() {
    // single-node constructor
    Node root1(10);
    BidirectionalList<Node> list1(root1);
    TEST_ASSERT_FALSE(list1.empty());
    TEST_ASSERT_EQUAL_size_t(1, list1.size());
    TEST_ASSERT_EQUAL_INT(10, list1.front().value());
    TEST_ASSERT_EQUAL_INT(10, list1.back().value());

    // root == leaf
    BidirectionalList<Node> list2(root1, root1);
    TEST_ASSERT_FALSE(list2.empty());
    TEST_ASSERT_EQUAL_size_t(1, list2.size());
    TEST_ASSERT_EQUAL_INT(10, list2.front().value());
    TEST_ASSERT_EQUAL_INT(10, list2.back().value());

    // distinct root / leaf
    Node root2(20), leaf2(30);
    root2.attach_next(&leaf2);
    BidirectionalList<Node> list3(root2, leaf2);
    TEST_ASSERT_FALSE(list3.empty());
    TEST_ASSERT_EQUAL_size_t(2, list3.size());
    TEST_ASSERT_EQUAL_INT(20, list3.front().value());
    TEST_ASSERT_EQUAL_INT(30, list3.back().value());

    // push_back should ignore duplicate link
    list3.push_back(leaf2);
    TEST_ASSERT_EQUAL_size_t(2, list3.size());
    TEST_ASSERT_EQUAL_INT(20, list3.front().value());
    TEST_ASSERT_EQUAL_INT(30, list3.back().value());
}

// verify move constructor
void ut_bidirectional_list_copy_constructor() {
    BidirectionalList<Node> list1;
    Node node1(1), node2(2);
    list1.push_back(node1);
    list1.push_back(node2);

    BidirectionalList<Node> list2 = std::move(list1);
    TEST_ASSERT_EQUAL_size_t(2, list2.size());
    TEST_ASSERT_EQUAL_size_t(0, list1.size());

    auto it = list2.begin();
    TEST_ASSERT_EQUAL_INT(1, it->value());
    ++it;
    TEST_ASSERT_EQUAL_INT(2, it->value());
}

// verify single element insertion / removal
void ut_bidirectional_list_single_insertion() {
    BidirectionalList<Node> list;
    Node node1(1);

    list.push_back(node1);
    TEST_ASSERT_FALSE(list.empty());
    TEST_ASSERT_EQUAL_size_t(1, list.size());
    TEST_ASSERT_EQUAL_INT(1, list.front().value());
    TEST_ASSERT_EQUAL_INT(1, list.back().value());

    list.pop_front();
    TEST_ASSERT_TRUE(list.empty());
    TEST_ASSERT_EQUAL_size_t(0, list.size());
}

void ut_bidirectional_list_multiple_insertions() {
    BidirectionalList<Node> list;
    Node node1(1), node2(2), node3(3);

    // insert at back
    list.push_back(node1);
    list.push_back(node2);
    list.push_back(node3);

    TEST_ASSERT_FALSE(list.empty());
    TEST_ASSERT_EQUAL_size_t(3, list.size());
    TEST_ASSERT_EQUAL_INT(1, list.front().value());
    TEST_ASSERT_EQUAL_INT(3, list.back().value());

    // insert at front
    Node node0(0);
    list.push_front(node0);

    TEST_ASSERT_EQUAL_size_t(4, list.size());
    TEST_ASSERT_EQUAL_INT(0, list.front().value());
    TEST_ASSERT_EQUAL_INT(3, list.back().value());
}

// verify insert_before / insert_after
void ut_bidirectional_list_insert_before_after() {
    BidirectionalList<Node> list;
    Node node1(1), node2(2), node3(3), node4(4);

    // start with node1 -> node3
    list.push_back(node1);
    list.push_back(node3);

    // verify initial size / order
    TEST_ASSERT_EQUAL_size_t(2, list.size());
    {
        auto it = list.begin();
        TEST_ASSERT_EQUAL_INT(1, it->value());
        ++it;
        TEST_ASSERT_EQUAL_INT(3, it->value());
    }

    // insert node2 after node1
    auto it = list.begin();
    it = list.insert_after(it, node2);
    TEST_ASSERT_EQUAL_size_t(3, list.size());

    // verify order node1 -> node2 -> node3
    {
        auto iter = list.begin();
        TEST_ASSERT_EQUAL_INT(1, iter->value());
        ++iter;
        TEST_ASSERT_EQUAL_INT(2, iter->value());
        ++iter;
        TEST_ASSERT_EQUAL_INT(3, iter->value());
    }

    // find node3
    it = list.begin();
    while (it != list.end() && it->value() != 3)
        ++it;
    TEST_ASSERT_TRUE(it != list.end());

    // insert node4 before node3
    it = list.insert_before(it, node4);
    TEST_ASSERT_EQUAL_size_t(4, list.size());

    // verify order node1 -> node2 -> node4 -> node3
    {
        auto iter = list.begin();
        TEST_ASSERT_EQUAL_INT(1, iter->value());
        ++iter;
        TEST_ASSERT_EQUAL_INT(2, iter->value());
        ++iter;
        TEST_ASSERT_EQUAL_INT(4, iter->value());
        ++iter;
        TEST_ASSERT_EQUAL_INT(3, iter->value());
    }

    // verify back returns node3
    TEST_ASSERT_EQUAL_INT(3, list.back().value());

    {
        const std::vector<int> expected = {1, 2, 4, 3};
        std::vector<int> actual;
        for (auto iter = list.begin(); iter != list.end(); ++iter)
            actual.push_back(iter->value());

        for (size_t i = 0; i < expected.size(); ++i)
            TEST_ASSERT_EQUAL_INT(expected[i], actual[i]);
    }
}

// verify erase on head, middle, and tail
void ut_bidirectional_link_erase() {
    BidirectionalList<Node> list;
    Node node1(1), node2(2), node3(3), node4(4);

    // insert nodes: node1 <-> node2 <-> node3 <-> node4
    list.push_back(node1);
    list.push_back(node2);
    list.push_back(node3);
    list.push_back(node4);

    // erase head
    list.erase(list.begin());
    TEST_ASSERT_EQUAL_size_t(3, list.size());
    TEST_ASSERT_EQUAL_INT(2, list.front().value());

    // verify order node2 <-> node3 <-> node4
    {
        auto iter = list.begin();
        TEST_ASSERT_EQUAL_INT(2, iter->value());
        ++iter;
        TEST_ASSERT_EQUAL_INT(3, iter->value());
        ++iter;
        TEST_ASSERT_EQUAL_INT(4, iter->value());
    }

    // erase middle (node3)
    auto it = list.begin();
    ++it; // node3
    list.erase(it);
    TEST_ASSERT_EQUAL_size_t(2, list.size());

    // verify order node2 <-> node4
    {
        auto iter = list.begin();
        TEST_ASSERT_EQUAL_INT(2, iter->value());
        ++iter;
        TEST_ASSERT_EQUAL_INT(4, iter->value());
    }

    // verify reverse order node4 <-> node2
    {
        auto iter = list.rbegin();
        TEST_ASSERT_EQUAL_INT(4, iter->value());
        ++iter;
        TEST_ASSERT_EQUAL_INT(2, iter->value());
    }

    // erase tail
    list.erase(&list.back());
    TEST_ASSERT_EQUAL_size_t(1, list.size());
    TEST_ASSERT_EQUAL_INT(2, list.front().value());
    TEST_ASSERT_EQUAL_INT(2, list.back().value());
}

// verify pop_front / pop_back / erase
void ut_bidirectional_list_removals() {
    BidirectionalList<Node> list;
    Node node1(1), node2(2), node3(3), node4(4);

    list.push_back(node1);
    list.push_back(node2);
    list.push_back(node3);
    list.push_back(node4);

    // pop_front
    list.pop_front();
    TEST_ASSERT_EQUAL_size_t(3, list.size());
    TEST_ASSERT_EQUAL_INT(2, list.front().value());

    // pop_back
    list.pop_back();
    TEST_ASSERT_EQUAL_size_t(2, list.size());
    TEST_ASSERT_EQUAL_INT(2, list.front().value());
    TEST_ASSERT_EQUAL_INT(3, list.back().value());

    // erase middle (node2)
    auto it = list.begin();
    it = list.erase(it);
    TEST_ASSERT_EQUAL_size_t(1, list.size());
    TEST_ASSERT_EQUAL_INT(3, list.front().value());
    TEST_ASSERT_EQUAL_INT(3, list.back().value());
}

// verify forward / reverse iteration
void ut_bidirectional_list_iteration() {
    BidirectionalList<Node> list;
    Node node1(1), node2(2), node3(3);

    list.push_back(node1);
    list.push_back(node2);
    list.push_back(node3);

    int expected_fwd[] = {1, 2, 3};
    int idx = 0;
    for (auto it = list.begin(); it != list.end(); ++it, ++idx)
        TEST_ASSERT_EQUAL_INT(expected_fwd[idx], it->value());
    TEST_ASSERT_EQUAL_INT(3, idx);

    int expected_rev[] = {3, 2, 1};
    idx = 0;
    for (auto it = list.rbegin(); it != list.rend(); ++it, ++idx)
        TEST_ASSERT_EQUAL_INT(expected_rev[idx], it->value());
    TEST_ASSERT_EQUAL_INT(3, idx);
}

// verify reverse iterators collect correct values
void ut_bidirectional_list_reverse_iterators() {
    BidirectionalList<Node> list;
    Node node1(1), node2(2), node3(3);

    list.push_back(node1);
    list.push_back(node2);
    list.push_back(node3);

    const std::vector<int> expected = {3, 2, 1};
    std::vector<int> actual;
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        actual.push_back(it->value());
    TEST_ASSERT_EQUAL_INT_ARRAY(expected.data(), actual.data(), expected.size());
}

// verify depth helpers
void ut_bidirectional_list_depth_calculations() {
    BidirectionalList<Node> list;
    Node node1(1), node2(2), node3(3), node4(4);

    list.push_back(node1);
    list.push_back(node2);
    list.push_back(node3);
    list.push_back(node4);

    TEST_ASSERT_EQUAL_size_t(4, list.size());
    TEST_ASSERT_EQUAL_size_t(4, list.front().depth_to_back());
    TEST_ASSERT_EQUAL_size_t(4, list.back().depth_to_front());
    TEST_ASSERT_EQUAL_size_t(3, node2.depth_to_back());
    TEST_ASSERT_EQUAL_size_t(3, node3.depth_to_front());
}

// verify front / back helpers
void ut_bidirectional_list_front_back() {
    BidirectionalList<Node> list;
    Node node1(1), node2(2), node3(3), node4(4);

    // push_back nodes
    list.push_back(node1);
    list.push_back(node2);
    list.push_back(node3);
    list.push_back(node4);

    TEST_ASSERT_EQUAL_INT(1, list.front().value());
    TEST_ASSERT_EQUAL_INT(4, list.back().value());

    // pop_front
    list.pop_front();
    TEST_ASSERT_EQUAL_INT(2, list.front().value());
    TEST_ASSERT_EQUAL_INT(4, list.back().value());

    // pop_back
    list.pop_back();
    TEST_ASSERT_EQUAL_INT(2, list.front().value());
    TEST_ASSERT_EQUAL_INT(3, list.back().value());
}

// verify duplicate insert ignored
void ut_bidirectional_list_edge_cases() {
    BidirectionalList<Node> list;
    Node node1(1);

    list.push_back(node1);
    TEST_ASSERT_EQUAL_size_t(1, list.size());

    // attempt duplicate insert
    list.push_back(node1);
    TEST_ASSERT_EQUAL_size_t(1, list.size());
    TEST_ASSERT_EQUAL_INT(1, list.front().value());
    TEST_ASSERT_EQUAL_INT(1, list.back().value());

    list.pop_back();
    TEST_ASSERT_TRUE(list.empty());
}

// verify erase returns next iterator
void ut_bidirectional_list_erase_return_value() {
    BidirectionalList<Node> list;
    Node a(1), b(2), c(3);
    list.push_back(a);
    list.push_back(b);
    list.push_back(c);

    auto next = list.erase(++list.begin()); // remove b
    TEST_ASSERT_EQUAL_PTR(&c, &*next);
}

// verify move assignment
void ut_bidirectional_list_move_assignment() {
    BidirectionalList<Node> src;
    Node a(1);
    src.push_back(a);

    BidirectionalList<Node> dst;
    dst = std::move(src);

    TEST_ASSERT_TRUE(src.empty());
    TEST_ASSERT_FALSE(dst.empty());
    TEST_ASSERT_EQUAL_INT(1, dst.front().value());
}

// verify external unlink of tail updates list
void ut_bidirectional_list_external_unlink_tail() {
    BidirectionalList<Node> list;
    Node a(1), b(2);
    list.push_back(a);
    list.push_back(b);

    b.unlink(); // external detach tail

    TEST_ASSERT_EQUAL_PTR(&a, &list.front());
    TEST_ASSERT_EQUAL_PTR(&a, &list.back());

    size_t cnt = 0;
    for (auto& n : list)
        ++cnt;
    TEST_ASSERT_EQUAL_size_t(1, cnt);
}

// verify const iteration compile-time behavior
void ut_bidirectional_list_const_iteration() {
    BidirectionalList<Node> list;
    Node a(1);
    list.push_back(a);

    const auto& clist = list;
    for (const auto& n : clist)
        TEST_ASSERT_EQUAL_INT(1, n.value());
}

// verify iterator navigation on empty list
void ut_bidirectional_list_iterators_empty_list_navigation() {
    BidirectionalList<Node> l;
    auto fwd = l.end(); // --end on empty
    --fwd;
    TEST_ASSERT_TRUE(fwd == l.end());

    auto rev = l.rend(); // ++rend on empty
    ++rev;
    TEST_ASSERT_TRUE(rev == l.rend());
}

// verify iterator / pointer comparators
void ut_bidirectional_list_pointer_comparators() {
    BidirectionalList<Node> l;
    Node a(1);
    l.push_back(a);

    TEST_ASSERT_TRUE(l.begin() == &a);
    TEST_ASSERT_TRUE(&a == l.begin());
    TEST_ASSERT_FALSE(l.begin() != &a);
}

// verify erase return on head / tail
void ut_bidirectional_list_erase_return_head_tail() {
    BidirectionalList<Node> l;
    Node a(1);
    l.push_back(a);

    auto it = l.erase(l.begin());
    TEST_ASSERT_TRUE(it == l.end());
    TEST_ASSERT_TRUE(l.empty());
}

// verify second unlink is safe
void ut_bidirectional_list_double_unlink_head_and_tail() {
    Node h(1), t(2);
    h.attach_next(&t);
    h.unlink(); // unlink head
    h.unlink(); // second unlink must be safe
    TEST_ASSERT_NULL(h.next());
    TEST_ASSERT_EQUAL_PTR(&t, &t.leaf_front());
}

// verify ensure_detached moves node between lists
void ut_bidirectional_list_cross_list_detach() {
    BidirectionalList<Node> l1, l2;
    Node a(1);
    l1.push_back(a);
    l2.push_back(a); // ensure_detached removes from l1

    TEST_ASSERT_TRUE(l1.empty());
    TEST_ASSERT_FALSE(l2.empty());
}

// verify move assign over existing contents
void ut_bidirectional_list_move_assign_over_existing() {
    BidirectionalList<Node> src;
    Node a(1), b(2);
    src.push_back(a);
    src.push_back(b);

    BidirectionalList<Node> dst;
    Node x(9);
    dst.push_back(x); // dst not empty

    dst = std::move(src);
    TEST_ASSERT_EQUAL_size_t(2, dst.size());
    TEST_ASSERT_TRUE(src.empty());
    TEST_ASSERT_EQUAL_INT(1, dst.front().value());
    TEST_ASSERT_EQUAL_INT(2, dst.back().value());
}

// verify erasure doesnt interfere with iteration
void ut_bidirectional_list_iterator_erase_while_iterating() {
    BidirectionalList<Node> l;
    Node a(1), b(2), c(3);
    l.push_back(a);
    l.push_back(b);
    l.push_back(c);

    size_t n = 0;
    for (auto it = l.begin(); it != l.end(); /* erase returns next */)
        it = l.erase(it), ++n;

    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_TRUE(l.empty());
}

// verify forward iteration properly terminates
void ut_bidirectional_list_iterator_forward_terminates() {
    BidirectionalList<Node> l;
    Node a(1), b(2), c(3);
    l.push_back(a);
    l.push_back(b);
    l.push_back(c);

    size_t steps = 0;
    for (auto it = l.begin(); it != l.end(); ++it) {
        ++steps;
        if (steps > 10) TEST_FAIL_MESSAGE("forward iteration never reached end()");
    }
    TEST_ASSERT_EQUAL_size_t(3, steps);
}

// verify reverse iteration properly terminates
void ut_bidirectional_list_iterator_reverse_terminates() {
    BidirectionalList<Node> l;
    Node a(1), b(2), c(3);
    l.push_back(a);
    l.push_back(b);
    l.push_back(c);

    size_t steps = 0;
    for (auto it = l.rbegin(); it != l.rend(); ++it) {
        ++steps;
        if (steps > 10) TEST_FAIL_MESSAGE("reverse iteration never reached rend()");
    }
    TEST_ASSERT_EQUAL_size_t(3, steps);
}

// verify no endless loop is created when wrongly over increasing iterator
void ut_bidirectional_list_increment_end_no_reentry() {
    BidirectionalList<Node> l;
    Node a(1);
    l.push_back(a);

    auto it = l.end();
    for (int i = 0; i < 3; ++i)
        ++it; // must stay at end()
    TEST_ASSERT_TRUE(it == l.end());
}

// verify no endless loop is created when wrongly over decreasing iterator
void ut_bidirectional_list_decrement_begin_no_reentry() {
    BidirectionalList<Node> l;
    Node a(1);
    l.push_back(a);

    auto it = l.begin();
    for (int i = 0; i < 3; ++i)
        --it;
    TEST_ASSERT_TRUE(it == l.begin());
}

// verify external unlinking of head doesnt break list
void ut_bidirectional_list_external_unlink_head_and_middle() {
    BidirectionalList<Node> l;
    Node a(1), b(2), c(3);
    l.push_back(a);
    l.push_back(b);
    l.push_back(c);

    a.unlink(); // unlink original head
    TEST_ASSERT_EQUAL_size_t(2, l.size());
    TEST_ASSERT_EQUAL_PTR(&b, &l.front());
    TEST_ASSERT_EQUAL_PTR(&c, &l.back());

    b.unlink(); // unlink new head / middle
    TEST_ASSERT_EQUAL_size_t(1, l.size());
    TEST_ASSERT_EQUAL_PTR(&c, &l.front());
    TEST_ASSERT_EQUAL_PTR(&c, &l.back());
}

// verify linking and relinking to different list works properly
void ut_bidirectional_list_cross_list_detach_middle() {
    BidirectionalList<Node> l1, l2;
    Node a(1), b(2), c(3);
    l1.push_back(a);
    l1.push_back(b);
    l1.push_back(c);

    l2.push_back(b); // ensure_detached must remove from l1

    TEST_ASSERT_EQUAL_size_t(2, l1.size());
    TEST_ASSERT_EQUAL_size_t(1, l2.size());
    TEST_ASSERT_EQUAL_PTR(&a, &l1.front());
    TEST_ASSERT_EQUAL_PTR(&c, &l1.back());
    TEST_ASSERT_EQUAL_PTR(&b, &l2.front());
}

// verify inserting at leafs behaves like push front/back
void ut_bidirectional_list_insert_before_end_after_rend() {
    BidirectionalList<Node> l;
    Node a(1), b(2), c(3);

    l.push_back(a);
    l.insert_before(l.end(), b); // should behave like push_back
    l.insert_after(l.rend(), c); // should behave like push_front

    TEST_ASSERT_EQUAL_size_t(3, l.size());
    TEST_ASSERT_EQUAL_INT(3, l.front().value());
    TEST_ASSERT_EQUAL_INT(2, l.back().value());
}

// verify ends of trees are tied off at destruction
void ut_bidirectional_list_untouched_underlying() {
    static constexpr auto MAX = 10;
    BidirectionalLink<Node> nodes[MAX];

    TEST_ASSERT_EQUAL(false, nodes[0].has_prev());
    TEST_ASSERT_EQUAL(false, nodes[MAX - 1].has_next());

    {
        BidirectionalList<Node> l{};
        size_t i = 0;
        for (auto& node : nodes) {
            l.push_back(node);
            TEST_ASSERT_EQUAL(true, &node == &nodes[i++]);
        }
        l.detach_self();
    }

    // first node has no predecessor
    TEST_ASSERT_EQUAL(true, nodes[0].has_next());
    TEST_ASSERT_EQUAL(false, nodes[0].has_prev());

    // last node has no ancestor
    TEST_ASSERT_EQUAL(true, nodes[MAX - 1].has_prev());
    TEST_ASSERT_EQUAL(false, nodes[MAX - 1].has_next());
}

} // namespace

int run_all_tests() {
    UNITY_BEGIN();

    RUN_TEST(ut_bidirectional_link_pointers);
    RUN_TEST(ut_bidirectional_link_attach_prev_next);
    RUN_TEST(ut_bidirectional_link_unlink);
    RUN_TEST(ut_bidirectional_link_leaf_accessors);
    RUN_TEST(ut_bidirectional_list_initialization);
    RUN_TEST(ut_bidirectional_list_constructors);
    RUN_TEST(ut_bidirectional_list_copy_constructor);
    RUN_TEST(ut_bidirectional_list_single_insertion);
    RUN_TEST(ut_bidirectional_list_multiple_insertions);
    RUN_TEST(ut_bidirectional_list_insert_before_after);
    RUN_TEST(ut_bidirectional_link_erase);
    RUN_TEST(ut_bidirectional_list_removals);
    RUN_TEST(ut_bidirectional_list_iteration);
    RUN_TEST(ut_bidirectional_list_reverse_iterators);
    RUN_TEST(ut_bidirectional_list_depth_calculations);
    RUN_TEST(ut_bidirectional_list_front_back);
    RUN_TEST(ut_bidirectional_list_edge_cases);
    RUN_TEST(ut_bidirectional_list_erase_return_value);
    RUN_TEST(ut_bidirectional_list_move_assignment);
    RUN_TEST(ut_bidirectional_list_external_unlink_tail);
    RUN_TEST(ut_bidirectional_list_const_iteration);
    RUN_TEST(ut_bidirectional_list_iterators_empty_list_navigation);
    RUN_TEST(ut_bidirectional_list_pointer_comparators);
    RUN_TEST(ut_bidirectional_list_erase_return_head_tail);
    RUN_TEST(ut_bidirectional_list_double_unlink_head_and_tail);
    RUN_TEST(ut_bidirectional_list_cross_list_detach);
    RUN_TEST(ut_bidirectional_list_move_assign_over_existing);
    RUN_TEST(ut_bidirectional_list_iterator_erase_while_iterating);
    RUN_TEST(ut_bidirectional_list_iterator_forward_terminates);
    RUN_TEST(ut_bidirectional_list_iterator_reverse_terminates);
    RUN_TEST(ut_bidirectional_list_increment_end_no_reentry);
    RUN_TEST(ut_bidirectional_list_decrement_begin_no_reentry);
    RUN_TEST(ut_bidirectional_list_external_unlink_head_and_middle);
    RUN_TEST(ut_bidirectional_list_cross_list_detach_middle);
    RUN_TEST(ut_bidirectional_list_insert_before_end_after_rend);
    RUN_TEST(ut_bidirectional_list_untouched_underlying);

    return UNITY_END();
}

#if defined(ARDUINO)
#    include <Arduino.h>
void setup() {
    // wait >2 s if board lacks DTR/RTS reset
    delay(2000);
    run_all_tests();
}

void loop() {}
#else
int main(int, char**) { return run_all_tests(); }
#endif
