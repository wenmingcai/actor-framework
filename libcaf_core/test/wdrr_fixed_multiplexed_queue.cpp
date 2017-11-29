/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2017                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#define CAF_SUITE wdrr_fixed_multiplexed_queue

#include <memory>

#include "caf/test/unit_test.hpp"

#include "caf/intrusive/drr_queue.hpp"
#include "caf/intrusive/singly_linked.hpp"
#include "caf/intrusive/wdrr_fixed_multiplexed_queue.hpp"

using namespace caf;
using namespace caf::intrusive;

namespace {

struct inode : singly_linked<inode> {
  int value;
  inode(int x = 0) : value(x) {
    // nop
  }
};

std::string to_string(const inode& x) {
  return std::to_string(x.value);
}

class high_prio_queue;

struct inode_policy {
  using mapped_type = inode;

  using task_size_type = int;

  using deficit_type = int;

  using deleter_type = std::default_delete<mapped_type>;

  using unique_pointer = std::unique_ptr<mapped_type, deleter_type>;

  static inline task_size_type task_size(const mapped_type&) {
    return 1;
  }

  static inline size_t id_of(const inode& x) {
    return x.value % 3;
  }

  template <class Queue>
  deficit_type quantum(const Queue&, deficit_type x) {
    return x;
  }

  deficit_type quantum(const high_prio_queue&, deficit_type x) {
    return enable_priorities ? 2 * x : x;
  }

  bool enable_priorities = false;
};

class high_prio_queue : public drr_queue<inode_policy> {
public:
  using super = drr_queue<inode_policy>;

  using super::super;
};

using nested_queue_type = drr_queue<inode_policy>;

using queue_type = wdrr_fixed_multiplexed_queue<inode_policy,
                                                high_prio_queue,
                                                nested_queue_type,
                                                nested_queue_type>;

struct fixture {
  inode_policy policy;
  queue_type queue{policy, policy, policy, policy};

  void fill(queue_type&) {
    // nop
  }

  template <class T, class... Ts>
  void fill(queue_type& q, T x, Ts... xs) {
    q.emplace_back(x);
    fill(q, xs...);
  }

  std::string seq;

  std::function<void (inode&)> f;

  fixture() {
    f = [&](inode& x) {
      if (!seq.empty())
        seq += ',';
      seq += to_string(x);
    };
  }
};

} // namespace <anonymous>

CAF_TEST_FIXTURE_SCOPE(wdrr_fixed_multiplexed_queue_tests, fixture)

CAF_TEST(default_constructed) {
  CAF_REQUIRE_EQUAL(queue.empty(), true);
}

CAF_TEST(new_round) {
  fill(queue, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12);
  // Allow f to consume 2 items per nested queue.
  auto round_result = queue.new_round(2, f);
  CAF_CHECK_EQUAL(round_result, true);
  CAF_CHECK_EQUAL(seq, "3,6,1,4,2,5");
  CAF_REQUIRE_EQUAL(queue.empty(), false);
  // Allow f to consume one more item from each queue.
  seq.clear();
  round_result = queue.new_round(1, f);
  CAF_CHECK_EQUAL(round_result, true);
  CAF_CHECK_EQUAL(seq, "9,7,8");
  CAF_REQUIRE_EQUAL(queue.empty(), false);
  // Allow f to consume the remainder, i.e., 12.
  seq.clear();
  round_result = queue.new_round(1000, f);
  CAF_CHECK_EQUAL(round_result, true);
  CAF_CHECK_EQUAL(seq, "12");
  CAF_REQUIRE_EQUAL(queue.empty(), true);
}

CAF_TEST(priorities) {
  queue.policy().enable_priorities = true;
  fill(queue, 1, 2, 3, 4, 5, 6, 7, 8, 9);
  // Allow f to consume 2 items from the high priority and 1 item otherwise.
  auto round_result = queue.new_round(1, f);
  CAF_CHECK_EQUAL(round_result, true);
  CAF_CHECK_EQUAL(seq, "3,6,1,2");
  CAF_REQUIRE_EQUAL(queue.empty(), false);
  // Drain the high-priority queue with one item left per other queue.
  seq.clear();
  round_result = queue.new_round(1, f);
  CAF_CHECK_EQUAL(round_result, true);
  CAF_CHECK_EQUAL(seq, "9,4,5");
  CAF_REQUIRE_EQUAL(queue.empty(), false);
  // Drain queue.
  seq.clear();
  round_result = queue.new_round(1000, f);
  CAF_CHECK_EQUAL(round_result, true);
  CAF_CHECK_EQUAL(seq, "7,8");
  CAF_REQUIRE_EQUAL(queue.empty(), true);
}

CAF_TEST_FIXTURE_SCOPE_END()