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
# This script can get info out of dexfiles using libdexfile.so external API
#

from abc import ABC
from ctypes import *
import os.path
import functools
import zipfile

libdexfile = CDLL(
    os.path.expandvars("$ANDROID_HOST_OUT/lib64/libdexfiled.so"))

ExtDexFile = c_void_p

class ExtMethodInfo(Structure):
  """Output format for MethodInfo"""
  _fields_ = [("sizeof_struct", c_size_t),
              ("addr", c_int32),
              ("size", c_int32),
              ("name", POINTER(c_char)),
              ("name_size", c_size_t)]

AllMethodsCallback = CFUNCTYPE(c_int, c_void_p, POINTER(ExtMethodInfo))
libdexfile.ExtDexFileOpenFromMemory.argtypes = [
    c_void_p,
    POINTER(c_size_t),
    c_char_p,
    POINTER(ExtDexFile)
]
libdexfile.ExtDexFileOpenFromMemory.restype = c_int
libdexfile.ExtDexFileGetAllMethodInfos.argtypes = [
    ExtDexFile, c_int, AllMethodsCallback, c_void_p
]

class DexClass(object):
  """Accessor for DexClass Data"""

  def __init__(self, name):
    self.name = name.strip()
    self.arrays = name.count("[")
    self.base_name = self.name if self.arrays == 0 else self.name[:-(
        self.arrays * 2)]

  def __repr__(self):
    return self.name

  @functools.cached_property
  def descriptor(self):
    """The name as a descriptor"""
    if self.base_name == "int":
      return "[" * self.arrays + "I"
    elif self.base_name == "short":
      return "[" * self.arrays + "S"
    elif self.base_name == "long":
      return "[" * self.arrays + "J"
    elif self.base_name == "char":
      return "[" * self.arrays + "C"
    elif self.base_name == "boolean":
      return "[" * self.arrays + "Z"
    elif self.base_name == "byte":
      return "[" * self.arrays + "B"
    elif self.base_name == "float":
      return "[" * self.arrays + "F"
    elif self.base_name == "double":
      return "[" * self.arrays + "D"
    elif self.base_name == "void":
      return "[" * self.arrays + "V"
    else:
      return "[" * self.arrays + "L{};".format("/".join(
          self.base_name.split(".")))


class Method(object):
  """Method info wrapper"""

  def __init__(self, mi):
    self.offset = mi.addr
    self.len = mi.size
    self.name = string_at(mi.name, mi.name_size).decode("utf-8")

  def __repr__(self):
    return "(" + self.name + ")"

  @functools.cached_property
  def descriptor(self):
    """name as a descriptor"""
    ret = DexClass(self.name.split(" ")[0])
    non_ret = self.name[len(ret.name) + 1:]
    arg_str = non_ret[non_ret.find("(") + 1:-1]
    args = [] if arg_str == "" else map(
        lambda a: DexClass(a.strip()).descriptor, arg_str.split(","))
    class_and_meth = non_ret[0:non_ret.find("(")]
    class_only = DexClass(class_and_meth[0:class_and_meth.rfind(".")])
    meth = class_and_meth[class_and_meth.rfind(".") + 1:]
    return "{cls}->{meth}({args}){ret}".format(
        cls=class_only.descriptor,
        meth=meth,
        args="".join(args),
        ret=ret.descriptor)

  @functools.cached_property
  def name_only(self):
    """name without the return-type or arguments in java format"""
    ret = DexClass(self.name.split(" ")[0])
    non_ret = self.name[len(ret.name) + 1:]
    class_and_meth = non_ret[0:non_ret.find("(")]
    return class_and_meth

  @functools.cached_property
  def klass(self):
    """declaring DexClass."""
    ret = DexClass(self.name.split(" ")[0])
    non_ret = self.name[len(ret.name) + 1:]
    class_and_meth = non_ret[0:non_ret.find("(")]
    return DexClass(class_and_meth[0:class_and_meth.rfind(".")])


class BaseDexFile(ABC):
  """DexFile base class"""

  def __init__(self):
    self.ext_dex_file_ = None
    return

  @functools.cached_property
  def methods(self):
    """Methods in the dex-file"""
    meths = []

    @AllMethodsCallback
    def my_cb(_, info):
      """Callback visitor for method infos"""
      meths.append(Method(info[0]))
      return 0

    libdexfile.ExtDexFileGetAllMethodInfos(self.ext_dex_file_,
                                           c_int(1), my_cb, c_void_p())
    return meths

class MemDexFile(BaseDexFile):
  """DexFile using memory"""

  def __init__(self, dat, loc):
    assert type(dat) == bytes
    super().__init__()
    # Don't want GC to screw us over.
    self.mem_ref = (c_byte * len(dat)).from_buffer_copy(dat)
    res_fle_ptr = pointer(c_void_p())
    res = libdexfile.ExtDexFileOpenFromMemory(
        self.mem_ref, byref(c_size_t(len(dat))),
        create_string_buffer(bytes(loc, "utf-8")), res_fle_ptr)
    if res != 0:
      raise Exception("Failed to open file: {}. Error {}.".format(loc, res))
    self.ext_dex_file_ = res_fle_ptr.contents

class FileDexFile(MemDexFile):
  """DexFile using a file"""

  def __init__(self, file, loc):
    if type(file) == str:
      self.file = open(file, "rb")
      self.loc = file
    else:
      self.file = file
      self.loc = "file_obj"
    super().__init__(self.file.read(), self.loc)

def OpenJar(fle):
  """Opens all classes[0-9]*.dex files in a zip archive"""
  res = []
  with zipfile.ZipFile(fle) as zf:
    for f in zf.namelist():
      if f.endswith(".dex") and f.startswith("classes"):
        res.append(
            MemDexFile(zf.read(f), "classes" if type(fle) != str else fle))
  return res
