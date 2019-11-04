# tiallocsample

tiallocsample is a JVMTI agent designed to track the call stacks of allocations
in the heap.

# Usage
### Build
>    `m libtiallocsample`

The libraries will be built for 32-bit, 64-bit, host and target. Below examples
assume you want to use the 64-bit version.

Use `libtiallocsamples` if you wish to build a version without non-NDK dynamic dependencies.

### Command Line

The agent is loaded using -agentpath like normal. It takes arguments in the
following format:
>     `sample_rate,stack_depth_limit,log_path`

* sample_rate is an integer specifying how frequently an event is reported.
  E.g., 10 means every tenth call to new will be logged.
* stack_depth_limit is an integer that determines the number of frames the deepest stack trace
  can contain.  It returns just the top portion if the limit is exceeded.
* log_path is an absolute file path specifying where the log is to be written.

#### Output Format

The resulting file is a sequence of object allocations, with a limited form of
text compression.  For example a single stack frame might look like:

```
+0,jthread[main], jclass[[I file: <UNKNOWN_FILE>], size[24, hex: 0x18]
+1,main([Ljava/lang/String;)V
+2,run()V
+3,invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;
+4,loop()V
+5,dispatchMessage(Landroid/os/Message;)V
+6,handleMessage(Landroid/os/Message;)V
+7,onDisplayChanged(I)V
+8,getState()I
+9,updateDisplayInfoLocked()V
+10,getDisplayInfo(I)Landroid/view/DisplayInfo;
+11,createFromParcel(Landroid/os/Parcel;)Ljava/lang/Object;
+12,createFromParcel(Landroid/os/Parcel;)Landroid/view/DisplayInfo;
+13,<init>(Landroid/os/Parcel;Landroid/view/DisplayInfo$1;)V
+14,<init>(Landroid/os/Parcel;)V
+15,readFromParcel(Landroid/os/Parcel;)V
=16,0;1;2;3;1;4;5;6;7;8;9;10;10;11;12;13;14;15
16
```

Lines starting with a + are key, value pairs.  So, for instance, key 2 stands for
```
run()V
```
.

The line starting with 0 is the thread, type, and size (TTS) of an allocation.  The
remaining lines starting with + are stack frames (SFs), containing function signatures.
Lines starting with = are stack traces (STs), and are again key, value pairs.  In the
example above, an ST called 16 is the TTS plus sequence of SFs.  Any line not starting
with + or = is a sample.  It is a reference to an ST.  Hence repeated samples are
represented as just numbers.

#### ART
>    `art -Xplugin:$ANDROID_HOST_OUT/lib64/libopenjdkjvmti.so '-agentpath:libtiallocsample.so=100' -cp tmp/java/helloworld.dex -Xint helloworld`

* `-Xplugin` and `-agentpath` need to be used, otherwise the agent will fail during init.
* If using `libartd.so`, make sure to use the debug version of jvmti.

>    `adb shell setenforce 0`
>
>    `adb push $ANDROID_PRODUCT_OUT/system/lib64/libtiallocsample.so /data/local/tmp/`
>
>    `adb shell am start-activity --attach-agent /data/local/tmp/libtiallocsample.so=100 some.debuggable.apps/.the.app.MainActivity`

#### RI
>    `java '-agentpath:libtiallocsample.so=MethodEntry' -cp tmp/helloworld/classes helloworld`
