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

#ifndef ART_RUNTIME_BASE_MESSAGE_QUEUE_H_
#define ART_RUNTIME_BASE_MESSAGE_QUEUE_H_

#include <deque>
#include <optional>
#include <variant>

#include "base/time_utils.h"
#include "mutex.h"
#include "thread.h"

#pragma clang diagnostic push
#pragma clang diagnostic error "-Wconversion"

namespace art {

struct TimeoutExpiredMessage {};

// MessageQueue is an unbounded multiple producer, multiple consumer (MPMC) queue that can be
// specialized to send messages between threads. The queue is parameterized by a set of types that
// serve as the message types. Note that messages are passed by value, so smaller messages should be
// used when possible.
//
// Example:
//
//     struct IntMessage { int value; };
//     struct DoubleMessage { double value; };
//
//     MessageQueue<IntMessage, DoubleMessage> queue;
//
//     queue.SendMessage(IntMessage{42});
//     queue.SendMessage(DoubleMessage{42.0});
//
//     auto message = queue.ReceiveMessage();  // message is a std::variant of the different
//                                             // message types.
//
//     if (std::holds_alternative<IntMessage>(message)) {
//       cout << "Received int message with value " << std::get<IntMessage>(message) << "\n";
//     }
//
// The message queue also supports a special timeout message. This is scheduled to be sent by the
// SetTimeout method, which will cause the MessageQueue to deliver a TimeoutExpiredMessage after the
// time period has elapsed. Note that only one timeout can be active can be active at a time, and
// subsequent calls to SetTimeout will overwrite any existing timeout.
//
// Example:
//
//     queue.SetTimeout(5000);  // request to send TimeoutExpiredMessage in 5000ms.
//
//     auto message = queue.ReceiveMessage();  // blocks for 5000ms and returns
//                                             // TimeoutExpiredMessage
//
// Note additional messages can be sent in the meantime and a ReceiveMessage call will wake up to
// return that message. The TimeoutExpiredMessage will still be sent at the right time.
//
// Finally, MessageQueue has a SwitchReceive method that can be used to run different code depending
// on the type of message received. SwitchReceive takes a set of lambda expressions that take one
// argument of one of the allowed message types. An additional lambda expression that takes a single
// auto argument can be used to serve as a catch-all case.
//
// Example:
//
//     queue.SwitchReceive(
//       [&](IntMessage message) {
//         cout << "Received int: " << message.value << "\n";
//       },
//       [&](DoubleMessage message) {
//         cout << "Received double: " << message.value << "\n";
//       },
//       [&](auto other_message) {
//         // Another message was received. In this case, it's TimeoutExpiredMessage.
//       }
//     )
//
// For additional examples, see message_queue_test.cc.
template <typename... MessageTypes>
class MessageQueue {
 public:
  using Message = std::variant<TimeoutExpiredMessage, MessageTypes...>;

  // Adds a message to the message queue, which can later be received with ReceiveMessage. See class
  // comment for more details.
  void SendMessage(Message message) {
    // TimeoutExpiredMessage should not be sent manually.
    DCHECK(!std::holds_alternative<TimeoutExpiredMessage>(message));
    Thread* self = Thread::Current();
    MutexLock lock{self, mutex_};
    messages_.push_back(message);
    cv_.Signal(self);
  }

  // Schedule a TimeoutExpiredMessage to be delivered in timeout_milliseconds. See class comment for
  // more details.
  void SetTimeout(uint64_t timeout_milliseconds) {
    Thread* self = Thread::Current();
    MutexLock lock{self, mutex_};
    deadline_milliseconds_ = timeout_milliseconds + MilliTime();
    cv_.Signal(self);
  }

  // Remove and return a message from the queue. If no message is available, ReceiveMessage will
  // block until one becomes available. See class comment for more details.
  Message ReceiveMessage() {
    Thread* self = Thread::Current();
    MutexLock lock{self, mutex_};

    // Loop until we receive a message
    while (true) {
      uint64_t const current_time = MilliTime();
      // First check if the deadline has passed.
      if (deadline_milliseconds_.has_value() && deadline_milliseconds_.value() < current_time) {
        deadline_milliseconds_.reset();
        return TimeoutExpiredMessage{};
      }

      // Check if there is a message in the queue.
      if (messages_.size() > 0) {
        Message message = messages_.front();
        messages_.pop_front();
        return message;
      }

      // Otherwise, wait until we have a message or a timeout.
      if (deadline_milliseconds_.has_value()) {
        DCHECK_LE(current_time, deadline_milliseconds_.value());
        int64_t timeout = static_cast<int64_t>(deadline_milliseconds_.value() - current_time);
        cv_.TimedWait(self, timeout, /*ns=*/0);
      } else {
        cv_.Wait(self);
      }
    }
  }

  // Waits for a message and applies the appropriate function argument to the received message. See
  // class comment for more details.
  template <typename ReturnType = void, typename... Fn>
  ReturnType SwitchReceive(Fn... case_fn) {
    struct Matcher : Fn... {
      using Fn::operator()...;
    } matcher{case_fn...};
    return std::visit(matcher, ReceiveMessage());
  }

 private:
  Mutex mutex_{"MessageQueue Mutex"};
  ConditionVariable cv_{"MessageQueue ConditionVariable", mutex_};

  std::deque<Message> messages_ GUARDED_BY(mutex_);
  std::optional<uint64_t> deadline_milliseconds_ GUARDED_BY(mutex_);
};

}  // namespace art

#pragma clang diagnostic pop  // -Wconversion

#endif  // ART_RUNTIME_BASE_MESSAGE_QUEUE_H_
