/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "message_queue.h"

#include <thread>

#include "common_runtime_test.h"
#include "thread-current-inl.h"

namespace art {

class MessageQueueTest : public CommonRuntimeTest {};

namespace {

// Define some message types
struct EmptyMessage {};
struct IntMessage {
  int value;
};
struct OtherIntMessage {
  int other_value;
};
struct TwoIntMessage {
  int value1;
  int value2;
};
struct StringMessage {
  std::string message;
};

using TestMessageQueue =
    MessageQueue<EmptyMessage, IntMessage, OtherIntMessage, TwoIntMessage, StringMessage>;

}  // namespace

TEST_F(MessageQueueTest, SendReceiveTest) {
  TestMessageQueue queue;

  queue.SendMessage(EmptyMessage{});
  ASSERT_TRUE(std::holds_alternative<EmptyMessage>(queue.ReceiveMessage()));

  queue.SendMessage(IntMessage{42});
  ASSERT_TRUE(std::holds_alternative<IntMessage>(queue.ReceiveMessage()));

  queue.SendMessage(OtherIntMessage{43});
  ASSERT_TRUE(std::holds_alternative<OtherIntMessage>(queue.ReceiveMessage()));

  queue.SendMessage(TwoIntMessage{1, 2});
  ASSERT_TRUE(std::holds_alternative<TwoIntMessage>(queue.ReceiveMessage()));

  queue.SendMessage(StringMessage{"Hello, World!"});
  ASSERT_TRUE(std::holds_alternative<StringMessage>(queue.ReceiveMessage()));
}

TEST_F(MessageQueueTest, TestTimeout) {
  TestMessageQueue queue;

  constexpr uint64_t kDuration = 500;

  const auto start = MilliTime();
  queue.SetTimeout(kDuration);
  ASSERT_TRUE(std::holds_alternative<TimeoutExpiredMessage>(queue.ReceiveMessage()));
  const auto elapsed = MilliTime() - start;

  ASSERT_GT(elapsed, kDuration);
}

TEST_F(MessageQueueTest, TwoWayMessaging) {
  TestMessageQueue queue1;
  TestMessageQueue queue2;

  std::thread thread{[&]() {
    // Tell the parent thread we are running.
    queue1.SendMessage(EmptyMessage{});

    // Wait for a message from the parent thread.
    queue2.ReceiveMessage();
  }};

  queue1.ReceiveMessage();
  queue2.SendMessage(EmptyMessage{});

  thread.join();
}

TEST_F(MessageQueueTest, SwitchReceiveTest) {
  TestMessageQueue queue;

  queue.SendMessage(EmptyMessage{});
  queue.SendMessage(IntMessage{42});
  queue.SendMessage(OtherIntMessage{43});
  queue.SendMessage(TwoIntMessage{1, 2});
  queue.SendMessage(StringMessage{"Hello, World!"});
  queue.SetTimeout(500);

  bool empty_received = false;
  bool int_received = false;
  bool other_int_received = false;
  bool two_int_received = false;
  bool string_received = false;
  bool timeout_received = false;

  while (!(empty_received && int_received && other_int_received && two_int_received &&
           string_received && timeout_received)) {
    queue.SwitchReceive(
        [&]([[maybe_unused]] const EmptyMessage& message) {
          ASSERT_FALSE(empty_received);
          empty_received = true;
        },
        [&](const IntMessage& message) {
          ASSERT_FALSE(int_received);
          int_received = true;

          ASSERT_EQ(message.value, 42);
        },
        [&](const OtherIntMessage& message) {
          ASSERT_FALSE(other_int_received);
          other_int_received = true;

          ASSERT_EQ(message.other_value, 43);
        },
        // The timeout message is here to make sure the cases can go in any order
        [&]([[maybe_unused]] const TimeoutExpiredMessage& message) {
          ASSERT_FALSE(timeout_received);
          timeout_received = true;
        },
        [&](const TwoIntMessage& message) {
          ASSERT_FALSE(two_int_received);
          two_int_received = true;

          ASSERT_EQ(message.value1, 1);
          ASSERT_EQ(message.value2, 2);
        },
        [&](const StringMessage& message) {
          ASSERT_FALSE(string_received);
          string_received = true;

          ASSERT_EQ(message.message, "Hello, World!");
        });
  }
}

TEST_F(MessageQueueTest, SwitchReceiveAutoTest) {
  TestMessageQueue queue;

  queue.SendMessage(EmptyMessage{});
  queue.SendMessage(IntMessage{42});
  queue.SendMessage(OtherIntMessage{43});
  queue.SendMessage(TwoIntMessage{1, 2});
  queue.SendMessage(StringMessage{"Hello, World!"});
  queue.SetTimeout(500);

  int pending_messages = 6;

  while (pending_messages > 0) {
    queue.SwitchReceive([&]([[maybe_unused]] auto message) { pending_messages--; });
  }
}

TEST_F(MessageQueueTest, SwitchReceivePartialAutoTest) {
  TestMessageQueue queue;

  queue.SendMessage(EmptyMessage{});
  queue.SendMessage(IntMessage{42});
  queue.SendMessage(OtherIntMessage{43});
  queue.SendMessage(TwoIntMessage{1, 2});
  queue.SendMessage(StringMessage{"Hello, World!"});
  queue.SetTimeout(500);

  bool running = true;
  while (running) {
    queue.SwitchReceive(
        [&](const StringMessage& message) {
          ASSERT_EQ(message.message, "Hello, World!");
          running = false;
        },
        [&]([[maybe_unused]] const auto& message) {
          const bool is_string{std::is_same<StringMessage, decltype(message)>()};
          ASSERT_FALSE(is_string);
        });
  }
}

TEST_F(MessageQueueTest, SwitchReceiveReturn) {
  TestMessageQueue queue;

  queue.SendMessage(EmptyMessage{});

  ASSERT_TRUE(
      queue.SwitchReceive<bool>([&]([[maybe_unused]] const EmptyMessage& message) { return true; },
                                [&]([[maybe_unused]] const auto& message) { return false; }));

  queue.SendMessage(IntMessage{42});

  ASSERT_FALSE(
      queue.SwitchReceive<bool>([&]([[maybe_unused]] const EmptyMessage& message) { return true; },
                                [&]([[maybe_unused]] const auto& message) { return false; }));
}

TEST_F(MessageQueueTest, ReceiveInOrder) {
  TestMessageQueue queue;

  std::vector<TestMessageQueue::Message> messages{
      EmptyMessage{},
      IntMessage{42},
      OtherIntMessage{43},
      TwoIntMessage{1, 2},
      StringMessage{"Hello, World!"},
  };

  // Send the messages
  for (const auto& message : messages) {
    queue.SendMessage(message);
  }
  queue.SetTimeout(500);

  // Receive the messages. Make sure they came in order, except for the TimeoutExpiredMessage, which
  // can come at any time.
  bool received_timeout = false;
  size_t i = 0;
  while (i < messages.size()) {
    auto message = queue.ReceiveMessage();
    if (std::holds_alternative<TimeoutExpiredMessage>(message)) {
      ASSERT_FALSE(received_timeout);
      received_timeout = true;
    } else {
      ASSERT_EQ(message.index(), messages[i].index());
      i++;
    }
  }
  if (!received_timeout) {
    // If we have not received the timeout yet, receive one more message and make sure it's the
    // timeout.
    ASSERT_TRUE(std::holds_alternative<TimeoutExpiredMessage>(queue.ReceiveMessage()));
  }
}

}  // namespace art
