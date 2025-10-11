```
┌───────────────────────────────┐
│           term.c              │
│ ───────────────────────────── │
│ • forkpty() → starts zsh      │
│ • read() from masterfd (PTY)  │
│ • utf8decode() (text parsing) │
│ • main loop                   │
│   → window_init()             │
│   → window_clear()            │
│   → window_swap()             │
│   → window_poll()             │
│                               │
│ Acts as:                      │
│   - Terminal core             │
│   - Glue code for rendering   │
│                               │
│ Data flow:                    │
│   - Reads shell output        │
│   - Prints or draws it        │
│   - Calls into window.c       │
└───────────────┬───────────────┘
                │
                │ draw background / handle input
                ▼
┌───────────────────────────────┐
│           window.c            │
│ ───────────────────────────── │
│ • Initializes GLFW + OpenGL   │
│ • Opens 800×600 window        │
│ • Clears screen, swaps buffer │
│ • Polls GLFW events           │
│ • (later: key → write to PTY) │
│                               │
│ Acts as:                      │
│   - Platform abstraction      │
│   - Rendering + input layer   │
│                               │
│ Data flow:                    │
│   - Receives draw commands    │
│   - Sends keyboard events →   │
│     term.c (to write to PTY)  │
└───────────────────────────────┘
```
