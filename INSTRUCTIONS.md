# Instructions

## Installation

- Make sure to install a debug version and a release version (you can have two different build directories, or just use 2 different top-level directories if you want)
  - The debug version runs slower, but allows you to debug (it has more details when you run into errors)
  - The release version is _significantly_ faster but does not print in as much detail when you segfault

## Flow

1. Generate `.fir` in Chipyard (`sims/verilator` -> `make debug CONFIG=<config>`
2. Delete annotations (inside `%[[` to the corresponding closing brackets `]]`)
3. Change the module from `TestHarness` to what you want to look at (`Core` for `Sodor1StageConfig`)
4. Remove all the `printf` statements -- you can do this with the following command: `sed -i '$!N;/\n[[:space:]]*printf/d;P;D' <file.fir>`

## To-do

- [ ] Try using the release with debug version

## Where things are

- The place to make it not flatten: `lib/Firtool/Firtool/cpp:441`
  - When you do this, try and output MLIR versions (try and get the one after each pass)
- The place to handle modules: `lib/Conversion/HWToBTOR2/HWToBTOR2.cpp:runOnOperation` (end of file)
  - You want something that walks over modules, and then for each individual

```
Log executions of toplevel module passes

intDebugInfo, disallowExpressionInliningInPorts, disallowMuxInlining, emitWireInPort, emitBindComments, omitVersionComment, caseInsensitiveKeywords
  --output-final-mlir=<filename>                             - Optional file name to output the final MLIR into, in addition to the output requested by -o
  --output-hw-mlir=<filename>                                - Optional file name to output the HW IR into, in addition to the output requested by -o
```


### How things connect to each other

- Look at another circt pass (maybe HW to SMT for example, or just an easier one)
- For how they go over modules

### General thoughts for how to edit this

- 
