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
  docs/WINDOWS_TASKLIST.md and docs/DEVELOPMENT.md for historical preview7: classic native
  tabs, embedded input tests, configuration notifications, language-bar mode and
  Chinese punctuation. Keep the standalone test app removed. The settings
  executable is myswy_settings.exe; package scripts use preview21; read docs/WINDOWS_PREVIEW21_TASKLIST.md. Environment
  reproduction is in docs/ENVIRONMENT.md.
- Previous delivery: preview7 MSVC installer from commit b14116e, GitHub Actions run
  37212769471. All six jobs passed, including six native Windows CTests and the
  installer lifecycle. Native NSIS needs the pinned AMD64 preparation helper
  and explicit UTF-8 source input. Real Win11/Notepad3 validation is still pending.
- This Windows preview8–16 phase is closed as of 2026-10-05. Read
  docs/WINDOWS_PHASE_WRAPUP.md for final delivery, local integration and retained
  validation gaps; start a new codex/ branch for new feature work.
- Current slice: preview21 adds self-contained custom candidate skins, six presets,
  PNG/color/radius/padding/motif editing, immutable configuration snapshots and
  bounded scaled-art caches. The user explicitly reopened character themes;
  final whale art is the unchanged transparent Q-version avatar from the user's
  TreapGoGo website. Keep its attribution separate from the code license.
  86 Rust Release tests, C ABI, eight native CTests and package verification pass.
  Personal install and physical mixed-DPI remain unverified. Read docs/SKINS.md and
  docs/WINDOWS_PREVIEW21_TASKLIST.md; ordinary profile review B–E remains pending.
- Previous slice: preview20 prioritizes characters for complete single syllables,
  retaining word completion and per-lane history. Restricted TSF activation uses
  only the embedded dictionary and in-memory defaults, without personal files,
  learning, associations or settings UI; SECUREMODE is registered/unregistered.
  86 Rust Release tests, C ABI and seven native CTests pass. Installed Explorer
  still loads preview7; upgraded WinUI address/search validation is pending.
  Read docs/WINDOWS_PREVIEW20_TASKLIST.md; do not equate fixtures with live hosts.
- Latest quality slice: preview19 requires three recent hits for unattested learned
  text, preserving dictionary-attested legacy preferences. Read the preview19 tasklist.
  82 Rust Release tests, C ABI and seven native CTests pass. Several real processes
  still load preview12/7; binary upgrades require reopening those hosts.
- Current review slice: preview18 fixes separated digraphs, word-cardinality
  parsing, trailing terminal separators, per-profile destructive generations,
  strict release asset/version binding and isolated fixture notifications.
  77 Rust tests, C ABI and seven native CTests pass. Source ad84c9d is on main;
  Actions 37314439254 passed all six jobs and the isolated installer lifecycle.
  Read docs/CODE_REVIEW_2026-10-05.md, docs/REVIEW_REPAIR_PLAN.md and
  docs/CONTEXT_HANDOFF.md. Normal profile synchronization and bounded writer
  retries are the next batch; do not confuse destructive generation with revision.
- Previous slice: preview17 gives complete one-letter-per-character abbreviations
  priority over shorter fuzzy/typo interpretations, with complete word pagination.
  73 Rust tests and seven native Windows CTests pass; read the preview17 tasklist.
- Closed-phase final slice: preview16 adds accurate lexical prefix phrase/character choices
  after whole matching. Windows opts into immediate prefix commits and rematches
  remaining pinyin with a shortened TSF composition range. Legacy core/C ABI
  default staged selection is unchanged; configure_incremental is additive.
  Prefix learning is acknowledged only after host acceptance; failures after a
  text write must consume the choice and recover a valid caret.
  67 Rust tests, zero-key-allocation/64 KiB, native C ABI and seven CTests pass.
  Package, complete payload hashes/COM and licenses verified; personal install unchanged.
- Preview15 protects complete lexical input spans and constrains
  sentence boundaries using attested words/authored links/limited pronoun frames.
  Unknown exact phrase joins are at most one boundary and one fallback candidate;
  fuzzy/corrected/abbreviated/predicted paths require supported boundaries.
  Unknown history needs three selections before promotion; recent low-hit history
  loses promotion before actual forgetting. Dictionary replacement invalidates
  the per-session history cache. The shared derived Unicode word index is immutable.
  61 Rust tests, zero-key-allocation/64 KiB, C ABI and seven native CTests pass.
- Preview14 expands learning to 8192 records / 4 MiB; reads legacy
  MSWYUSR1, writes MSWYUSR2 with recent selection hits/opportunities and decay.
  Independent per-session history-query caches adapt active capacity 4–16;
  full keys/options/text budget identify entries, profile mutations invalidate.
  Forgetting uses recent hit rate, never keystrokes or unconfirmed host writes.
  53 Rust tests, zero-key-allocation/64 KiB, C ABI and seven native CTests pass.
  Preview13 separates whole-word and whole-character recall;
  each lane has at most two exact and two fuzzy history promotions. No prefix
  characters pollute whole-word recall. Unattested adjacent-character synthesis
  is rejected, short full spelling avoids abbreviation synthesis, and correction
  distance participates in ranking. Profile binary format and C ABI remain compatible.
  Preview12 adds preference-file change fallback and directly
  refreshes visible candidates without a host edit lock. Explicit candidate
  pinyin is default off, including correction annotations; legacy default-on
  preferences migrate to off without losing other fields. Seven native CTests
  cover existing cross-process consumers, failed notification mapping and
  denied edit locks; screenshots render only fixture windows with PrintWindow.
  Binary upgrades still require reopening processes that loaded an older DLL.
  Preview11 labels universal keyboard-correction examples explicitly;
  matching is not limited to zhang. Cross-pinyin regression covers hao/ping/shi/ni'hao.
  Preview10 removes the three character themes, retaining system/white/black.
  Configurable phonetic pairs and bounded swap/omission/QWERTY/repetition matching
  show canonical per-letter candidate annotations; default matching flags are zero.
  Native seven CTests, 43 Rust tests, C ABI, 64 KiB/zero-key-allocation checks and
  package byte-exact/manifest/license checks pass. No new remote CI claim.
  Preview10 installed manifest/COM and a fresh Notepad3 DLL load pass. Real
  Notepad3/Edit/RichEdit correction annotations, raw Space, paging and middle
  insertion pass; idle Notepad3 Shift ^&J pass. Temporary matching flags restored
  to zero, learning hash unchanged. See WINDOWS_PREVIEW10_DESKTOP_VALIDATION.
  Full modern application matrix and physical mixed-DPI remain pending.
  Installed preview8 Notepad3 reveals Shift pass-through and trailing Space limits;
  preview9 fixes idle Shift test interception. See STATUS for exact evidence.
  Prior preview8 local Win11/MSVC seven CTests, Rust checks and real
  Edit/RichEdit input pass; package produced locally, no new remote CI claim.
  Physical mixed-DPI and full application matrix remain separate validation.
- Record source and license for any imported vocabulary or corpus. The bundled
  98-entry demo dictionary is not a production vocabulary.
- Documentation is primarily Chinese. Keep STATUS's next work items current.
