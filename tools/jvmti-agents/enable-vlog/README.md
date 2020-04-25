# enablevlog

enablevlog is a JVMTI agent designed for changing the VLOG options of a
running process. Currently it only allows one to turn vlog options 'on'.

# Usage
### Build
>    `m libenablevlog`

The libraries will be built for 32-bit, 64-bit, host and target. Below
examples assume you want to use the 64-bit version.

Use `libenablevlogs` if you wish to build a version without non-NDK dynamic
dependencies.

### Command Line

The agent is loaded using -agentpath like normal. It takes arguments in the
following format:

`[vlog1[,vlog2[,...]]]`

It will cause the runtime to act as though you had passed these after the
`-verbose:[...]` argument to `dalvikvm`.

#### Supported events

At the time of writing, the following events may be listened for with this agent

* `class`

* `collector`

* `compiler`

* `deopt`

* `gc`

* `heap`

* `interpreter`

* `jdwp`

* `jit`

* `jni`

* `monitor`

* `oat`

* `profiler`

* `signals`

* `simulator`

* `startup`

* `third-party-jni`

* `threads`

* `verifier`

* `verifier-debug`

* `image`

* `systrace-locks`

* `plugin`

* `agents`

* `dex`

These are not particularly stable and new options might get added. Examine
the LogVerbosity struct definition and the parser for a up-to-date list.

#### ART
>    `art -Xplugin:$ANDROID_HOST_OUT/lib64/libopenjdkjvmti.so '-agentpath:libenablevlog.so=class,profiler' -cp tmp/java/helloworld.dex -Xint helloworld`

* `-Xplugin` and `-agentpath` need to be used, otherwise the agent will fail during init.
* If using `libartd.so`, make sure to use the debug version of jvmti.

>    `adb shell setenforce 0`
>
>    `adb push $ANDROID_PRODUCT_OUT/system/lib64/libenablevlog.so /data/local/tmp/`
>
>    `adb shell am start-activity --attach-agent /data/local/tmp/libenablevlog.so=class,jit some.debuggable.apps/.the.app.MainActivity`
