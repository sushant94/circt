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
