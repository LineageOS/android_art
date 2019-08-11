# fieldcount

fieldcount is a JVMTI agent designed to investigate the types being held by specific fields and
how large the objects referenced by these fields are.

Note that just by using the agent some fields might be written (for example fields related to
keeping track of jfieldIDs). Users should be aware of this.

# Usage
### Build
>    `m libfieldcount libfieldcounts`

The libraries will be built for 32-bit, 64-bit, host and target. Below examples
assume you want to use the 64-bit version.

### Command Line

The agent is loaded using -agentpath like normal. It takes arguments in the
following format:
>     `Lname/of/class;.nameOfField:Ltype/of/field;[,...]`

#### ART
```shell
art -Xplugin:$ANDROID_HOST_OUT/lib64/libopenjdkjvmti.so '-agentpath:libfieldcount.so=Ljava/lang/Class;.extData:Ldalvik/system/ClassExt;,Ldalvik/system/ClassExt;.jmethodIDs:Ljava/lang/Object;' -cp tmp/java/helloworld.dex -Xint helloworld
```

* `-Xplugin` and `-agentpath` need to be used, otherwise the agent will fail during init.
* If using `libartd.so`, make sure to use the debug version of jvmti.

```shell
adb shell setenforce 0

adb push $ANDROID_PRODUCT_OUT/system/lib64/libfieldcounts.so /data/local/tmp/

adb shell am start-activity --attach-agent '/data/local/tmp/libfieldcounts.so=Ljava/lang/Class;.extData:Ldalvik/system/ClassExt;,Ldalvik/system/ClassExt;.jmethodIDs:Ljava/lang/Object;' some.debuggable.apps/.the.app.MainActivity
```

#### RI
>    `java '-agentpath:libfieldcount.so=Lname/of/class;.nameOfField:Ltype/of/field;' -cp tmp/helloworld/classes helloworld`

### Printing the Results
All statistics gathered during the trace are printed automatically when the
program normally exits. In the case of Android applications, they are always
killed, so we need to manually print the results.

>    `kill -SIGQUIT $(pid com.littleinc.orm_benchmark)`

Will initiate a dump of the counts (to logcat).

The dump will look something like this.

```
dalvikvm64 I 06-27 14:24:59 183155 183155 fieldcount.cc:60] listing field Ljava/lang/Class;.extData:Ldalvik/system/ClassExt;
dalvikvm64 I 06-27 14:24:59 183155 183155 fieldcount.cc:60] listing field Ldalvik/system/ClassExt;.jmethodIDs:Ljava/lang/Object;
Hello, world!
dalvikvm64 I 06-27 14:24:59 183155 183155 fieldcount.cc:97] Dumping counts of fields.
dalvikvm64 I 06-27 14:24:59 183155 183155 fieldcount.cc:98]     Field name      Type    Count   Total Size
dalvikvm64 I 06-27 14:24:59 183155 183155 fieldcount.cc:155]    Ljava/lang/Class;.extData:Ldalvik/system/ClassExt;      <ALL TYPES>     2800    3024
dalvikvm64 I 06-27 14:24:59 183155 183155 fieldcount.cc:161]    Ljava/lang/Class;.extData:Ldalvik/system/ClassExt;      Ldalvik/system/ClassExt;        64      3024
dalvikvm64 I 06-27 14:24:59 183155 183155 fieldcount.cc:161]    Ljava/lang/Class;.extData:Ldalvik/system/ClassExt;      <null>  2738    0
dalvikvm64 I 06-27 14:24:59 183155 183155 fieldcount.cc:155]    Ldalvik/system/ClassExt;.jmethodIDs:Ljava/lang/Object;  <ALL TYPES>     63      10008
dalvikvm64 I 06-27 14:24:59 183155 183155 fieldcount.cc:161]    Ldalvik/system/ClassExt;.jmethodIDs:Ljava/lang/Object;  <null>  26      0
dalvikvm64 I 06-27 14:24:59 183155 183155 fieldcount.cc:161]    Ldalvik/system/ClassExt;.jmethodIDs:Ljava/lang/Object;  [J      39      10008
```
