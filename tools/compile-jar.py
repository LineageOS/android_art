#!/usr/bin/python3
#
# Copyright (C) 2021 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#
# This script runs dex2oat on the host to compile a provided JAR or APK.
#

import argparse
import itertools
import shlex
import subprocess
import os
import os.path

def run_print(lst):
  return " ".join(map(shlex.quote, lst))


def parse_args():
  parser = argparse.ArgumentParser(
      description="compile dex or jar files",
      epilog="Unrecognized options are passed on to dex2oat unmodified.")
  parser.add_argument(
      "--dex2oat",
      action="store",
      default=os.path.expandvars("$ANDROID_HOST_OUT/bin/dex2oatd64"),
      help="selects the dex2oat to use.")
  parser.add_argument(
      "--debug",
      action="store_true",
      default=False,
      help="launches dex2oatd with lldb-server g :5039. Connect using vscode or remote lldb"
  )
  parser.add_argument(
      "--profman",
      action="store",
      default=os.path.expandvars("$ANDROID_HOST_OUT/bin/profmand"),
      help="selects the profman to use.")
  parser.add_argument(
      "--debug-profman",
      action="store_true",
      default=False,
      help="launches profman with lldb-server g :5039. Connect using vscode or remote lldb"
  )
  profs = parser.add_mutually_exclusive_group()
  profs.add_argument(
      "--profile-file",
      action="store",
      help="Use this profile file. Probably want to pass --compiler-filter=speed-profile with this."
  )
  profs.add_argument(
      "--profile-line",
      action="append",
      default=[],
      help="functions to add to a profile. Probably want to pass --compiler-filter=speed-profile with this. All functions are marked as 'hot'. Use --profile-file for more control."
  )
  parser.add_argument(
      "--add-bcp",
      action="append",
      default=[],
      nargs=2,
      metavar=("BCP_FILE", "BCP_LOCATION"),
      help="File and location to add to the boot-class-path. Note no deduplication is attempted."
  )
  parser.add_argument(
      "--arch",
      action="store",
      choices=["arm", "arm64", "x86", "x86_64", "host64", "host32"],
      default="host64",
      help="architecture to compile for. Defaults to host64")
  parser.add_argument(
      "--odex-file",
      action="store",
      help="odex file to write. File discarded if not set",
      default=None)
  parser.add_argument(
      "--save-profile",
      action="store",
      type=argparse.FileType("w"),
      default=None,
      help="File path to store the profile to")
  parser.add_argument(
      "dex_files", help="dex/jar files", nargs="+", metavar="DEX")
  return parser.parse_known_args()


def get_bcp_runtime_args(additions, image, arch):
  add_files = map(lambda a: a[0], additions)
  add_locs = map(lambda a: a[1], additions)
  if arch != "host32" and arch != "host64":
    args = [
        "art/tools/host_bcp.sh",
        os.path.expandvars(
            "${{OUT}}/system/framework/oat/{}/services.odex".format(arch)),
        "--use-first-dir"
    ]
    print("Running: {}".format(run_print(args)))
    print("=START=======================================")
    res = subprocess.run(args, capture_output=True, text=True)
    print("=END=========================================")
    if res.returncode != 0:
      print("Falling back to com.android.art BCP")
      args = [
          "art/tools/host_bcp.sh",
          os.path.expandvars(
              "${{OUT}}/apex/com.android.art.debug/javalib/{}/boot.oat".format(arch)),
          "--use-first-dir"
      ]
      print("Running: {}".format(run_print(args)))
      print("=START=======================================")
      res = subprocess.run(args, capture_output=True, text=True)
      print("=END=========================================")
      res.check_returncode()
    segments = res.stdout.split()
    def extend_bcp(segment: str):
      # TODO We should make the bcp have absolute paths.
      if segment.startswith("-Xbootclasspath:"):
        return ":".join(itertools.chain((segment,), add_files))
      elif segment.startswith("-Xbootclasspath-locations:"):
        return ":".join(itertools.chain((segment,), add_locs))
      else:
        return segment
    return list(map(extend_bcp, segments))
  else:
    # Host we just use the bcp locations for both.
    res = open(
        os.path.expandvars(
            "$ANDROID_HOST_OUT/apex/art_boot_images/javalib/{}/boot.oat".format(
                "x86" if arch == "host32" else "x86_64")), "rb").read()
    bcp_tag = b"bootclasspath\0"
    bcp_start = res.find(bcp_tag) + len(bcp_tag)
    bcp = res[bcp_start:bcp_start + res[bcp_start:].find(b"\0")]
    img_bcp = bcp.decode()
    # TODO We should make the str_bcp have absolute paths.
    str_bcp = ":".join(itertools.chain((img_bcp,), add_files))
    str_bcp_loc = ":".join(itertools.chain((img_bcp,), add_locs))
    return [
        "--runtime-arg", "-Xbootclasspath:{}".format(str_bcp),
        "--runtime-arg", "-Xbootclasspath-locations:{}".format(str_bcp_loc)
    ]


def fdfile(fd):
  return "/proc/{}/fd/{}".format(os.getpid(), fd)


def get_profile_args(args, location_base):
  """Handle all the profile file options."""
  if args.profile_file is None and len(args.profile_line) == 0:
    return []
  if args.profile_file:
    with open(args.profile_file, "rb") as prof:
      prof_magic = prof.read(4)
      if prof_magic == b'pro\0':
        # Looks like the profile-file is a binary profile. Just use it directly
        return ['--profile-file={}'.format(args.profile_file)]
  if args.debug_profman:
    profman_args = ["lldb-server", "g", ":5039", "--", args.profman]
  else:
    profman_args = [args.profman]
  if args.save_profile:
    prof_out_fd = args.save_profile.fileno()
    os.set_inheritable(prof_out_fd, True)
  else:
    prof_out_fd = os.memfd_create("reference_prof", flags=0)
  if args.debug_profman:
    profman_args.append("--reference-profile-file={}".format(
        fdfile(prof_out_fd)))
  else:
    profman_args.append("--reference-profile-file-fd={}".format(prof_out_fd))
  if args.profile_file:
    profman_args.append("--create-profile-from={}".format(args.profile_file))
  else:
    prof_in_fd = os.memfd_create("input_prof", flags=0)
    # Why on earth does fdopen take control of the fd and not mention it in the docs.
    with os.fdopen(os.dup(prof_in_fd), "w") as prof_in:
      for l in args.profile_line:
        print(l, file=prof_in)
    profman_args.append("--create-profile-from={}".format(fdfile(prof_in_fd)))
  for f in args.dex_files:
    profman_args.append("--apk={}".format(f))
    profman_args.append("--dex-location={}".format(
        os.path.join(location_base, os.path.basename(f))))
  print("Running: {}".format(run_print(profman_args)))
  print("=START=======================================")
  subprocess.run(profman_args, close_fds=False).check_returncode()
  print("=END=========================================")
  if args.debug:
    return ["--profile-file={}".format(fdfile(prof_out_fd))]
  else:
    return ["--profile-file={}".format(fdfile(prof_out_fd))]


def main():
  args, extra = parse_args()
  if args.arch == "host32" or args.arch == "host64":
    location_base = os.path.expandvars("${ANDROID_HOST_OUT}/framework/")
    real_arch = "x86" if args.arch == "host32" else "x86_64"
    boot_image = os.path.expandvars(
        "$ANDROID_HOST_OUT/apex/art_boot_images/javalib/boot.art")
    android_root = os.path.expandvars("$ANDROID_HOST_OUT")
    for f in args.dex_files:
      extra.append("--dex-location={}".format(
          os.path.join(location_base, os.path.basename(f))))
      extra.append("--dex-file={}".format(f))
  else:
    location_base = "/system/framework"
    real_arch = args.arch
    boot_image = os.path.expandvars(":".join([
        "${OUT}/apex/art_boot_images/javalib/boot.art",
        "${OUT}/system/framework/boot-framework.art"
    ]))
    android_root = os.path.expandvars("$OUT/system")
    for f in args.dex_files:
      extra.append("--dex-location={}".format(
          os.path.join(location_base, os.path.basename(f))))
      extra.append("--dex-file={}".format(f))
  extra += get_bcp_runtime_args(args.add_bcp, boot_image, args.arch)
  extra += get_profile_args(args, location_base)
  extra.append("--instruction-set={}".format(real_arch))
  extra.append("--boot-image={}".format(boot_image))
  extra.append("--android-root={}".format(android_root))
  extra += ["--runtime-arg", "-Xms64m", "--runtime-arg", "-Xmx512m"]
  if args.odex_file is not None:
    extra.append("--oat-file={}".format(args.odex_file))
  else:
    if args.debug:
      raise Exception("Debug requires a real output file. :(")
    extra.append("--oat-fd={}".format(os.memfd_create("odex_fd", flags=0)))
    extra.append("--oat-location={}".format("/tmp/odex_fd.odex"))
    extra.append("--output-vdex-fd={}".format(
        os.memfd_create("vdex_fd", flags=0)))
  pre_args = []
  if args.debug:
    pre_args = ["lldb-server", "g", ":5039", "--"]
  pre_args.append(args.dex2oat)
  print("Running: {}".format(run_print(pre_args + extra)))
  print("=START=======================================")
  subprocess.run(pre_args + extra, close_fds=False).check_returncode()
  print("=END=========================================")


if __name__ == "__main__":
  main()
