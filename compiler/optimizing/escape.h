/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef ART_COMPILER_OPTIMIZING_ESCAPE_H_
#define ART_COMPILER_OPTIMIZING_ESCAPE_H_

namespace art {

class HInstruction;

/*
 * Methods related to escape analysis, i.e. determining whether an object
 * allocation is visible outside ('escapes') its immediate method context.
 */

// A visitor for seeing all instructions escape analysis considers escaping.
// Called with each user of the reference passed to 'VisitEscapes'. Return true
// to continue iteration and false to stop.
class EscapeVisitor {
 public:
  virtual ~EscapeVisitor() {}
  virtual bool Visit(HInstruction* escape) = 0;
  bool operator()(HInstruction* user) {
    return Visit(user);
  }
};

// An explicit EscapeVisitor for lambdas
template <typename F>
class LambdaEscapeVisitor final : public EscapeVisitor {
 public:
  explicit LambdaEscapeVisitor(F f) : func_(f) {}
  bool Visit(HInstruction* escape) override {
    return func_(escape);
  }

 private:
  F func_;
};

// This functor is used with the escape-checking functions. If the NoEscape
// function returns true escape analysis will consider 'user' to not have
// escaped 'reference'. This allows clients with additional information to
// supplement the escape-analysis. If the NoEscape function returns false then
// the normal escape-checking code will be used to determine whether or not
// 'reference' escapes.
class NoEscapeCheck {
 public:
  virtual ~NoEscapeCheck() {}
  virtual bool NoEscape(HInstruction* reference, HInstruction* user) = 0;
  bool operator()(HInstruction* ref, HInstruction* user) {
    return NoEscape(ref, user);
  }
};

// An explicit NoEscapeCheck for use with c++ lambdas.
template <typename F>
class LambdaNoEscapeCheck final : public NoEscapeCheck {
 public:
  explicit LambdaNoEscapeCheck(F f) : func_(f) {}
  bool NoEscape(HInstruction* ref, HInstruction* user) override {
    return func_(ref, user);
  }

 private:
  F func_;
};

/*
 * Performs escape analysis on the given instruction, typically a reference to an
 * allocation. The method assigns true to parameter 'is_singleton' if the reference
 * is the only name that can refer to its value during the lifetime of the method,
 * meaning that the reference is not aliased with something else, is not stored to
 * heap memory, and not passed to another method. In addition, the method assigns
 * true to parameter 'is_singleton_and_not_returned' if the reference is a singleton
 * and not returned to the caller and to parameter 'is_singleton_and_not_deopt_visible'
 * if the reference is a singleton and not used as an environment local of an
 * HDeoptimize instruction (clients of the final value must run after BCE to ensure
 * all such instructions have been introduced already).
 *
 * Note that being visible to a HDeoptimize instruction does not count for ordinary
 * escape analysis, since switching between compiled code and interpreted code keeps
 * non escaping references restricted to the lifetime of the method and the thread
 * executing it. This property only concerns optimizations that are interested in
 * escape analysis with respect to the *compiled* code (such as LSE).
 *
 * When set, the no_escape function is applied to any use of the allocation instruction
 * prior to any built-in escape analysis. This allows clients to define better escape
 * analysis in certain case-specific circumstances. If 'no_escape(reference, user)'
 * returns true, the user is assumed *not* to cause any escape right away. The return
 * value false means the client cannot provide a definite answer and built-in escape
 * analysis is applied to the user instead.
 */
void CalculateEscape(HInstruction* reference,
                     NoEscapeCheck& no_escape,
                     /*out*/ bool* is_singleton,
                     /*out*/ bool* is_singleton_and_not_returned,
                     /*out*/ bool* is_singleton_and_not_deopt_visible);

inline void CalculateEscape(HInstruction* reference,
                            bool (*no_escape_fn)(HInstruction*, HInstruction*),
                            /*out*/ bool* is_singleton,
                            /*out*/ bool* is_singleton_and_not_returned,
                            /*out*/ bool* is_singleton_and_not_deopt_visible) {
  LambdaNoEscapeCheck esc(no_escape_fn);
  LambdaNoEscapeCheck noop_esc([](HInstruction*, HInstruction*) { return false; });
  CalculateEscape(reference,
                  no_escape_fn == nullptr ? static_cast<NoEscapeCheck&>(noop_esc) : esc,
                  is_singleton,
                  is_singleton_and_not_returned,
                  is_singleton_and_not_deopt_visible);
}

/*
 * Performs escape analysis and visits each escape of the reference. Does not try to calculate any
 * overall information about the method. Escapes are calculated in the same way as CalculateEscape.
 *
 * The escape_visitor should return true to continue visiting, false otherwise.
 */
void VisitEscapes(HInstruction* reference, EscapeVisitor& escape_visitor);

/*
 * Convenience method for testing the singleton and not returned properties at once.
 * Callers should be aware that this method invokes the full analysis at each call.
 */
bool DoesNotEscape(HInstruction* reference, NoEscapeCheck& no_escape);

inline bool DoesNotEscape(HInstruction* reference,
                          bool (*no_escape_fn)(HInstruction*, HInstruction*)) {
  LambdaNoEscapeCheck<typeof(no_escape_fn)> esc(no_escape_fn);
  return DoesNotEscape(reference, esc);
}

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_ESCAPE_H_
