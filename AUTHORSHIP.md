# Statement of Authorship & Process

## Architectural Origin

**minimidio** is conceived and directed by Joseph Stewart / octetta.

The project's defining constraint is practical minimalism: provide MIDI
input/output as a single C header that can be copied into a project, while
still presenting a consistent API across native and web targets.

Its core shape is deliberately small:

* a `miniaudio`-style `#define MINIMIDIO_IMPLEMENTATION` integration model
* native backends for CoreMIDI, WinMM, and ALSA sequencer
* a Web MIDI backend for Emscripten builds
* virtual MIDI ports where the host platform supports them
* focused examples for monitoring, output, through-routing, DAW sync,
  virtual ports, and Web MIDI testing

## Collaborative Process

The implementation was developed through a human-directed, AI-assisted
pair-programming workflow.

The human author supplied the project goals, constraints, taste, testing
feedback, and final direction. The AI assistant helped draft C code, inspect
platform behavior, adapt examples, diagnose backend issues, and iterate on
the documentation under human review.

In practice, the workflow looked like this:

* **Human direction**: defining the library's scope, single-header design,
  platform expectations, and examples worth shipping.
* **AI synthesis**: translating those goals into backend code, examples,
  build notes, and documentation patches.
* **Iterative validation**: compiling examples, testing on real Linux browser
  setups, identifying Emscripten runtime compatibility issues, and refining
  the implementation from observed failures.

This document acknowledges the AI-assisted fabrication process while
affirming that the creative direction, authorship decisions, and project
ownership reside with the human author.
