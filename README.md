# ppstep
The C and C++ preprocessors are famous for allowing users to write opaque, hard-to-follow code through macros. These downsides are tolerated because macros also allow code to have a high level of abstraction, which can reduce software complexity and improve developer productivity. Doing the good parts takes dicipline; eliminating the bad parts takes refactoring, and importantly, debugging. PPstep is the debugger that macro writers can use to do that.

<img src="https://raw.githubusercontent.com/notfoundry/ppstep/master/assets/demo.svg"/>

<p align="center">
  preprocessing a sequence into a tuple, visualized!
</p>

## Features
- Visually single-step through macro expansion and rescanning until preprocessing is complete
- Set breakpoints on macros for specific preprocessing events, and continue preprocessing between them
- Show backtrace of pending macro expansions, and forward-trace of future macro rescans
- #define/#undef macros mid-preprocessing, and interactively expand macros at any time

### Features In Progress
- Better visual representation of preprocessing steps, coloring tokens in output and explaining event types and causes
- Reverse stepping to rewind preprocssing and view steps from an earlier point
