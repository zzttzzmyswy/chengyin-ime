# Repository guidance

This is a long-term Chinese input method project. Read `docs/STATUS.md` and the
relevant milestone in `docs/ROADMAP.md` before continuing implementation.

User decisions: full pinyin first, double pinyin later. Linux desktop was the
initial priority; the user now has Windows available, so the current priority is
a Windows x64 TSF implementation delivered as a single installer EXE. The user
has few opportunities to test: finish substantial feature batches and automated
checks before requesting consolidated desktop validation. The first consolidated
build must include daily vocabulary, whole-sentence input, paging, middle editing,
segment selection, Windows settings and host candidate UI; do not hand off
smaller demo builds for user testing. Do not block further
development on feedback from small previews. Retain a shared core for
Windows, Linux X11, Linux Wayland, and Android.

- Keep core logic independent of platform I/O and GUI libraries. Share immutable
  dictionaries, never mutable composition sessions between input contexts.
- Preserve the C ABI ownership and event/commit contracts in `include/myswy_ime.h`.
- Measure performance; report hardware, corpus, scope and percentiles. Keep core
  microbenchmarks separate from platform/UI latency and input quality.
- Native compilation, headless tests and real desktop/device validation are
  distinct evidence. Update status honestly after each usable development slice.
- Run relevant checks (`scripts/check.sh` for Linux core/ABI; CMake/CTest for
  Fcitx changes; Windows CMake/CTest for TSF changes). Do not make tests depend on a user's real input history.
- Current repo: https://github.com/zzttzzmyswy/myswyIm, main branch. Read
  docs/WINDOWS_TASKLIST.md and docs/DEVELOPMENT.md for preview7: classic native
  tabs, embedded input tests, configuration notifications, language-bar mode and
  Chinese punctuation. Keep the standalone test app removed. The settings
  executable is myswy_settings.exe; package scripts use preview7. Environment
  reproduction is in docs/ENVIRONMENT.md.
- Record source and license for any imported vocabulary or corpus. The bundled
  98-entry demo dictionary is not a production vocabulary.
- Documentation is primarily Chinese. Keep STATUS's next work items current.
