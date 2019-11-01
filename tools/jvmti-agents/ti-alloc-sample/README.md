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
#20(VMObjectAlloc(#0(jthread[main], jclass[Ljava/lang/String; file: String.java], size[56, hex: 0x38
> ]))
    #1(nativeReadString(J)Ljava/lang/String;)
    #2(readString(Landroid/os/Parcel;)Ljava/lang/String;)
    #3(readString()Ljava/lang/String;)
    #4(readParcelableCreator(Ljava/lang/ClassLoader;)Landroid/os/Parcelable$Creator;)
    #5(readParcelable(Ljava/lang/ClassLoader;)Landroid/os/Parcelable;)
    #6(readFromParcel(Landroid/os/Parcel;)V)
    #7(<init>(Landroid/os/Parcel;)V)
    #8(<init>(Landroid/os/Parcel;Landroid/view/DisplayInfo$1;)V)
    #9(createFromParcel(Landroid/os/Parcel;)Landroid/view/DisplayInfo;)
    #10(createFromParcel(Landroid/os/Parcel;)Ljava/lang/Object;)
    #11(getDisplayInfo(I)Landroid/view/DisplayInfo;)
    #11
    #12(updateDisplayInfoLocked()V)
    #13(getState()I)
    #14(onDisplayChanged(I)V)
    #15(handleMessage(Landroid/os/Message;)V)
    #16(dispatchMessage(Landroid/os/Message;)V)
    #17(loop()V)
    #18(main([Ljava/lang/String;)V)
    #19(invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;))
```

The first line tells what thread the allocation occurred on, what type is
allocated, and what size the allocation was.  The remaining lines are the call
stack, starting with the function in which the allocation occured.  The depth
limit is 20 frames.

String compression is rudimentary.

```
    #1(nativeReadString(J)Ljava/lang/String;)
```

Indicates that the string inside the parenthesis is the first entry in a string
table.  Later occurences in the printout of that string will print as

```
    #1
```

Stack frame entries are compressed by this method, as are entire allocation
records.


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
