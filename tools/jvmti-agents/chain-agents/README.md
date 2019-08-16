# chainagent

The chainagents agent is a JVMTI agent that chain loads other agents from a file found at a
location relative to a passed in path. It can be used in combination with android startup_agents
in order to implement more complicated agent-loading rules.

It will open the file `chain_agents.txt` from the directory passed in as an argument and read it
line-by-line loading the agents (with the arguments) listed in the file.

Errors in loading are logged then ignored.

# Usage
### Build
>    `m libchainagents`

The libraries will be built for 32-bit, 64-bit, host and target. Below examples
assume you want to use the 64-bit version.

### Command Line
#### ART
>    `art -Xplugin:$ANDROID_HOST_OUT/lib64/libopenjdkjvmti.so -agentpath:$ANDROID_HOST_OUT/lib64/libchainagents.so=/some/path/here -Xint helloworld`

* `-Xplugin` and `-agentpath` need to be used, otherwise libtitrace agent will fail during init.
* If using `libartd.so`, make sure to use the debug version of jvmti.

### chain_agents.txt file format.

The chain-agents file is a list of agent files and arguments to load in the same format as the
`-agentpath` argument.

#### Example chain_agents.txt file

```
/data/data/com.android.launcher3/code_cache/libtifast32.so=ClassLoad
/data/data/com.android.launcher3/code_cache/libtifast64.so=ClassLoad
```
