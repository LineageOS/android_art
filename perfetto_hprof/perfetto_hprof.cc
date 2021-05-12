/*
 * Copyright (C) 2019 The Android Open Source Project
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

#define LOG_TAG "perfetto_hprof"

#include "perfetto_hprof.h"

#include <android-base/logging.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <time.h>

#include <limits>
#include <optional>
#include <type_traits>

#include "gc/heap-visit-objects-inl.h"
#include "gc/heap.h"
#include "gc/scoped_gc_critical_section.h"
#include "mirror/object-refvisitor-inl.h"
#include "nativehelper/scoped_local_ref.h"
#include "perfetto/profiling/normalize.h"
#include "perfetto/profiling/parse_smaps.h"
#include "perfetto/trace/interned_data/interned_data.pbzero.h"
#include "perfetto/trace/profiling/heap_graph.pbzero.h"
#include "perfetto/trace/profiling/profile_common.pbzero.h"
#include "perfetto/trace/profiling/smaps.pbzero.h"
#include "perfetto/config/profiling/java_hprof_config.pbzero.h"
#include "perfetto/protozero/packed_repeated_fields.h"
#include "perfetto/tracing.h"
#include "runtime-inl.h"
#include "runtime_callbacks.h"
#include "scoped_thread_state_change-inl.h"
#include "thread_list.h"
#include "well_known_classes.h"
#include "dex/descriptors_names.h"

// There are three threads involved in this:
// * listener thread: this is idle in the background when this plugin gets loaded, and waits
//   for data on on g_signal_pipe_fds.
// * signal thread: an arbitrary thread that handles the signal and writes data to
//   g_signal_pipe_fds.
// * perfetto producer thread: once the signal is received, the app forks. In the newly forked
//   child, the Perfetto Client API spawns a thread to communicate with traced.

namespace perfetto_hprof {

constexpr int kJavaHeapprofdSignal = __SIGRTMIN + 6;
constexpr time_t kWatchdogTimeoutSec = 120;
// This needs to be lower than the maximum acceptable chunk size, because this
// is checked *before* writing another submessage. We conservatively assume
// submessages can be up to 100k here for a 500k chunk size.
// DropBox has a 500k chunk limit, and each chunk needs to parse as a proto.
constexpr uint32_t kPacketSizeThreshold = 400000;
constexpr char kByte[1] = {'x'};
static art::Mutex& GetStateMutex() {
  static art::Mutex state_mutex("perfetto_hprof_state_mutex", art::LockLevel::kGenericBottomLock);
  return state_mutex;
}

static art::ConditionVariable& GetStateCV() {
  static art::ConditionVariable state_cv("perfetto_hprof_state_cv", GetStateMutex());
  return state_cv;
}

static int requested_tracing_session_id = 0;
static State g_state = State::kUninitialized;

// Pipe to signal from the signal handler into a worker thread that handles the
// dump requests.
int g_signal_pipe_fds[2];
static struct sigaction g_orig_act = {};

template <typename T>
uint64_t FindOrAppend(std::map<T, uint64_t>* m, const T& s) {
  auto it = m->find(s);
  if (it == m->end()) {
    std::tie(it, std::ignore) = m->emplace(s, m->size());
  }
  return it->second;
}

void ArmWatchdogOrDie() {
  timer_t timerid{};
  struct sigevent sev {};
  sev.sigev_notify = SIGEV_SIGNAL;
  sev.sigev_signo = SIGKILL;

  if (timer_create(CLOCK_MONOTONIC, &sev, &timerid) == -1) {
    // This only gets called in the child, so we can fatal without impacting
    // the app.
    PLOG(FATAL) << "failed to create watchdog timer";
  }

  struct itimerspec its {};
  its.it_value.tv_sec = kWatchdogTimeoutSec;

  if (timer_settime(timerid, 0, &its, nullptr) == -1) {
    // This only gets called in the child, so we can fatal without impacting
    // the app.
    PLOG(FATAL) << "failed to arm watchdog timer";
  }
}

bool StartsWith(const std::string& str, const std::string& prefix) {
  return str.compare(0, prefix.length(), prefix) == 0;
}

// Sample entries that match one of the following
// start with /system/
// start with /vendor/
// start with /data/app/
// contains "extracted in memory from Y", where Y matches any of the above
bool ShouldSampleSmapsEntry(const perfetto::profiling::SmapsEntry& e) {
  if (StartsWith(e.pathname, "/system/") || StartsWith(e.pathname, "/vendor/") ||
      StartsWith(e.pathname, "/data/app/")) {
    return true;
  }
  if (StartsWith(e.pathname, "[anon:")) {
    if (e.pathname.find("extracted in memory from /system/") != std::string::npos) {
      return true;
    }
    if (e.pathname.find("extracted in memory from /vendor/") != std::string::npos) {
      return true;
    }
    if (e.pathname.find("extracted in memory from /data/app/") != std::string::npos) {
      return true;
    }
  }
  return false;
}

constexpr size_t kMaxCmdlineSize = 512;

class JavaHprofDataSource : public perfetto::DataSource<JavaHprofDataSource> {
 public:
  constexpr static perfetto::BufferExhaustedPolicy kBufferExhaustedPolicy =
    perfetto::BufferExhaustedPolicy::kStall;
  void OnSetup(const SetupArgs& args) override {
    uint64_t normalized_cfg_tracing_session_id =
      args.config->tracing_session_id() % std::numeric_limits<int32_t>::max();
    if (requested_tracing_session_id < 0) {
      LOG(ERROR) << "invalid requested tracing session id " << requested_tracing_session_id;
      return;
    }
    if (static_cast<uint64_t>(requested_tracing_session_id) != normalized_cfg_tracing_session_id) {
      return;
    }

    // This is on the heap as it triggers -Wframe-larger-than.
    std::unique_ptr<perfetto::protos::pbzero::JavaHprofConfig::Decoder> cfg(
        new perfetto::protos::pbzero::JavaHprofConfig::Decoder(
          args.config->java_hprof_config_raw()));

    dump_smaps_ = cfg->dump_smaps();
    for (auto it = cfg->ignored_types(); it; ++it) {
      std::string name = (*it).ToStdString();
      ignored_types_.emplace_back(std::move(name));
    }

    uint64_t self_pid = static_cast<uint64_t>(getpid());
    for (auto pid_it = cfg->pid(); pid_it; ++pid_it) {
      if (*pid_it == self_pid) {
        enabled_ = true;
        return;
      }
    }

    if (cfg->has_process_cmdline()) {
      int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
      if (fd == -1) {
        PLOG(ERROR) << "failed to open /proc/self/cmdline";
        return;
      }
      char cmdline[kMaxCmdlineSize];
      ssize_t rd = read(fd, cmdline, sizeof(cmdline) - 1);
      if (rd == -1) {
        PLOG(ERROR) << "failed to read /proc/self/cmdline";
      }
      close(fd);
      if (rd == -1) {
        return;
      }
      cmdline[rd] = '\0';
      char* cmdline_ptr = cmdline;
      ssize_t sz = perfetto::profiling::NormalizeCmdLine(&cmdline_ptr, static_cast<size_t>(rd + 1));
      if (sz == -1) {
        PLOG(ERROR) << "failed to normalize cmdline";
      }
      for (auto it = cfg->process_cmdline(); it; ++it) {
        std::string other = (*it).ToStdString();
        // Append \0 to make this a C string.
        other.resize(other.size() + 1);
        char* other_ptr = &(other[0]);
        ssize_t other_sz = perfetto::profiling::NormalizeCmdLine(&other_ptr, other.size());
        if (other_sz == -1) {
          PLOG(ERROR) << "failed to normalize other cmdline";
          continue;
        }
        if (sz == other_sz && strncmp(cmdline_ptr, other_ptr, static_cast<size_t>(sz)) == 0) {
          enabled_ = true;
          return;
        }
      }
    }
  }

  bool dump_smaps() { return dump_smaps_; }
  bool enabled() { return enabled_; }

  void OnStart(const StartArgs&) override {
    if (!enabled()) {
      return;
    }
    art::MutexLock lk(art_thread(), GetStateMutex());
    if (g_state == State::kWaitForStart) {
      g_state = State::kStart;
      GetStateCV().Broadcast(art_thread());
    }
  }

  // This datasource can be used with a trace config with a short duration_ms
  // but a long datasource_stop_timeout_ms. In that case, OnStop is called (in
  // general) before the dump is done. In that case, we handle the stop
  // asynchronously, and notify the tracing service once we are done.
  // In case OnStop is called after the dump is done (but before the process)
  // has exited, we just acknowledge the request.
  void OnStop(const StopArgs& a) override {
    art::MutexLock lk(art_thread(), finish_mutex_);
    if (is_finished_) {
      return;
    }
    is_stopped_ = true;
    async_stop_ = std::move(a.HandleStopAsynchronously());
  }

  static art::Thread* art_thread() {
    // TODO(fmayer): Attach the Perfetto producer thread to ART and give it a name. This is
    // not trivial, we cannot just attach the first time this method is called, because
    // AttachCurrentThread deadlocks with the ConditionVariable::Wait in WaitForDataSource.
    //
    // We should attach the thread as soon as the Client API spawns it, but that needs more
    // complicated plumbing.
    return nullptr;
  }

  std::vector<std::string> ignored_types() { return ignored_types_; }

  void Finish() {
    art::MutexLock lk(art_thread(), finish_mutex_);
    if (is_stopped_) {
      async_stop_();
    } else {
      is_finished_ = true;
    }
  }

 private:
  bool enabled_ = false;
  bool dump_smaps_ = false;
  std::vector<std::string> ignored_types_;
  static art::Thread* self_;

  art::Mutex finish_mutex_{"perfetto_hprof_ds_mutex", art::LockLevel::kGenericBottomLock};
  bool is_finished_ = false;
  bool is_stopped_ = false;
  std::function<void()> async_stop_;
};

art::Thread* JavaHprofDataSource::self_ = nullptr;


void WaitForDataSource(art::Thread* self) {
  perfetto::TracingInitArgs args;
  args.backends = perfetto::BackendType::kSystemBackend;
  perfetto::Tracing::Initialize(args);

  perfetto::DataSourceDescriptor dsd;
  dsd.set_name("android.java_hprof");
  dsd.set_will_notify_on_stop(true);
  JavaHprofDataSource::Register(dsd);

  LOG(INFO) << "waiting for data source";

  art::MutexLock lk(self, GetStateMutex());
  while (g_state != State::kStart) {
    GetStateCV().Wait(self);
  }
}

class Writer {
 public:
  Writer(pid_t parent_pid, JavaHprofDataSource::TraceContext* ctx, uint64_t timestamp)
      : parent_pid_(parent_pid), ctx_(ctx), timestamp_(timestamp),
        last_written_(ctx_->written()) {}

  // Return whether the next call to GetHeapGraph will create a new TracePacket.
  bool will_create_new_packet() {
    return !heap_graph_ || ctx_->written() - last_written_ > kPacketSizeThreshold;
  }

  perfetto::protos::pbzero::HeapGraph* GetHeapGraph() {
    if (will_create_new_packet()) {
      CreateNewHeapGraph();
    }
    return heap_graph_;
  }

  void CreateNewHeapGraph() {
    if (heap_graph_) {
      heap_graph_->set_continued(true);
    }
    Finalize();

    uint64_t written = ctx_->written();

    trace_packet_ = ctx_->NewTracePacket();
    trace_packet_->set_timestamp(timestamp_);
    heap_graph_ = trace_packet_->set_heap_graph();
    heap_graph_->set_pid(parent_pid_);
    heap_graph_->set_index(index_++);

    last_written_ = written;
  }

  void Finalize() {
    if (trace_packet_) {
      trace_packet_->Finalize();
    }
    heap_graph_ = nullptr;
  }

  ~Writer() { Finalize(); }

 private:
  const pid_t parent_pid_;
  JavaHprofDataSource::TraceContext* const ctx_;
  const uint64_t timestamp_;

  uint64_t last_written_ = 0;

  perfetto::DataSource<JavaHprofDataSource>::TraceContext::TracePacketHandle
      trace_packet_;
  perfetto::protos::pbzero::HeapGraph* heap_graph_ = nullptr;

  uint64_t index_ = 0;
};

class ReferredObjectsFinder {
 public:
  explicit ReferredObjectsFinder(
      std::vector<std::pair<std::string, art::mirror::Object*>>* referred_objects,
      art::mirror::Object** min_nonnull_ptr)
      : referred_objects_(referred_objects), min_nonnull_ptr_(min_nonnull_ptr) {}

  // For art::mirror::Object::VisitReferences.
  void operator()(art::ObjPtr<art::mirror::Object> obj, art::MemberOffset offset,
                  bool is_static) const
      REQUIRES_SHARED(art::Locks::mutator_lock_) {
    if (offset.Uint32Value() == art::mirror::Object::ClassOffset().Uint32Value()) {
      // Skip shadow$klass pointer.
      return;
    }
    art::mirror::Object* ref = obj->GetFieldObject<art::mirror::Object>(offset);
    art::ArtField* field;
    if (is_static) {
      field = art::ArtField::FindStaticFieldWithOffset(obj->AsClass(), offset.Uint32Value());
    } else {
      field = art::ArtField::FindInstanceFieldWithOffset(obj->GetClass(), offset.Uint32Value());
    }
    std::string field_name = "";
    if (field != nullptr) {
      field_name = field->PrettyField(/*with_type=*/true);
    }
    referred_objects_->emplace_back(std::move(field_name), ref);
    if (!*min_nonnull_ptr_ || (ref && *min_nonnull_ptr_ > ref)) {
      *min_nonnull_ptr_ = ref;
    }
  }

  void VisitRootIfNonNull(art::mirror::CompressedReference<art::mirror::Object>* root
                              ATTRIBUTE_UNUSED) const {}
  void VisitRoot(art::mirror::CompressedReference<art::mirror::Object>* root
                     ATTRIBUTE_UNUSED) const {}

 private:
  // We can use a raw Object* pointer here, because there are no concurrent GC threads after the
  // fork.
  std::vector<std::pair<std::string, art::mirror::Object*>>* referred_objects_;
  art::mirror::Object** min_nonnull_ptr_;
};

class RootFinder : public art::SingleRootVisitor {
 public:
  explicit RootFinder(
    std::map<art::RootType, std::vector<art::mirror::Object*>>* root_objects)
      : root_objects_(root_objects) {}

  void VisitRoot(art::mirror::Object* root, const art::RootInfo& info) override {
    (*root_objects_)[info.GetType()].emplace_back(root);
  }

 private:
  // We can use a raw Object* pointer here, because there are no concurrent GC threads after the
  // fork.
  std::map<art::RootType, std::vector<art::mirror::Object*>>* root_objects_;
};

perfetto::protos::pbzero::HeapGraphRoot::Type ToProtoType(art::RootType art_type) {
  using perfetto::protos::pbzero::HeapGraphRoot;
  switch (art_type) {
    case art::kRootUnknown:
      return HeapGraphRoot::ROOT_UNKNOWN;
    case art::kRootJNIGlobal:
      return HeapGraphRoot::ROOT_JNI_GLOBAL;
    case art::kRootJNILocal:
      return HeapGraphRoot::ROOT_JNI_LOCAL;
    case art::kRootJavaFrame:
      return HeapGraphRoot::ROOT_JAVA_FRAME;
    case art::kRootNativeStack:
      return HeapGraphRoot::ROOT_NATIVE_STACK;
    case art::kRootStickyClass:
      return HeapGraphRoot::ROOT_STICKY_CLASS;
    case art::kRootThreadBlock:
      return HeapGraphRoot::ROOT_THREAD_BLOCK;
    case art::kRootMonitorUsed:
      return HeapGraphRoot::ROOT_MONITOR_USED;
    case art::kRootThreadObject:
      return HeapGraphRoot::ROOT_THREAD_OBJECT;
    case art::kRootInternedString:
      return HeapGraphRoot::ROOT_INTERNED_STRING;
    case art::kRootFinalizing:
      return HeapGraphRoot::ROOT_FINALIZING;
    case art::kRootDebugger:
      return HeapGraphRoot::ROOT_DEBUGGER;
    case art::kRootReferenceCleanup:
      return HeapGraphRoot::ROOT_REFERENCE_CLEANUP;
    case art::kRootVMInternal:
      return HeapGraphRoot::ROOT_VM_INTERNAL;
    case art::kRootJNIMonitor:
      return HeapGraphRoot::ROOT_JNI_MONITOR;
  }
}

perfetto::protos::pbzero::HeapGraphType::Kind ProtoClassKind(uint32_t class_flags) {
  using perfetto::protos::pbzero::HeapGraphType;
  switch (class_flags) {
    case art::mirror::kClassFlagNormal:
      return HeapGraphType::KIND_NORMAL;
    case art::mirror::kClassFlagNoReferenceFields:
      return HeapGraphType::KIND_NOREFERENCES;
    case art::mirror::kClassFlagString | art::mirror::kClassFlagNoReferenceFields:
      return HeapGraphType::KIND_STRING;
    case art::mirror::kClassFlagObjectArray:
      return HeapGraphType::KIND_ARRAY;
    case art::mirror::kClassFlagClass:
      return HeapGraphType::KIND_CLASS;
    case art::mirror::kClassFlagClassLoader:
      return HeapGraphType::KIND_CLASSLOADER;
    case art::mirror::kClassFlagDexCache:
      return HeapGraphType::KIND_DEXCACHE;
    case art::mirror::kClassFlagSoftReference:
      return HeapGraphType::KIND_SOFT_REFERENCE;
    case art::mirror::kClassFlagWeakReference:
      return HeapGraphType::KIND_WEAK_REFERENCE;
    case art::mirror::kClassFlagFinalizerReference:
      return HeapGraphType::KIND_FINALIZER_REFERENCE;
    case art::mirror::kClassFlagPhantomReference:
      return HeapGraphType::KIND_PHANTOM_REFERENCE;
    default:
      return HeapGraphType::KIND_UNKNOWN;
  }
}

std::string PrettyType(art::mirror::Class* klass) NO_THREAD_SAFETY_ANALYSIS {
  if (klass == nullptr) {
    return "(raw)";
  }
  std::string temp;
  std::string result(art::PrettyDescriptor(klass->GetDescriptor(&temp)));
  return result;
}

void DumpSmaps(JavaHprofDataSource::TraceContext* ctx) {
  FILE* smaps = fopen("/proc/self/smaps", "r");
  if (smaps != nullptr) {
    auto trace_packet = ctx->NewTracePacket();
    auto* smaps_packet = trace_packet->set_smaps_packet();
    smaps_packet->set_pid(getpid());
    perfetto::profiling::ParseSmaps(smaps,
        [&smaps_packet](const perfetto::profiling::SmapsEntry& e) {
      if (ShouldSampleSmapsEntry(e)) {
        auto* smaps_entry = smaps_packet->add_entries();
        smaps_entry->set_path(e.pathname);
        smaps_entry->set_size_kb(e.size_kb);
        smaps_entry->set_private_dirty_kb(e.private_dirty_kb);
        smaps_entry->set_swap_kb(e.swap_kb);
      }
    });
    fclose(smaps);
  } else {
    PLOG(ERROR) << "failed to open smaps";
  }
}

uint64_t GetObjectId(const art::mirror::Object* obj) {
  return reinterpret_cast<uint64_t>(obj) / std::alignment_of<art::mirror::Object>::value;
}

template <typename F>
void ForInstanceReferenceField(art::mirror::Class* klass, F fn) NO_THREAD_SAFETY_ANALYSIS {
  for (art::ArtField& af : klass->GetIFields()) {
    if (af.IsPrimitiveType() ||
        af.GetOffset().Uint32Value() == art::mirror::Object::ClassOffset().Uint32Value()) {
      continue;
    }
    fn(af.GetOffset());
  }
}

bool IsIgnored(const std::vector<std::string>& ignored_types,
               art::mirror::Object* obj) NO_THREAD_SAFETY_ANALYSIS {
  if (obj->IsClass()) {
    return false;
  }
  art::mirror::Class* klass = obj->GetClass();
  return std::find(ignored_types.begin(), ignored_types.end(), PrettyType(klass)) !=
         ignored_types.end();
}

size_t EncodedSize(uint64_t n) {
  if (n == 0) return 1;
  return 1 + static_cast<size_t>(art::MostSignificantBit(n)) / 7;
}

void DumpPerfetto(art::Thread* self) {
  pid_t parent_pid = getpid();
  LOG(INFO) << "preparing to dump heap for " << parent_pid;

  // Need to take a heap dump while GC isn't running. See the comment in
  // Heap::VisitObjects(). Also we need the critical section to avoid visiting
  // the same object twice. See b/34967844.
  //
  // We need to do this before the fork, because otherwise it can deadlock
  // waiting for the GC, as all other threads get terminated by the clone, but
  // their locks are not released.
  // This does not perfectly solve all fork-related issues, as there could still be threads that
  // are unaffected by ScopedSuspendAll and in a non-fork-friendly situation
  // (e.g. inside a malloc holding a lock). This situation is quite rare, and in that case we will
  // hit the watchdog in the grand-child process if it gets stuck.
  std::optional<art::gc::ScopedGCCriticalSection> gcs(std::in_place, self, art::gc::kGcCauseHprof,
                                                      art::gc::kCollectorTypeHprof);

  std::optional<art::ScopedSuspendAll> ssa(std::in_place, __FUNCTION__, /* long_suspend=*/ true);

  pid_t pid = fork();
  if (pid == -1) {
    // Fork error.
    PLOG(ERROR) << "fork";
    return;
  }
  if (pid != 0) {
    // Parent
    // Stop the thread suspension as soon as possible to allow the rest of the application to
    // continue while we waitpid here.
    ssa.reset();
    gcs.reset();
    for (size_t i = 0;; ++i) {
      if (i == 1000) {
        // The child hasn't exited for 1 second (and all it was supposed to do was fork itself).
        // Give up and SIGKILL it. The next waitpid should succeed.
        LOG(ERROR) << "perfetto_hprof child timed out. Sending SIGKILL.";
        kill(pid, SIGKILL);
      }
      // Busy waiting here will introduce some extra latency, but that is okay because we have
      // already unsuspended all other threads. This runs on the perfetto_hprof_listener, which
      // is not needed for progress of the app itself.
      int stat_loc;
      pid_t wait_result = waitpid(pid, &stat_loc, WNOHANG);
      if (wait_result == -1 && errno != EINTR) {
        if (errno != ECHILD) {
          // This hopefully never happens (should only be EINVAL).
          PLOG(FATAL_WITHOUT_ABORT) << "waitpid";
        }
        // If we get ECHILD, the parent process was handling SIGCHLD, or did a wildcard wait.
        // The child is no longer here either way, so that's good enough for us.
        break;
      } else if (wait_result > 0) {
        break;
      } else {  // wait_result == 0 || errno == EINTR.
        usleep(1000);
      }
    }
    return;
  }

  // The following code is only executed by the child of the original process.

  // Uninstall signal handler, so we don't trigger a profile on it.
  if (sigaction(kJavaHeapprofdSignal, &g_orig_act, nullptr) != 0) {
    close(g_signal_pipe_fds[0]);
    close(g_signal_pipe_fds[1]);
    PLOG(FATAL) << "Failed to sigaction";
    return;
  }

  // Daemon creates a new process that is the grand-child of the original process, and exits.
  if (daemon(0, 0) == -1) {
    PLOG(FATAL) << "daemon";
  }

  // The following code is only executed by the grand-child of the original process.

  // Make sure that this is the first thing we do after forking, so if anything
  // below hangs, the fork will go away from the watchdog.
  ArmWatchdogOrDie();

  struct timespec ts = {};
  if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0) {
    LOG(FATAL) << "Failed to get boottime.";
  }
  uint64_t timestamp = ts.tv_sec * 1000000000LL + ts.tv_nsec;

  WaitForDataSource(self);

  JavaHprofDataSource::Trace(
      [parent_pid, timestamp](JavaHprofDataSource::TraceContext ctx)
          NO_THREAD_SAFETY_ANALYSIS {
            bool dump_smaps;
            std::vector<std::string> ignored_types;
            {
              auto ds = ctx.GetDataSourceLocked();
              if (!ds || !ds->enabled()) {
                if (ds) ds->Finish();
                LOG(INFO) << "skipping irrelevant data source.";
                return;
              }
              dump_smaps = ds->dump_smaps();
              ignored_types = ds->ignored_types();
            }
            LOG(INFO) << "dumping heap for " << parent_pid;
            if (dump_smaps) {
              DumpSmaps(&ctx);
            }
            Writer writer(parent_pid, &ctx, timestamp);
            // Make sure that intern ID 0 (default proto value for a uint64_t) always maps to ""
            // (default proto value for a string).
            std::map<std::string, uint64_t> interned_fields{{"", 0}};
            std::map<std::string, uint64_t> interned_locations{{"", 0}};
            std::map<uintptr_t, uint64_t> interned_classes{{0, 0}};

            std::map<art::RootType, std::vector<art::mirror::Object*>> root_objects;
            RootFinder rcf(&root_objects);
            art::Runtime::Current()->VisitRoots(&rcf);
            std::unique_ptr<protozero::PackedVarInt> object_ids(
                new protozero::PackedVarInt);
            for (const auto& p : root_objects) {
              const art::RootType root_type = p.first;
              const std::vector<art::mirror::Object*>& children = p.second;
              perfetto::protos::pbzero::HeapGraphRoot* root_proto =
                writer.GetHeapGraph()->add_roots();
              root_proto->set_root_type(ToProtoType(root_type));
              for (art::mirror::Object* obj : children) {
                if (writer.will_create_new_packet()) {
                  root_proto->set_object_ids(*object_ids);
                  object_ids->Reset();
                  root_proto = writer.GetHeapGraph()->add_roots();
                  root_proto->set_root_type(ToProtoType(root_type));
                }
                object_ids->Append(GetObjectId(obj));
              }
              root_proto->set_object_ids(*object_ids);
              object_ids->Reset();
            }

            std::unique_ptr<protozero::PackedVarInt> reference_field_ids(
                new protozero::PackedVarInt);
            std::unique_ptr<protozero::PackedVarInt> reference_object_ids(
                new protozero::PackedVarInt);

            uint64_t prev_object_id = 0;

            art::Runtime::Current()->GetHeap()->VisitObjectsPaused(
                [&writer, &interned_fields, &interned_locations, &reference_field_ids,
                 &reference_object_ids, &interned_classes, &ignored_types, &prev_object_id](
                    art::mirror::Object* obj) REQUIRES_SHARED(art::Locks::mutator_lock_) {
                  if (obj->IsClass()) {
                    art::mirror::Class* klass = obj->AsClass().Ptr();
                    perfetto::protos::pbzero::HeapGraphType* type_proto =
                      writer.GetHeapGraph()->add_types();
                    type_proto->set_id(FindOrAppend(&interned_classes,
                          reinterpret_cast<uintptr_t>(klass)));
                    type_proto->set_class_name(PrettyType(klass));
                    type_proto->set_location_id(FindOrAppend(&interned_locations,
                          klass->GetLocation()));
                    type_proto->set_object_size(klass->GetObjectSize());
                    type_proto->set_kind(ProtoClassKind(klass->GetClassFlags()));
                    type_proto->set_classloader_id(GetObjectId(klass->GetClassLoader().Ptr()));
                    if (klass->GetSuperClass().Ptr()) {
                      type_proto->set_superclass_id(
                        FindOrAppend(&interned_classes,
                                     reinterpret_cast<uintptr_t>(klass->GetSuperClass().Ptr())));
                    }
                    ForInstanceReferenceField(
                        klass, [klass, &reference_field_ids, &interned_fields](
                                   art::MemberOffset offset) NO_THREAD_SAFETY_ANALYSIS {
                          auto art_field = art::ArtField::FindInstanceFieldWithOffset(
                              klass, offset.Uint32Value());
                          reference_field_ids->Append(
                              FindOrAppend(&interned_fields, art_field->PrettyField(true)));
                        });
                    type_proto->set_reference_field_id(*reference_field_ids);
                    reference_field_ids->Reset();
                  }

                  art::mirror::Class* klass = obj->GetClass();
                  uintptr_t class_ptr = reinterpret_cast<uintptr_t>(klass);
                  // We need to synethesize a new type for Class<Foo>, which does not exist
                  // in the runtime. Otherwise, all the static members of all classes would be
                  // attributed to java.lang.Class.
                  if (klass->IsClassClass()) {
                    CHECK(obj->IsClass());
                    perfetto::protos::pbzero::HeapGraphType* type_proto =
                      writer.GetHeapGraph()->add_types();
                    // All pointers are at least multiples of two, so this way we can make sure
                    // we are not colliding with a real class.
                    class_ptr = reinterpret_cast<uintptr_t>(obj) | 1;
                    auto class_id = FindOrAppend(&interned_classes, class_ptr);
                    type_proto->set_id(class_id);
                    type_proto->set_class_name(obj->PrettyTypeOf());
                    type_proto->set_location_id(FindOrAppend(&interned_locations,
                          obj->AsClass()->GetLocation()));
                  }

                  if (IsIgnored(ignored_types, obj)) {
                    return;
                  }

                  auto class_id = FindOrAppend(&interned_classes, class_ptr);

                  uint64_t object_id = GetObjectId(obj);
                  perfetto::protos::pbzero::HeapGraphObject* object_proto =
                    writer.GetHeapGraph()->add_objects();
                  if (prev_object_id && prev_object_id < object_id) {
                    object_proto->set_id_delta(object_id - prev_object_id);
                  } else {
                    object_proto->set_id(object_id);
                  }
                  prev_object_id = object_id;
                  object_proto->set_type_id(class_id);

                  // Arrays / strings are magic and have an instance dependent size.
                  if (obj->SizeOf() != klass->GetObjectSize())
                    object_proto->set_self_size(obj->SizeOf());

                  std::vector<std::pair<std::string, art::mirror::Object*>>
                      referred_objects;
                  art::mirror::Object* min_nonnull_ptr = nullptr;
                  ReferredObjectsFinder objf(&referred_objects, &min_nonnull_ptr);

                  const bool emit_field_ids =
                      klass->GetClassFlags() != art::mirror::kClassFlagObjectArray &&
                      klass->GetClassFlags() != art::mirror::kClassFlagNormal;
                  if (klass->GetClassFlags() != art::mirror::kClassFlagNormal) {
                    obj->VisitReferences(objf, art::VoidFunctor());
                  } else {
                    for (art::mirror::Class* cls = klass; cls != nullptr;
                         cls = cls->GetSuperClass().Ptr()) {
                      ForInstanceReferenceField(
                          cls, [obj, objf](art::MemberOffset offset) NO_THREAD_SAFETY_ANALYSIS {
                            objf(art::ObjPtr<art::mirror::Object>(obj), offset,
                                 /*is_static=*/false);
                          });
                    }
                  }

                  uint64_t bytes_saved = 0;
                  uint64_t base_obj_id = GetObjectId(min_nonnull_ptr);
                  if (base_obj_id) {
                    // We need to decrement the base for object ids so that we can tell apart
                    // null references.
                    base_obj_id--;
                  }
                  if (base_obj_id) {
                    for (auto& p : referred_objects) {
                      art::mirror::Object*& referred_obj = p.second;
                      if (!referred_obj || IsIgnored(ignored_types, referred_obj)) {
                        referred_obj = nullptr;
                        continue;
                      }
                      uint64_t referred_obj_id = GetObjectId(referred_obj);
                      bytes_saved +=
                          EncodedSize(referred_obj_id) - EncodedSize(referred_obj_id - base_obj_id);
                    }
                  }

                  // +1 for storing the field id.
                  if (bytes_saved <= EncodedSize(base_obj_id) + 1) {
                    // Subtracting the base ptr gains fewer bytes than it takes to store it.
                    base_obj_id = 0;
                  }

                  for (auto& p : referred_objects) {
                    const std::string& field_name = p.first;
                    art::mirror::Object* referred_obj = p.second;
                    if (emit_field_ids) {
                      reference_field_ids->Append(FindOrAppend(&interned_fields, field_name));
                    }
                    uint64_t referred_obj_id = GetObjectId(referred_obj);
                    if (referred_obj_id) {
                      referred_obj_id -= base_obj_id;
                    }
                    reference_object_ids->Append(referred_obj_id);
                  }
                  if (emit_field_ids) {
                    object_proto->set_reference_field_id(*reference_field_ids);
                    reference_field_ids->Reset();
                  }
                  if (base_obj_id) {
                    object_proto->set_reference_field_id_base(base_obj_id);
                  }
                  object_proto->set_reference_object_id(*reference_object_ids);
                  reference_object_ids->Reset();
                });

            for (const auto& p : interned_locations) {
              const std::string& str = p.first;
              uint64_t id = p.second;

              perfetto::protos::pbzero::InternedString* location_proto =
                writer.GetHeapGraph()->add_location_names();
              location_proto->set_iid(id);
              location_proto->set_str(reinterpret_cast<const uint8_t*>(str.c_str()),
                                  str.size());
            }
            for (const auto& p : interned_fields) {
              const std::string& str = p.first;
              uint64_t id = p.second;

              perfetto::protos::pbzero::InternedString* field_proto =
                writer.GetHeapGraph()->add_field_names();
              field_proto->set_iid(id);
              field_proto->set_str(
                  reinterpret_cast<const uint8_t*>(str.c_str()), str.size());
            }

            writer.Finalize();
            ctx.Flush([] {
              {
                art::MutexLock lk(JavaHprofDataSource::art_thread(), GetStateMutex());
                g_state = State::kEnd;
                GetStateCV().Broadcast(JavaHprofDataSource::art_thread());
              }
            });
            // Wait for the Flush that will happen on the Perfetto thread.
            {
              art::MutexLock lk(JavaHprofDataSource::art_thread(), GetStateMutex());
              while (g_state != State::kEnd) {
                GetStateCV().Wait(JavaHprofDataSource::art_thread());
              }
            }
            {
              auto ds = ctx.GetDataSourceLocked();
              if (ds) {
                ds->Finish();
              } else {
                LOG(ERROR) << "datasource timed out (duration_ms + datasource_stop_timeout_ms) "
                              "before dump finished";
              }
            }
          });

  LOG(INFO) << "finished dumping heap for " << parent_pid;
  // Prevent the atexit handlers to run. We do not want to call cleanup
  // functions the parent process has registered. However, have functions
  // registered with `at_quick_exit` (for instance LLVM's code coverage profile
  // dumping routine) be called before exiting.
  quick_exit(0);
}

// The plugin initialization function.
extern "C" bool ArtPlugin_Initialize() {
  if (art::Runtime::Current() == nullptr) {
    return false;
  }
  art::Thread* self = art::Thread::Current();
  {
    art::MutexLock lk(self, GetStateMutex());
    if (g_state != State::kUninitialized) {
      LOG(ERROR) << "perfetto_hprof already initialized. state: " << g_state;
      return false;
    }
    g_state = State::kWaitForListener;
  }

  if (pipe2(g_signal_pipe_fds, O_CLOEXEC) == -1) {
    PLOG(ERROR) << "Failed to pipe";
    return false;
  }

  struct sigaction act = {};
  act.sa_flags = SA_SIGINFO | SA_RESTART;
  act.sa_sigaction = [](int, siginfo_t* si, void*) {
    requested_tracing_session_id = si->si_value.sival_int;
    if (write(g_signal_pipe_fds[1], kByte, sizeof(kByte)) == -1) {
      PLOG(ERROR) << "Failed to trigger heap dump";
    }
  };

  // TODO(fmayer): We can probably use the SignalCatcher thread here to not
  // have an idle thread.
  if (sigaction(kJavaHeapprofdSignal, &act, &g_orig_act) != 0) {
    close(g_signal_pipe_fds[0]);
    close(g_signal_pipe_fds[1]);
    PLOG(ERROR) << "Failed to sigaction";
    return false;
  }

  std::thread th([] {
    art::Runtime* runtime = art::Runtime::Current();
    if (!runtime) {
      LOG(FATAL_WITHOUT_ABORT) << "no runtime in perfetto_hprof_listener";
      return;
    }
    if (!runtime->AttachCurrentThread("perfetto_hprof_listener", /*as_daemon=*/ true,
                                      runtime->GetSystemThreadGroup(), /*create_peer=*/ false)) {
      LOG(ERROR) << "failed to attach thread.";
      {
        art::MutexLock lk(nullptr, GetStateMutex());
        g_state = State::kUninitialized;
        GetStateCV().Broadcast(nullptr);
      }

      return;
    }
    art::Thread* self = art::Thread::Current();
    if (!self) {
      LOG(FATAL_WITHOUT_ABORT) << "no thread in perfetto_hprof_listener";
      return;
    }
    {
      art::MutexLock lk(self, GetStateMutex());
      if (g_state == State::kWaitForListener) {
        g_state = State::kWaitForStart;
        GetStateCV().Broadcast(self);
      }
    }
    char buf[1];
    for (;;) {
      int res;
      do {
        res = read(g_signal_pipe_fds[0], buf, sizeof(buf));
      } while (res == -1 && errno == EINTR);

      if (res <= 0) {
        if (res == -1) {
          PLOG(ERROR) << "failed to read";
        }
        close(g_signal_pipe_fds[0]);
        return;
      }

      perfetto_hprof::DumpPerfetto(self);
    }
  });
  th.detach();

  return true;
}

extern "C" bool ArtPlugin_Deinitialize() {
  if (sigaction(kJavaHeapprofdSignal, &g_orig_act, nullptr) != 0) {
    PLOG(ERROR) << "failed to reset signal handler";
    // We cannot close the pipe if the signal handler wasn't unregistered,
    // to avoid receiving SIGPIPE.
    return false;
  }
  close(g_signal_pipe_fds[1]);

  art::Thread* self = art::Thread::Current();
  art::MutexLock lk(self, GetStateMutex());
  // Wait until after the thread was registered to the runtime. This is so
  // we do not attempt to register it with the runtime after it had been torn
  // down (ArtPlugin_Deinitialize gets called in the Runtime dtor).
  while (g_state == State::kWaitForListener) {
    GetStateCV().Wait(art::Thread::Current());
  }
  g_state = State::kUninitialized;
  GetStateCV().Broadcast(self);
  return true;
}

}  // namespace perfetto_hprof

namespace perfetto {

PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(perfetto_hprof::JavaHprofDataSource);

}
