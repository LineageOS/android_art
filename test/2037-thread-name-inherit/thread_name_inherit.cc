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

#include <jni.h>

#include <iostream>

#include "android-base/logging.h"

struct ThreadArgs {
  JavaVM* jvm;
  jobject consumer;
  JavaVMAttachArgs* attach_args;
  bool set_in_java;
};

// The main method of the test thread. The ThreadArgs controls what this does.
void* ThreadMain(void* arg) {
  ThreadArgs* args = reinterpret_cast<ThreadArgs*>(arg);
  JNIEnv* env = nullptr;
  pthread_t self = pthread_self();

  int err = pthread_setname_np(self, "native-thread");
  CHECK_EQ(err, 0);

  args->jvm->AttachCurrentThread(&env, args->attach_args);

  jclass thread_class = env->FindClass("java/lang/Thread");
  jclass consumer_class = env->FindClass("java/util/function/BiConsumer");
  jmethodID current_thread_method =
      env->GetStaticMethodID(thread_class, "currentThread", "()Ljava/lang/Thread;");
  jmethodID accept_method =
      env->GetMethodID(consumer_class, "accept", "(Ljava/lang/Object;Ljava/lang/Object;)V");
  jobject current_thread = env->CallStaticObjectMethod(thread_class, current_thread_method);
  if (args->set_in_java) {
    jmethodID set_name_method = env->GetMethodID(thread_class, "setName", "(Ljava/lang/String;)V");
    jobject str_name = env->NewStringUTF("native-thread-set-java");
    env->CallVoidMethod(current_thread, set_name_method, str_name);
  }

  char name_chars[1024];
  err = pthread_getname_np(self, name_chars, sizeof(name_chars));
  CHECK_EQ(err, 0);
  jobject str_name = env->NewStringUTF(name_chars);

  env->CallVoidMethod(args->consumer, accept_method, str_name, current_thread);

  args->jvm->DetachCurrentThread();

  return nullptr;
}

extern "C" JNIEXPORT void Java_Main_runThreadTestWithName(JNIEnv* env,
                                                          jclass /*clazz*/,
                                                          jobject consumer) {
  jobject global_consumer = env->NewGlobalRef(consumer);
  JavaVMAttachArgs args;
  args.group = nullptr;
  args.name = "java-native-thread";
  args.version = JNI_VERSION_1_6;
  ThreadArgs ta {
    .jvm = nullptr, .consumer = global_consumer, .attach_args = &args, .set_in_java = false
  };
  env->GetJavaVM(&ta.jvm);
  pthread_t child;
  pthread_create(&child, nullptr, ThreadMain, &ta);
  void* ret;
  pthread_join(child, &ret);
  env->DeleteGlobalRef(ta.consumer);
}

extern "C" JNIEXPORT void Java_Main_runThreadTest(JNIEnv* env, jclass /*clazz*/, jobject consumer) {
  jobject global_consumer = env->NewGlobalRef(consumer);
  ThreadArgs ta {
    .jvm = nullptr, .consumer = global_consumer, .attach_args = nullptr, .set_in_java = false
  };
  env->GetJavaVM(&ta.jvm);
  pthread_t child;
  pthread_create(&child, nullptr, ThreadMain, &ta);
  void* ret;
  pthread_join(child, &ret);
  env->DeleteGlobalRef(ta.consumer);
}

extern "C" JNIEXPORT void Java_Main_runThreadTestSetJava(JNIEnv* env,
                                                         jclass /*clazz*/,
                                                         jobject consumer) {
  jobject global_consumer = env->NewGlobalRef(consumer);
  ThreadArgs ta {
    .jvm = nullptr, .consumer = global_consumer, .attach_args = nullptr, .set_in_java = true
  };
  env->GetJavaVM(&ta.jvm);
  pthread_t child;
  pthread_create(&child, nullptr, ThreadMain, &ta);
  void* ret;
  pthread_join(child, &ret);
  env->DeleteGlobalRef(ta.consumer);
}
