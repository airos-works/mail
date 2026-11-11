# Implementation status

The requested Mail development implementation is delivered. See
[acceptance and limits](ACCEPTANCE.md) for the feature-by-feature handoff and
the boundary between completed implementation, live-provider qualification and
future enhancements. No earlier Mail project existed under `apps` when work
began on 2026-10-21. The evidence below is chronological: earlier limitations
may be resolved by later entries.

## Evidence collected

- `build-host/mail_core_tests`: 409 assertions pass with address and undefined
  behavior sanitizers, plus 500 deterministically mutated MIME inputs.
- Verified scenarios: account isolation, unified-inbox opt-out, out-of-order
  threading and thread merges, authoritative provider thread IDs, idempotent
  ingestion, local edits surviving re-ingestion and restart, UTC snooze expiry,
  Unicode full-text search, query escaping, smart-folder persistence, ordered
  account-specific filters, transaction rollback, and binary attachments.
- MIME fixtures cover nested alternatives/mixed parts, encoded/folded subjects,
  legacy body encodings, emoji, quoted-printable soft breaks, inline CID images,
  RFC2231 single extended filenames, leaf-name safety, HTML text fallback,
  invalid base64, incomplete multiparts, and bounded headers.
- Real socket fixtures verify IMAP and POP3 over implicit TLS and STARTTLS,
  certificate rejection, authentication failure, cancellation, literal response
  framing, modified UTF-7/literal mailbox names, UIDVALIDITY, BODY.PEEK, flags,
  MOVE/COPYUID, POP3 UIDL/retention/dot transparency, SMTP envelopes and DATA.
  All 37 protocol assertions pass on Linux and the native air/OS image. The
  fixture also independently checks encrypted authentication and SMTP delivery.
- Store checks cover queued local edits, stale acknowledgements, move identity
  preservation, remote deletion, POP3 local organization, account removal,
  long folder names, schema 1/2/3/4 migration, and distinct credential identifiers
  that survive edits/restart without leaking across databases or reused row IDs.
- `bash tools/cross-build.sh` successfully compiles and links the core tests and
  native x86_64 Haiku Mail executable with warnings treated as errors. Resources
  are embedded by the Haiku `rc` and `xres` tools.
- Icon generated as a 383-byte HVIF, codec round-trip checked, host preview
  visually inspected. The native smoke test loads the actual Mail executable's
  embedded vector resource and renders it through BIconUtils at 16/32/64 pixels.
- An existing x86_64 Haiku VM process was live, but SSH on port 2225 timed out
  during banner exchange. It was not restarted or modified.
- A separate disposable VM booted from a copy-on-write overlay of the existing
  air/OS image (Haiku hrev60206+1145, x86_64, SQLite 3.50.4). Both core and native
  UI tests pass there. The UI test imports an actual `.eml` through the window's
  refs handler, reads it, resizes, archives/restores, and searches/clears search.
  [Native screenshot](evidence/native-inbox.png) was captured by BScreen and
  visually inspected. The original image exposed a missing emoji glyph;
  the embedded font work below now covers that reader/composer case.
- Native execution revealed that this workspace's cross GCC links a static
  exception unwinder by default. Explicit shared `gcc_s` linkage fixed failing
  exception catches. Core tests now pass on both Linux and air/OS.
- The actual `Mail --database /boot/home/mail-check/app-gmail-cache.db` executable was
  launched, its window title queried through `hey`, and quit cleanly. No errors
  appeared in its log. This proves startup, not account connectivity.
- A separate native account UI test now verifies Bridge presets, masked password
  fields, account add/edit, blank-password preservation, BKeyStore save/update/
  load/remove, and actual background IMAP reception after closing Accounts.
  Disposable key-ring access prompts were answered in the test VM. The native
  [account window](evidence/native-accounts.png) was captured and inspected.
  A layout width issue found there was fixed. Background polling is suspended
  while account settings are open, preventing removal/edit races with sync.
- Composition/outbox tests now cover 111 assertions on both Linux and air/OS: Unicode
  MIME, folded headers, continued filenames, binary attachments, BCC privacy,
  safe links, reply-all/Reply-To, drafts, queue immutability, exclusive claiming,
  restart recovery, explicit uncertain retries, and atomic local Sent creation.
  Python's independent email parser checks the actual generated wire message.
- Protocol fixtures now include SMTP STARTTLS, recipient rejection before DATA,
  and connection loss after DATA is accepted. The outbox records uncertainty
  and does not send the message again on a later delivery pass.
- Native account tests also drive the composer: headers, bold text, an emoji,
  actual file/image refs drops, save/close/reopen, Send, and local Sent creation.
  The server independently verifies one SMTP delivery, HTML formatting, private
  BCC, Unicode, and both attachments. The [composer capture](evidence/native-composer.png)
  was inspected. A B_REGULAR_FACE/B_BOLD_FACE conflict found on target was fixed.
  An edit immediately followed by Close is saved even if its change notification
  is still queued. The current composer capture includes the embedded emoji
  glyph; the original capture had shown a missing-glyph box.
- Google transport adds 94 host/native assertions. A temporary HTTPS fixture
  independently verifies authorization-code exchange, S256 PKCE, unique state,
  a random loopback port, rejection of forged/duplicate state and wrong Host or
  method, denied/insufficient consent, refresh-token preservation, revocation,
  browser cancellation/timeouts, TLS validation and redirect rejection.
- Gmail API checks cover paginated message lists, raw/minimal retrieval,
  multiple labels per message, string-preserved history IDs, deduplicated
  history events, 404 propagation, label creation/mutation and trash/untrash.
  Sending preserves a BCC-free local copy and creates the necessary BCC header
  only for Google's API. Python independently checks recipients, Unicode,
  HTML, attachments, thread reply headers, and BCC-only mail. A new BCC-only
  test found and fixed handling of the composer's empty recipient group.
- The Google client refreshes once following an explicit HTTP 401. A lost
  upload response, HTTP 5xx send result, or unusable successful receipt reports
  uncertain delivery without retry. Fixtures verify one submission per attempt,
  bounded JSON/base64 responses, and no server-body/token leakage in errors.
  OpenSSL supplies random bytes and SHA-256; nlohmann/json 3.12.0 is vendored
  unmodified with its MIT license and checksum. The Haiku build links libcrypto
  and libnetwork. Existing native core/composition/reader smoke checks still pass.
- Gmail storage adds 45 host/native assertions. Schema 5 stores label definitions,
  membership and history cursors while preserving older mail, tags and drafts.
  One message can appear in Inbox, Sent and multiple label views; trash/spam
  suppress ordinary views. Pending read/star/move/tag changes survive older
  snapshots and stale acknowledgements. Label rename/deletion, account isolation,
  restart, checkpoint rollback and removal cascades are covered.
- A separate stateful HTTPS fixture adds 36 host/native sync/outbox assertions.
  It checks full/history pagination, body reuse, expired-history recovery, failed
  and repeated pages, confirmed deletions, a Sent copy created during receive,
  automatic rules, label-creation conflicts and missing-label 404s. Receive rules
  do not act on historical Sent/Draft/Spam/Trash downloads. A mutation's 404 must
  be confirmed by message lookup before hiding cached mail.
- Gmail delivery now plugs into the core outbox without SMTP fallback. It checks
  profile identity before sending and atomically records Gmail's message/thread
  receipt. Later history reuses that Sent row, including when receive already
  downloaded it before send finalization. The fixture verifies BCC, one submission
  after an interrupted response, explicit failure and cancellation.
- Native Google account setup, BKeyStore tokens and receive/send workers are now
  connected. The Accounts window launches the preferred system browser, verifies
  the Gmail profile, saves authorization, and supports cancel, reauthorize and
  explicit revoke. Client configuration comes from a Desktop JSON in the user
  settings directory or beside the executable; endpoint overrides are ignored.
- `mail_native_google` passes on air/OS against a disposable OAuth/Gmail HTTPS
  fixture. It drives actual Accounts/composer windows through sign-in, metadata
  editing, cancellation on close, wrong-account rejection, duplicate identity
  reconnection, background receive, reply/send, Sent reconciliation, revoke and
  reconnect. Concurrent workers share one expiry refresh and one forced refresh
  after HTTP 401. Tokens round-trip through the native key store and are absent
  from SQLite database/WAL files. Five authorizations, two refreshes, one revoke,
  one send and two raw downloads are independently checked by the server.
- The [Google account capture](evidence/native-google.png) was inspected. A
  read-only BTextView/BCardView height issue clipped the second help line; a
  stable status area fixes it, with a native bounds assertion. The browser is
  simulated for this fixture; Google's live consent screens remain unverified.
  [Google setup instructions](GOOGLE_SETUP.md) cover registration, client-file
  installation, Testing token expiry, revocation and a manual live check.
- After native Google integration, all seven host CTest suites pass with
  sanitizers. Native core (356 assertions plus 500 mutated MIME inputs), Gmail
  storage (45), composition (30), reader/icon smoke and the IMAP/SMTP
  Accounts/composer fixture pass again. The actual Mail executable launches
  with an empty disposable database, reports its window title and exits cleanly.
- Mail now embeds the unmodified Noto Emoji variable font and its OFL license,
  with source revision and checksums in [third_party](../third_party/README.md).
  BFont loads it as a private application font from an executable resource;
  no system font is installed. Reader/composer runs, subject input, the reader
  title, conversation summaries and the emoji menu use its monochrome glyphs.
  Text measurements and truncation use the same fonts as drawing.
- Native typography checks cover 14 actual font glyphs, semantic bold/italic/
  underline on emoji, ordinary typing after emoji, subject rendering, draft
  persistence, clipboard export without private font IDs, paste/undo/redo,
  formatted MIME and coalescing presentation fonts out of stored style runs.
  The native suite reports 55 checks. The [typography capture](evidence/native-typography.png)
  and updated reader/composer images were inspected. A packaging regression
  found by the smoke test was fixed: one resource injection now includes both
  the font/license and existing signature/version/icon resources.
- After typography integration, all seven host suites and native reader/icon,
  typography, IMAP/SMTP Accounts/composer and Google Accounts/composer fixtures
  pass. Native reader and reopened composer checks now require real emoji
  glyphs from the embedded face, in addition to verifying Unicode text.
- Delivery recovery now has a persistent Outbox button with total and attention
  counts. Receive progress, search and mailbox refreshes cannot overwrite it.
  Counts and list previews avoid reading MIME bodies or attachments. Outbox
  opens as one window, preserves selected IDs across updates, and disables
  actions that are unavailable for queued, sending or empty selections.
- `mail_native_outbox` drives actual buttons against temporary SMTP/IMAP servers:
  rejection, lost acknowledgements, successful explicit retries, Cancel/Confirm
  for both uncertain actions, rich draft recovery, and a click racing a worker's
  claim. Confirmations default to Cancel and explain duplicate-delivery or recall
  consequences. Returned drafts preserve BCC, links, formatting and attachments.
  The independent server verifies exactly four DATA submissions, immutable retry
  bytes and no resend from later receive checks. The [Outbox capture](evidence/native-outbox.png)
  was visually inspected.
- Recovery checks and updates use one SQLite transaction. Twenty-four concurrent
  claim/return races through separate database connections prove exactly one
  outcome wins; a confirmed recovery cannot remove or requeue an active send.
  Counts survive database reopening and exclude completed delivery history.
- After delivery recovery integration, all seven host suites pass with sanitizers.
  The final native build passes core (356 plus 500 mutated MIME inputs), Gmail
  storage (45), composition/outbox (87), reader/icon/layout smoke, typography (55),
  and the new Outbox fixture. The Accounts and Google UI fixtures passed in the
  preceding typography milestone; their rebuilt binaries were not rerun here.
- Attachment lists now initiate native negotiated copy operations. Each drag
  owns a bounded immutable snapshot and worker looper, so later selection changes
  or draft edits cannot change its bytes. Tracker receives the original file and
  MIME attribute in its reserved destination; populated files and symlinks are
  rejected. No temporary exported mailbox files are left on disk for canceled
  drags. Mail-to-Mail negotiation also attaches received files directly to drafts;
  dropping an image into the editor creates a new inline CID. The file protocol
  follows Haiku's [negotiated drag documentation](https://www.haiku-os.org/legacy-docs/bebook/DragAndDrop.html).
- Reader and composer image previews use the native Translation Kit on a worker
  thread. PNG dimensions, pixel channels and partial alpha are checked against
  an independently generated four-color fixture. Malformed/truncated images and
  cancellation return controlled failures. Output is capped at 64 MiB, dimensions
  at 8192 per axis, and the validated stride bounds native bitmap allocation.
  Previews fit without upscaling and survive removal of the source attachment.
  Attachment rows and the preview filename use the embedded emoji font.
- `mail_native_attachments` passes native export, cross-window negotiation,
  selection controls and preview checks, then performs a real QEMU pointer drag
  onto Tracker's desktop and checks exact file bytes. The [preview capture](evidence/native-attachments.png)
  was visually inspected. The optional named-FIFO destination check is explicitly
  skipped because this air/OS filesystem reports it unsupported; populated-file
  and symlink cases run. Cleanup removes the symlink before its target because
  this Haiku libstdc++ `remove_all` leaves the dangling link behind.
- After attachment integration, all seven host suites and the final native
  core/storage/composition, reader/icon/layout, typography, attachment and
  IMAP/SMTP Accounts/composer checks pass. The SMTP fixture independently confirms
  one rich delivery with private BCC and both attachments. The current build's
  Outbox and Google UI binaries were rebuilt but not rerun in this milestone;
  their earlier execution coverage is recorded above.

- Schema 6 adds per-account Sent policy/folder settings and a separate durable
  Sent-copy journal. SMTP acceptance commits the local message and copy work
  atomically. Existing schema 1–5 mail, organization and drafts are retained;
  historical Sent messages are not retroactively uploaded. Core migration tests
  now total 364 assertions plus the malformed MIME corpus.
- IMAP checks search for an exact Message-ID before APPEND, use synchronizing
  literals, preserve the delivery time and mark copies read. APPENDUID binds
  the stable local message to the server identity. A positive reply without a
  UID is recorded as accepted and searched later. Lost or malformed replies
  remain unconfirmed; ordinary checks never re-upload them. Definite rejections
  allow corrected folder settings on the next check. Copy failures cannot
  requeue SMTP. The behavior follows [IMAP APPEND](https://www.rfc-editor.org/rfc/rfc9051.html#section-6.3.12)
  and [UIDPLUS receipt/search guidance](https://www.rfc-editor.org/rfc/rfc4315.html#section-3).
- Incoming server Sent copies adopt known locally delivered messages, including
  receive completing before the SMTP receipt. Local tags/read/star/moves and
  stable IDs survive reconciliation; incoming filters do not run on Sent mail.
  Copies are uploaded after mailbox snapshots so an older snapshot cannot hide
  a new upload. Ordinary unrelated messages with equal Message-IDs remain distinct.
- Automatic Sent policy uploads for IMAP, keeps POP3 local, and lets Bridge save
  its own copy, consistent with [Proton's client guidance](https://proton.me/support/protonmail-bridge-clients-windows-outlook-2016).
  Native Accounts exposes the policy and optional exact mailbox name. Missing
  or ambiguous mappings do not cause guessed folder creation. The form fits the
  1280×800 native display and its saved setting passes the Accounts test.
- The new Sent suite passes 96 assertions on host/native, with 14 independent
  SMTP deliveries and seven IMAP literals. Cases include both sides of APPEND
  rejection, lost/invalid/no-UID receipts, delayed visibility, substring search
  mismatches, unstable UIDs, malformed search, custom/missing/ambiguous folders,
  local/server policies, exclusive claims, crash recovery and organization.
  The Python fixture checks exact MIME bytes, BCC privacy and no SMTP resends.
- `mail_native_sent` drives separate delivered/copy status, Cancel and explicit
  retry confirmations, stale actions during an active check and Keep local.
  Its fixture confirms exactly one SMTP delivery and two copy uploads. The
  [Sent recovery capture](evidence/native-sent.png) was visually inspected.
  Accounts/composer and existing Outbox delivery recovery also pass. All eight
  ASAN/UBSAN host suites pass on the final source. Native core (364 plus the
  mutated MIME corpus), Gmail storage (45), composition (87), reader/icon/layout,
  typography (55), Accounts/composer and Outbox passed during this milestone.
  The final folder-retry correction was then rebuilt and verified by both the
  96-check native Sent suite and native Sent recovery UI test. Google UI and
  attachment binaries were rebuilt; their last runtime coverage is recorded above.
  The final Mail executable also opened a fresh schema-6 database, returned its
  native window title, and quit with an empty log. The disposable VM was stopped.

- Schema 7 adds durable Gmail label changes, explicit rule ordering, and
  non-reused rule/smart-folder IDs. Migration checks retain data from schemas
  1–6; stale editors cannot overwrite a replacement created after removal.
  The full core suite now has 372 assertions plus 500 mutated MIME inputs.
- Native **Organize → Rules**, **Smart folders**, and **Tags** windows now
  provide management rather than only creation. Rules support account scope,
  conditions, actions, order, enable/disable and removal. Smart folders expose
  all current query fields. Stable sidebar identity preserves selection when
  a view is renamed and refreshes results when criteria change. Local drafts
  honor applicable smart-view conditions. Unsaved changes use Cancel/Discard;
  removal confirmations default to Cancel and retain messages.
- Account-scoped local tag changes update assigned messages and scoped rule/view
  references. All-account criteria intentionally retain their original names.
  Removing a tag preserves saved-view criteria to avoid broadening results.
  Message-menu removal affects selected conversations. Tag names and sidebar
  labels use the embedded emoji font; the captured missing sidebar glyph was
  corrected during native review.
- Gmail label rename/delete work uses immutable label IDs and a durable journal.
  Newer edits survive older acknowledgements and stale catalogs. Membership
  changes wait for dependent label edits. HTTP failures remain visible; lost
  rename responses retry the same label and delete 404s acknowledge an already
  removed label. Profile identity is verified before mutations. Implementation
  follows Google's [label PATCH](https://developers.google.com/workspace/gmail/api/reference/rest/v1/users.labels/patch)
  and [label DELETE](https://developers.google.com/workspace/gmail/api/reference/rest/v1/users.labels/delete)
  endpoints without deleting messages or requiring another OAuth scope.
- Organization storage passes 39 assertions on host and air/OS. A separate
  HTTPS fixture passes 23 API assertions on both platforms, independently
  checking 12 mutations, retained original messages, and no duplicate labels
  during rename. It covers rejected/lost/mismatched receipts, edits made during
  an older request, wrong-account rejection, externally removed labels and
  cancellation. All ten ASAN/UBSAN host suites pass on the current core.
- The native organization interaction fixture passes rule creation, editing,
  ordering, disabling and removal; real receive-driven Gmail rule actions;
  smart-folder creation/edit/removal, stable selection and local draft criteria;
  selected-conversation tag removal; unsaved Cancel/Discard;
  tag removal Cancel/Confirm; scoped local rename; and Gmail rename/removal of
  both assigned and unused labels. Its independent server observes seven
  mutations and retains the original messages. The rule, smart-folder and tag
  captures were visually reviewed at 1280×800: [rules](evidence/native-organization-rules.png),
  [smart folders](evidence/native-organization-smart.png), and
  [tags](evidence/native-organization-tags.png). The provider is a fixture; no
  live Google client or account is configured.
- Final native regression checks pass core (372 plus 500 mutated MIME inputs),
  Gmail storage (45), organization storage (39), composition/outbox (87),
  reader/icon/layout smoke and typography (55). The expanded organization UI
  test also passes selected-message tag removal and local-draft conditions.
  The rebuilt Mail executable opened a fresh database, returned its native
  window title and quit with an empty log. The owned disposable VM was synced
  and stopped. The other provider, Sent, Accounts and attachment UI executables
  were rebuilt; their earlier runtime evidence is recorded above, rather than
  claimed as rerun during this organization milestone.

- Schema 8 adds a durable snooze-reminder journal with identities that are not
  reused. A new snooze supersedes its old reminder; an older notification
  acknowledgement or Dismiss action cannot erase the replacement. Multi-message
  snoozes commit atomically. Explicit moves and End snooze cancel pending
  reminders. Schema 1–7 migrations pass; active old snoozes are retained without
  notifying every historical expired deadline. Core tests now total 380
  assertions plus the 500-input mutated MIME corpus.
- Native snooze now offers one hour, tomorrow morning, next Monday and custom
  local date/time fields. Invalid calendar dates and skipped local times cannot
  be saved. Repeated hours offer an explicit second-occurrence choice and show
  the UTC offset. Haiku's ICU-backed `mktime` ignores the `tm_isdst` hint, so the
  parser samples nearby offsets and round-trips candidates rather than relying
  on that hint. Presets use local calendar days across offset changes.
- The new snooze suite passes 38 assertions on Linux and air/OS: leap dates,
  Hobart clock gaps/repeats, Lord Howe's half-hour transitions, Apia's skipped
  day, invalid input, elapsed-hour/calendar-day presets, restart persistence,
  atomic batches, stale acknowledgements, account removal and schema migration.
  All eleven host suites passed with ASAN/UBSAN; the final time-conversion
  correction was then verified by the updated snooze suite on both platforms.
- Mail now has a persistent Reminders count, a grouped reminder list, and native
  notifications. Open conversation selects the correct account and thread;
  Snooze again and Dismiss retain messages. Reminders include accounts excluded
  from the unified inbox. Each account also has All mail and Snoozed views.
  Notification clicks target the executable directly, avoiding reliance on a
  pre-existing MIME registration for a portable copy, and pass its database
  location back through the new `--reminders` launch option.
- `mail_native_snooze` passes on air/OS. One process saves future snoozes and
  closes; another starts after expiry. The host physically clicks the real OS
  notification through QEMU and the native app verifies its launch arguments.
  The test drives reminder open/dismiss/reschedule, invalid dates, a skipped
  hour, the second occurrence of a repeated hour, cancellation and early wake.
  It then lets another deadline expire through the ordinary 30-second timer
  while Mail is minimized and verifies the notification and persistent count.
  Captures were reviewed: [custom time](evidence/native-snooze-custom.png),
  [reminders](evidence/native-snooze-reminders.png),
  [notification](evidence/native-snooze-notification.png), and
  [minimized timer](evidence/native-snooze-live.png).
- Native core (380 plus the mutated corpus), snooze (38), Gmail storage (45),
  organization storage (39), composition/outbox (87), reader/icon/layout smoke
  and typography (55) pass during this milestone. No real mail account or
  external recipient was used. The test changes only its own process time zone.
  Other provider and interaction binaries were rebuilt; their earlier runtime
  coverage remains recorded above.
- The actual Mail executable also passes notification-click routing and a
  separate `--reminders --database PATH` reopening check, with both runs exiting
  cleanly and no log errors. A final visual pass replaced the generic portable
  executable icon in notifications with Mail's embedded HVIF and shortened
  account subfolder labels to fit the default sidebar. The final build's
  [Mail notification](evidence/native-snooze-mail.png) was captured and inspected;
  clicking it opened the native reminder list and Mail quit with an empty log.
  The disposable VM was synced and stopped after verification.

- Schema 9 adds persisted inline-image placeholder ranges and CID references.
  The composition layer substitutes images at those positions, supports reuse
  of a single image part, and retains the legacy append behavior for old drafts.
  Validation rejects missing files, damaged placeholders, overlapping ranges,
  UTF-8 splits, overflow and styles that split a placeholder. These are core
  capabilities; the native integration is recorded below.
- Composition now passes 111 assertions on host and air/OS, including image
  layout through reopen, immutable outbox serialization and return-to-draft.
  Python independently checks the exact HTML/text order, escaped Unicode
  alternative text, reused CID, binary payload and legacy fallback. Core checks
  pass 392 assertions plus 500 mutated MIME inputs; schema 1–8 migrations retain
  old drafts, and the schema-8 reminder journal, notification acknowledgement
  and snooze triggers survive the image migration. All eleven host suites pass
  with ASAN/UBSAN. Native snooze (38), Gmail storage (45), organization (39),
  reader/icon/layout smoke and typography (55) also pass in the disposable VM.
  Other native binaries were rebuilt, with prior runtime coverage noted above.
- The requested [Google setup guide](GOOGLE_SETUP.md) was checked against the
  current official documentation and Mail's actual file lookup and account UI.
  It now clarifies setup on another computer, browser callback locality and
  precedence of a stale settings-directory client file. No live Google client
  registration, account consent or external mail was used.

- The native editor now owns body text, links, image ranges and immutable
  attachment snapshots. Body drops capture their position, including negotiated
  transfers that finish after more typing or a caret move. Images are represented
  by named placeholders; Save/reopen and Send preserve their position. Old drafts
  convert trailing inline files to placeholders when opened. Deleting or damaging
  the last placeholder retains its file as a regular attachment, with no silent
  trailing image. Attachment remove/toggle operations update the body as well.
- Native Undo/redo now restores formatting, links, files and image positions
  together; consecutive typing is grouped, and snapshots share attachment bytes.
  Mail clipboard fragments retain link/image semantics and binary files across
  editors, reuse a file for repeated placements in the same draft, assign new
  CIDs in another draft, and retain ordinary text/font data for other apps.
  Partial link edits preserve the surrounding linked text. Formatting selections
  expand over image markers, and failed insertions roll back the full edit.
- `mail_native_composer` passes 95 checks on air/OS: placement, grouped typing, formatting/link undo,
  cut/copy/paste, cross-editor transfer, repeated images, attachment removal and
  toggling, damaged markers, old drafts, failed-edit rollback, pending positions,
  a delayed negotiated transfer, native reopen, Send serialization and delivery
  recovery. The expanded suite checks multilevel undo/redo, branching, failed and
  no-op edits, caret formatting, focus changes, 100-step and 64 MiB limits,
  shared attachment storage, and native Command-Z / Command-Shift-Z dispatch
  in the body and subject field. Haiku's text-run font getter does not expose
  its pending typing style, so the editor tracks that style separately and
  restores it with each history step. Moving the caret adopts the destination
  text's style. Native typography also passes 55 checks with the final editor;
  [the final history and typography log](evidence/native-history.log) records
  both results. Core (392 plus 500 mutated MIME cases), snooze (38), Gmail
  storage (45), organization (39), composition (111), preview snapshots (20),
  and native reader/icon/layout smoke passed earlier in this UI-only turn.
  The [native composer](evidence/native-compose-images.png) was reviewed.
  The account UI fixture also passes: its SMTP server independently verifies one
  rich delivery with the image between the intended words, two attachment parts
  and private BCC. Its [capture](evidence/native-compose-positioned-smtp.png) was
  reviewed. The existing attachment fixture passes 65 checks including a physical
  Tracker drop (FIFO destinations remain unsupported by the target image).

- The composer now has a rendered **Preview message** command (Cmd-P). A
  separate `MailPreview` application uses modern air/OS WebKit with a private
  context. It regenerates delivery HTML, preserves image positions, displays
  headers and regular attachment names, fits wide images and provides zoom,
  Refresh, Edit message and Close controls. Refresh saves the composer and
  replaces its existing preview window. Empty optional headers are hidden.
- Preview transfers use bounded, validated mode-0600 snapshots in a mode-0700
  cache directory. The helper consumes and deletes them before rendering.
  Original attachment bytes and the embedded font are served through exact
  `mail-preview:` resource URLs; links are inert, body markup is escaped, and
  the content policy prohibits network loads and scripts. The helper has no
  database connection or account tokens. A crash before consumption can leave
  a private snapshot in the cache. Received message HTML remains text-only.
- All twelve host suites pass under ASAN/UBSAN, including 20 new preview
  snapshot/HTML checks. Native preview testing passes 49 checks: actual PNG
  pixels, SVG scaling after resize, loaded emoji font, styles and text order,
  inert links, escaped markup, CSP-blocked fetch, transfer permissions/removal,
  document replacement, actual composer-to-companion launch/refresh, Edit
  message and three companion open/close cycles. The independent HTTP fixture
  records zero requests. The [native rendered preview](evidence/native-message-preview.png)
  was visually reviewed. Cache snapshots and renderer processes are absent
  after the final run.
- Native validation found and corrected an overly broad rejection of WebKit
  main-frame resource requests (which include images/fonts). An initial failed
  test also exposed worker teardown during process exit. The companion and test
  now keep the application loop running until owned WebKit helpers exit, then
  allow a final dispatch turn. Repeated close-during-load and normal shutdown
  pass with the final code. The installed runtime emits fontconfig/Mesa fallback
  diagnostics, but software-rendered pixel checks pass. The final cross build
  also passes core (392 plus 500 mutated inputs), snooze (38), Gmail storage
  (45), organization (39), composition (111), preview (20), native reader/icon/
  layout, typography (55) and composer (40) checks. Main Mail has no WebKit
  runtime dependency; only its companion uses the modern engine.

- Native Mail now has authenticated interoperability evidence against the real
  air/OS Proton Bridge engine. `tests/BridgeFixture.go` is overlaid into the
  adjacent Bridge checkout's test package without changing its sources. The
  checked source revision is `f58ad306beff0c0723ef38bf59e83fee7eded55c` with a clean
  tracked worktree. Bridge's production engine/Gluon IMAP/SMTP run in the same
  Haiku VM as Mail's production transport; upstream API, platform helpers and
  keychain are disposable test implementations. No installed Bridge profile,
  personal account, production Proton endpoint or external recipient is used.
- The fixture passes 50 initial Mail checks and 16 after closing/recreating the
  engine and reopening Mail's cache. It covers strict trust of Bridge's actual
  certificate, incorrect-password rejection, standard folder discovery, rich
  SMTP delivery with emoji/positioned CID image/binary file, a separate BCC
  identity, two sending identities, threaded replies, read/star and
  archive/trash/restore. Server-created Sent copies reconcile exactly once;
  reopening neither duplicates Sent nor resends mail. An independent Go IMAP
  client verifies all three identities' Inbox/Sent counts, senders, recipient
  BCC privacy and persisted flags before and after the restart. The final
  [native fixture log](evidence/native-bridge.log) records a passing 5.15-second
  run; the native C++ client and cross-built Go fixture both exit successfully.
- Bridge's certificate names `127.0.0.1`, not `localhost`. Mail's existing
  Bridge preset already uses the correct address. The new
  [Bridge setup guide](BRIDGE_SETUP.md) documents this, the actual advanced
  **Export TLS certificates** control, `cert.pem` versus `key.pem`, ports,
  generated credentials and server-managed Sent. Live Proton login, installed
  GUI/key-store interaction and certificate export by a user remain unverified.
- The native Go toolchain's `os.RemoveAll` failed to remove nonempty temporary
  directories on this VM, reproduced independently on both `/tmp` and the home
  volume. The integration fixture explicitly removes only its own temporary
  tree with path-based operations before Go's cleanup. The shared Go runtime
  and Bridge deletion code were not patched; Bridge cache/account deletion
  behavior on this environment is not proven by this fixture. Final cleanup
  succeeds and leaves no fixture processes or temporary trees.

The formatted received-message reader now opens from **Formatted view**,
**Message → View formatted conversation** (Command-P), or a conversation's
double-click invocation. It starts with the latest message, provides a native
message picker and plain-text toggle, and refreshes the chosen message without
changing unread state. It runs in the existing isolated `MailPreview` helper;
the main three-pane reader remains native plain text. The helper receives only
the selected message and conversation header summaries, and has no database or
account credentials.

Received HTML is parsed with libxml2 and rebuilt from allowed display elements
and attributes. Tables, inline styles, stylesheets, Unicode and matching CID
images are retained. Active elements, navigation URLs, event attributes, forms,
embedded documents and sender metadata directives are removed. Duplicate CIDs,
non-image parts and unavailable images are not guessed. The private resource
handler serves only exact URLs for this snapshot. CSP blocks remote resources,
and a response-header sandbox denies script execution; it also prevents the
test harness from evaluating JavaScript in the received document. The native
test therefore uses read-only text search, UI messages and actual screen pixels.

All 13 host suites pass with ASAN/UBSAN. The new reader suite passes 35 checks
and 500 mutated HTML cases on both host and air/OS. The native received reader
passes 34 checks, including real painted PNG quadrants, stylesheet colour,
script denial, plain-text switching, thread selection, complex-HTML fallback,
the actual main-window launch/refresh/reopen flow and companion shutdown.
The independent HTTP probe records zero requests from hostile HTML, CSS and
SVG resources. The [received reader capture](evidence/native-received-reader.png)
and [native log](evidence/native-reader.log) record this milestone. Early native
test failures involved initial-page timing, sandbox-denied DOM inspection and
a pixel classifier; a queued test tick after failure caused a test-only crash.
The fixture now stops its queued work on failure and uses the fixture's actual
colour ranges. No engine or shared runtime changes were made.
The existing composer preview still passes all 49 native checks with zero
HTTP probe requests, and the main-window import/layout/read/archive/search
smoke test passes with the new Formatted view control. Both final screenshots
were reviewed. The cross build completes for all targets; core parser/preview
checks were repeated after the shared HTML-document refactor. Final cleanup
leaves no reader/preview processes or private snapshot files.

The composer now imports and exports portable `text/html` clipboard content.
Pasting retains bold, italic, underline, safe links, readable paragraph/list/table
text and embedded base64 images. Copied HTML contains the selected images' bytes
so other applications do not need Mail's private CID resources. Remote, file and
unresolved CID images become alternative text; the importer does not fetch them.
HTML input/output is bounded to 8 MiB, with plain/native fallback when available.
Stylesheets, font sizes/colors, full list/table layout and bitmap-only clipboard
formats remain unsupported. Mail's private fragment still preserves its complete
supported semantics. If another app replaces only the plain representation, a
stale private fragment and its accompanying HTML are ignored.

All 14 host suites pass with ASAN/UBSAN. The new clipboard suite passes 23 checks
and 500 mutated HTML inputs on both host and air/OS. The expanded native composer
passes 120 checks, including actual HTML-only paste messages, self-contained
copy/paste, stale-format fallback, image bytes, single-step Undo/Redo and failed
paste history preservation. Running clipboard operations before the existing
typing checks exposed a stale zero-length font run at the end of the text;
uncached insertion styles now use the last actual UTF-8 character there. The
existing combined caret-style regression and all 55 native typography checks
pass with that fix. The [native clipboard log](evidence/native-clipboard.log)
records the final run. All cross-build targets complete successfully. These
checks use the disposable VM clipboard and no real mail account.

The composer now draws decoded raster images directly within its native editable
text. A private blank font reserves each image's advance and line height inside
BTextView; the named marker, attachment bytes, CID, semantic formatting and byte
offsets remain unchanged in the stored draft. The font never leaves the app in
clipboard text runs and is not installed in the system. Click selects the image;
arrow keys, Backspace and Delete treat it as one object. Copy/paste, formatting,
Undo/Redo and save/reopen preserve image semantics. Layout changes retain the
scrolled reading position, and typing beneath images keeps the caret visible.

A single background worker uses the existing bounded Translation Kit decoder
and produces thumbnails of at most 512 × 320 RGBA pixels. The 100-attachment limit
bounds retained thumbnails below 64 MiB, separately from decoder buffers and
draft/history data. Removing a placement keeps its cached thumbnail while its
attachment remains; removing the attachment cancels its pending work. Window
destruction cancels and joins decoding. Automatic previews accept identified
PNG, JPEG, GIF, BMP, TIFF, WebP and ICO containers, checking bytes before invoking
translators. SVG and unsupported/damaged data retain named markers. Original
files are never resized for saving or sending. The native fixture verifies PNG
and alpha; wider installed-translator coverage, image resizing/cropping, animation,
large-image editing performance and further DPI/keyboard review remain pending.

All 14 host suites pass with ASAN/UBSAN, and all cross-build targets complete.
The new native inline-image suite passes 40 checks in the actual composer,
including four painted colour quadrants, transparency, narrow-column reflow,
whole-image selection/deletion, native clipboard font portability, exact draft
MIME after Undo/reload, malformed/SVG fallback, long-draft scrolling and shutdown
with queued image work. The final native run also passes reader/icon/layout
smoke, 120 composer checks, 55 typography checks and the core suites listed in
the [native log](evidence/native-inline-images.log). Both the
[inline-image capture](evidence/native-inline-images.png) and
[reflow capture](evidence/native-inline-reflow.png) were reviewed. Early fixture
failures sampled queued paints too soon or assumed adjacent bold runs would
remain separate HTML elements; the fixture now observes painted pixels and
semantic styles. No shared OS or renderer code was changed.

The composer and native header fields now use ICU extended grapheme boundaries
for caret movement, Shift selection, Backspace/Delete, Cut/Copy/Paste, formatting
and links. Unicode bytes remain unchanged: joined emoji, skin-tone sequences,
regional-indicator flags, keycaps, combining accents and Hangul are edited as
whole characters. Image insertion also snaps to a grapheme boundary. Native
line-break candidates within a cluster are rejected. This milestone added
editing; single-glyph sequence rendering followed in the work below. An
exceptionally wide unsupported cluster can still exceed BTextView's layout column.

The new native suite passes 149 checks, including actual Shift-key messages,
partial-byte selections, portable clipboard fonts and the subject field's
native Undo. Its first run exposed that BTextView keeps its internal caret
separate from a visible range: expanding the selection alone made Paste insert
at an old caret position. Selection normalization now sets the active caret edge
as well as the range, and replacement/Undo checks pass. This was fixed in Mail;
the shared OS was not changed.

The boundary suite passes 154 application checks and every case in the matching
official Unicode corpus: 1,187 Unicode 15.1 cases on air/OS's ICU 74.1 and 766
Unicode 17 cases on the host's ICU 78.2. Versioned source data and checksums are
in [the corpus directory](../tests/data/unicode/README.md). All 15 host suites
pass with ASAN/UBSAN (27.35 seconds), and all cross targets build. The final
[native grapheme regression log](evidence/native-graphemes.log) also records
passing core, reader/icon/layout, 120 composer, 55 typography and 40 inline-image
checks. The native runner now includes both new suites and the Unicode corpora.
An initial disposable-VM boot stalled in kernel module/notification waits before
Mail ran; its stack traces were preserved locally and the owned VM was restarted
successfully. Live account validation remains pending.

Supported joined emoji, regional-indicator flags, modifiers and keycaps now
render as single native glyphs. HarfBuzz shapes each extended grapheme against
the bundled Noto font. A private glyph face renders the first scalar, and a
zero-width blank face preserves the remaining Unicode scalars in BTextView.
Insertion/deletion reshapes the affected paragraph; formatting and MIME retain
semantic styles and exact Unicode. Native labels truncate only at whole
grapheme boundaries. Unsupported sequences use the existing scalar fallback.
This is monochrome rendering, including Noto's country-code flag designs and
its treatment of skin tones; it does not add color emoji.

The glyph faces share one private family, avoiding Haiku's 128-user-family
limit, and stay valid until application exit for retained text runs and Undo.
The initial single-weight version allowed one face per bundled glyph plus the
blank face (1,892 faces maximum). Its capacity fixture loaded 263 faces using 674,776 font bytes;
500 repeat passes created no additional faces. HarfBuzz and its subset library
are native UI dependencies; the host core remains independent of them.

The new native suite passes 125 checks. Eight raster comparisons independently
draw the original glyph through FreeType and compare it with Mail's native
pixels (weighted overlaps 0.717–0.922). The suite also checks zero continuation
width, decorations preserving glyph identity, capacity above 128 styles,
incremental typing, caret advance, label clipping, semantic runs and Unicode
MIME. The [reviewed capture](evidence/native-emoji-sequences.png) shows the
actual composer. The [native regression log](evidence/native-emoji-sequences.log)
also records 149 grapheme, 55 typography, 120 composer and 40 inline-image
checks, native smoke and the core suites including 1,187 Unicode cases. All
cross targets build, and all 15 ASAN/UBSAN host suites pass in 27.56 seconds.

This exposed an OS dependency: the original app_server freed private-font
bytes while a global FreeType cache entry still borrowed them, then crashed
when another application queried glyph coverage. A
[three-file font-cache fix](../platform/haiku/README.md) retains the source
style and keys cached entries by that retained identity. Fix `2940a1cdff` and
capability metadata `e9268e63e0` are now merged into the owner's air/OS `master`
as `51b26dfe89` and pushed to `jmgasper/haiku`; the merged topic branch was
deleted. The shared installed OS is unchanged. This Mail build requires the
fix. Ten process restart pairs passed with the same app_server
team ([lifetime log](evidence/native-font-lifetime.log)); after a guest reboot,
the launch override, binary hash and both suites passed again
([reboot log](evidence/native-font-reboot.log)). The subsequent package
qualification below replaces that temporary launch override with a patched
OS package. A complete new OS release image is not produced by this Mail task.

Mail now has an installable x86_64 development package,
`artifacts/airos_mail-0.1.0~dev1-2-x86_64.hpkg` (3,367,094 bytes). It contains
the optimized Mail and MailPreview executables, native MIME/icon attributes,
the Deskbar link, installation/Gmail instructions and licenses. The private
`apps/airOS-Mail` directory and **Applications → airOS → Mail** entry coexist
with Haiku's legacy Mail. The package declares its runtime libraries, modern
Summit WebKit and `haiku_private_font_cache >= 1`. The original OS package
correctly [rejects installation](evidence/package-dependency.log) because it
lacks that capability.

The owned VM was upgraded to a qualification OS package based on its original
`hrev60206+1145` package. Only app_server and package metadata were replaced;
the initial package is retained for recovery. After reboot, the ordinary
`/system/servers/app_server` launch path and the tested binary hash were
[verified](evidence/os-package-verified.log), with no user launch override.
This qualifies the runtime dependency and package resolver; it is not a
complete rebuild of all OS components or deployment to shared hardware.

The [package lifecycle fixture](evidence/package-lifecycle.log) installs
revision 1, launches the actual packaged app through its Deskbar entry, edits
a local draft, launches the adjacent packaged preview helper and waits for
four painted image quadrants. It then upgrades to revision 2, launches by
application signature, checks the saved emoji, formatting and image bytes,
and removes the package. The database hash and legacy Mail binary remain
unchanged by removal. Both the [installed capture](evidence/package-installed.png)
and [upgraded capture](evidence/package-upgraded.png) were reviewed with rendered
content visible. No provider account or outgoing delivery is involved.
The installed 32-pixel icon in [Tracker](evidence/package-tracker.png) and
16-pixel icon in [Deskbar's airOS submenu](evidence/package-deskbar.png) were
also visually reviewed; both show the vector artwork without clipping.

This qualification caught fixture and tooling gaps: the fixture now closes
its native SQLite connection before launching Mail and inspects writes after
exit; it waits for painted content instead of a fixed delay and accounts for
the PNG's translucent yellow quadrant. Linux packaging tools now use an
isolated attribute store with unique xattr references. Plain inode-based
storage leaked stale MIME attributes, and full native xattrs exceeded this
filesystem's per-file capacity while extracting the MIME database. The builder
checks app signatures, icons and MIME entries before accepting a package.
A deliberately incomplete MIME database was [rejected](evidence/package-negative.log),
and the builder removed the invalid pending package without accepting an artifact.
GCC's optimized build also diagnosed a generic JSON comparison in a fixture;
the helper now compares explicitly typed strings, with the Gmail sync host
suite passing again. No production mail logic changed in that correction.

All debug and release cross targets build. The optimized native run passes
the [full native regression set](evidence/native-release.log), including 125
emoji, 149 grapheme, 55 typography, 120 composer and 40 inline-image checks,
native smoke and the core suites. Ten more process restart pairs pass on the
packaged server with its team unchanged
([release lifetime log](evidence/release-font-lifetime.log)). The renderer
still logs the pre-existing optional media-decoder/fontconfig and Zink device
warnings; the installed preview's pixel checks and captures confirm rendering
despite those diagnostics. Build and install details are in
[package building](BUILD_PACKAGE.md) and [installation](INSTALL.md).

IMAP folder moves now use a persistent schema-10 journal. The original source,
target UIDVALIDITY and the first possible new destination UID are recorded before
transmission. MOVE is preferred; UIDPLUS servers without MOVE use COPY followed
by STORE and UID EXPUNGE for only the original UID. IMAP4rev2 capabilities are
recognized. COPYUID is checked and saved before source removal, so interrupted
STORE/EXPUNGE can resume without another COPY. This follows the semantics in
[RFC 6851](https://www.rfc-editor.org/rfc/rfc6851) and
[RFC 4315](https://www.rfc-editor.org/rfc/rfc4315).

An interrupted command or missing receipt triggers reconciliation of only newer
destination UIDs in the saved epoch. Exactly one byte-for-byte MIME match is
required; Message-ID alone is insufficient. Existing duplicates, different
content with the same Message-ID, multiple matches, changed identifiers and
unconfirmed copies cannot cause automatic source deletion or a repeated COPY.
The local message ID, tags and newer user edits survive recovery. Incomplete
journals hold snapshots of their two folders to avoid creating duplicate local
rows; unrelated folders continue syncing. Explicit review/retry controls for
unresolvable moves remain to be implemented.

The new TLS fixture covers 20 scenarios with 220 checks, including reconnects
after COPY, MOVE, STORE and EXPUNGE; missing/invalid/conflicting receipts;
definite COPY rejection; multiple destination matches; identifier changes;
IMAP4rev2 and missing UIDPLUS; and a durable-copy checkpoint followed by reopening
SQLite. Server assertions count actual commands and preserve both an older exact
duplicate and an unrelated message already marked Deleted. The debug native run
also passed the existing 37 protocol, 96 Sent-copy and 66 real-Bridge checks.
Historical schema fixtures were updated to remove the new table when recreating
older databases; schema 1–9 migrations and the snooze migration remain covered.
The [16-suite host run](evidence/move-host-all.log) passes with ASan/UBSan, as does
the [final move-fixture check](evidence/move-host-fixture.log). The same 220 checks
pass on both [debug](evidence/native-moves-debug.log) and
[optimized Haiku](evidence/native-moves-release.log). Existing native
[protocol](evidence/move-native-protocol.log), [Sent-copy](evidence/move-native-sent.log)
and [Bridge-engine](evidence/move-native-bridge.log) coverage remains green.
One initial optimized run failed before its first scenario completed; the old
fixture did not print the underlying error. Diagnostics were added, and a one-to-two
second guest clock lag was measured. The fixture certificate now starts five
minutes before creation, with ordinary trust/hostname validation still enabled;
the rerun passes. Clock skew is a plausible cause, not a confirmed diagnosis of
that initial failure.

Revision 3, `artifacts/airos_mail-0.1.0~dev1-3-x86_64.hpkg` (3,385,125 bytes),
contains the new move recovery. The [native regression run](evidence/move-native-regression.log)
passes. The [package lifecycle](evidence/package-v3-lifecycle.log) installs the
unchanged revision 2, seeds its schema-9 profile using the retained old fixture,
then upgrades to revision 3 and verifies the draft, image bytes, formatting and
rendered preview. The resulting database was inspected read-only: schema 10,
with its positioned draft image retained. Removal preserves that database and
legacy Mail. Both [before](evidence/package-v2-before-upgrade.png) and
[after](evidence/package-v3-upgraded.png) captures show the rendered image.
They also exposed an unreadable joined emoji in the preview header; the weight
fix below resolves that defect. System window-title fallback remains incomplete.
The previous package's
[build hashes](evidence/release-package2.sha256) are retained separately.

The preview-header defect was reproduced in one process and traced to Mail's
fixed 0.6-pixel synthetic bold width. At a 13-point label size it filled the
glyph's internal details. Native emoji now use static instances of Noto's
designed 400 and 700 weights, for both full scalar fonts and subset sequence
glyphs. Font names and bold metadata identify the variant; BTextView retains
bold semantics without synthetic expansion. Italic still uses native shear,
and a proportional synthetic fallback is retained only if creating a static
instance fails. The original embedded font and outgoing Unicode are unchanged.

The [style comparison](evidence/emoji-weight-styles.png) shows regular, designed
bold and the former synthetic bold at 13/24/48/64 points. The expanded native
suite compares 60 combinations of ten emoji, two weights and three sizes with
FreeType rendering the original variable font. Small-glyph comparisons allow
one pixel of baseline/hinting alignment and bound both overlap and ink coverage.
The original 13-point defect has 2.540 times the reference ink and fails that
bound; the designed weight has 1.063 times the ink and passes. The suite also
retains the original large-glyph comparisons, formatting, caret, MIME and cache
checks. Two weights bound the private family to 3,784 faces; the fixture loads
274 faces with 2,492,144 generated font bytes, including both scalar fonts.

All debug/release targets build. The [optimized native regressions](evidence/emoji-weight-regression.log)
pass 343 emoji checks, 55 typography, 120 composer, 40 inline-image and 149
grapheme checks, plus reader smoke and the core suites. The separate
[preview](evidence/emoji-weight-preview.log) and [received-reader](evidence/emoji-weight-reader.log)
fixtures pass 50 and 34 checks with no document-triggered network requests.
Ten new-process [lifetime pairs](evidence/emoji-weight-lifetime.log) each pass
341 emoji and 53 typography checks, with app_server team 118 unchanged.

Revision 4, `artifacts/airos_mail-0.1.0~dev1-4-x86_64.hpkg` (3,394,923 bytes),
contains the weight fix and keeps database schema 10. Its
[package lifecycle](evidence/package-v4-lifecycle.log) upgrades the unmodified
revision 3, preserves the styled draft and image, renders the installed helper,
launches by application signature and removes the package while retaining data
and legacy Mail. The actual [upgraded preview](evidence/package-v4-upgraded.png)
and [magnified header comparison](evidence/emoji-header-comparison.png) were
visually reviewed. The comparison shows revision 3 above revision 4; it is an
enlarged crop of the two package captures. The decorator's missing joined-emoji
glyph remains visible in the window tab and is not claimed fixed.

Folder-move review is now available through **File → Folder moves…** and a
persistent count in the main window. The native window shows each account,
subject, source, destination, saved state and latest error, including moves
that fail before a copy can start. **Check server** continues saved progress;
**Retry copy…** requires confirmation before allowing another uncertain COPY.
Recorded destination receipts cannot be retried as new copies. **Keep local
copy…** preserves the message and its current edits, detaches its server
identity, and clears its queued mutations without deleting remote mail.
**Cancel move** restores the server folder for a queued move that has not begun.
The [native review capture](evidence/native-move-review.png) was visually
inspected; the details and action labels fit the default native window.

Schema 11 adds durable worker claims, revision checks and per-move errors.
Opening another database connection does not reset an active claim; startup
recovery releases abandoned claims without losing copy receipts. Both worker
and UI actions revalidate the selected operation. A confirmation cannot change
a running move, a replaced request, or a move checked since the dialog opened.
Explicit retry obtains a fresh destination baseline, so earlier ambiguous
copies are excluded from recovery of the new attempt. The original source
UIDVALIDITY still has to match before copying or removing anything.

The [host suite](evidence/move-review-host.log) passes all 16 CTest suites with
ASAN/UBSAN. Added [migration and review checks](evidence/move-review-migration-host.log)
preserve a schema-10 copy receipt through the upgrade. The
[native move suite](evidence/move-review-native-protocol.log) passes 317 checks
across 25 TLS scenarios plus storage/recovery cases; the server independently
asserts exact copy counts and preservation of unrelated deleted mail and old
destination copies. The [native UI fixture](evidence/move-review-native-ui.log)
passes 158 checks for Cancel, explicit retry, checking saved progress, active
worker guards, keep-local behavior, queued cancellation and absence of SMTP work.
The [native regression suite](evidence/move-review-native-regression.log) also
passes core (409), snooze (38), Gmail storage (45), organization (39), compose
(111), preview (20), reader (35 plus 500 mutated inputs), clipboard (23 plus
500 mutated inputs), graphemes (154 plus 1,187 Unicode cases), emoji (343),
native smoke, typography (55), composer (120), inline images (40) and native
graphemes (149). Debug and optimized native builds both compile cleanly.

Revision 5, `artifacts/airos_mail-0.1.0~dev1-5-x86_64.hpkg` (3,421,491 bytes),
contains the review controls and schema migration. Its
[package lifecycle](evidence/package-v5-lifecycle.log) installs the unchanged
revision 4, seeds a schema-10 draft using the retained old fixture, upgrades,
launches the installed application and preview, and verifies the saved content.
The closed database was inspected read-only: schema 11, review columns present,
and the positioned draft image retained. Package removal preserves that database
and legacy Mail. The [upgraded capture](evidence/package-v5-upgraded.png) was
visually inspected, including the new main-window move count. Revision 4's
[build hashes](evidence/release-package4.sha256) remain available separately.

The Gmail draft API and MIME conversion layer are now implemented. Draft
creation/replacement retains BCC and allows an empty recipient list; delivery
still uses a validated envelope and an immutable local MIME copy without BCC.
Import restores supported bold/italic/underline/links, repeated positioned CID
images and binary attachments. Encoded display names are parsed before decoding,
so a comma inside a name cannot become another recipient. This also fixes the
existing Gmail send path. Unfinished address input stays unchanged; duplicate
recipient headers and decoded header controls are rejected. Unknown/ambiguous
CID references keep their attachment bytes without choosing an arbitrary image,
and no image URL or file is fetched.

The API handles list/search pagination, raw or minimal fetch, create, replace,
delete and send using Gmail's stable draft ID separately from its changing
message ID. Its behavior follows Google's [draft guide](https://developers.google.com/workspace/gmail/api/guides/drafts)
and [draft resource](https://developers.google.com/workspace/gmail/api/reference/rest/v1/users.drafts).
The [send method](https://developers.google.com/workspace/gmail/api/reference/rest/v1/users.drafts/send)
receives the saved outgoing content and recipients rather than sending a mutable
server draft by ID alone. Lost/malformed write receipts and HTTP 5xx are treated
as uncertain saves; uncertain sends remain delivery uncertainty. Only an explicit
401 gets one token refresh and retry. No ambiguous mutation is automatically
repeated by the transport.

All 17 [host suites](evidence/draft-transport-host.log) pass with ASAN/UBSAN.
The new [host](evidence/draft-transport-protocol-host.log) and
[native](evidence/draft-transport-native.log) suites pass 101 checks each. Their
HTTPS server independently parses MIME, verifies BCC and repeated CID images,
changes draft message IDs, modifies recipients as another client would, and
checks exact mutation counts after interrupted requests. The existing
[native Google suite](evidence/draft-transport-native-google.log) still passes
94 assertions, and the [native regression suite](evidence/draft-transport-native-regression.log)
passes after the shared MIME/address parsing changes.

At revision 6 this was an API foundation with local-only composer saves.
The persistent journal work below builds on it; application draft synchronization
remains incomplete.

Revision 6, `artifacts/airos_mail-0.1.0~dev1-6-x86_64.hpkg` (3,461,303 bytes),
includes the Gmail recipient-name fix and the inactive draft API foundation.
The [package lifecycle](evidence/package-v6-lifecycle.log) passes installation,
upgrade from revision 5, native launch/edit/preview, reopening and removal.
Schema 11 remains unchanged; the saved draft, positioned image and legacy Mail
survive the test. The [upgraded preview](evidence/package-v6-upgraded.png) was
visually inspected. Revision 5's [build manifest](evidence/release-package5.sha256)
and schema-11 seed fixture are retained for later migration qualification.

The schema-12 development milestone added optimistic draft revisions;
stale saves, composer send and discard cannot overwrite a newer stored version.
Saving unchanged content preserves its revision and modification time. The
Gmail draft journal stores stable server IDs, acknowledged MIME, the exact
pending upload and its local revision, per-draft errors and worker claims.
Late receipts acknowledge only their uploaded snapshot while preserving newer
typing. Discard and From-account changes retain old server cleanup records,
including creates whose server identity has not yet arrived. A second database
connection cannot take an active claim; startup recovery invalidates old worker
tokens while retaining uncertain attempts. Account removal waits for active
draft work, and account edits cannot retarget a tracked Google identity.

The outbox waits for active or unresolved draft saves. Once a draft is bound,
delivery uses `drafts.send` with the immutable queued MIME and recipients.
Sending and uncertain delivery exclude draft work; successful delivery retires
the journal and local draft atomically with its Sent record. Rejection, missing
drafts and lost responses remain explicit delivery failures or uncertainty,
without fallback sending or automatic replay. The main window checks for an
eligible queued send so a waiting draft cannot spin the send worker.

All 18 [host suites](evidence/draft-journal-host.log) pass with ASAN/UBSAN.
The new storage suite passes 102 assertions on host and air/OS, including schema-11
migration with binary attachments and an immutable queued message. The expanded
[host](evidence/draft-journal-transport-host.log) and
[native](evidence/draft-journal-native-transport.log) HTTPS suites pass 117 checks;
the fixture changes server content before outbox delivery and verifies exact
mutation counts for success, disconnect, rejection and removal. The
[native regression suite](evidence/draft-journal-native-regression.log) passes,
including 417 core assertions and composer, inline images, typography, reader
and emoji checks. Both debug and optimized native builds compile cleanly.
The [native Google UI fixture](evidence/draft-journal-native-google.log) also
passes sign-in, key-store refresh, cancellation, identity checks, receive,
reply/send, Sent reconciliation and revoke/reconnect. Its
[first attempt](evidence/draft-journal-native-google-permission-timeout.log)
timed out at the rebuilt disposable test application's key-store permission
prompts; after granting its get/add/remove access, the clean rerun passed.

That milestone did not enable automatic enrollment/upload. Revision 6's
[build manifest](evidence/release-package6.sha256) and
`.cache/mail-native-package-schema11-v6` seed are preserved, along with the
schema-12 [build manifest](evidence/release-draft-journal.sha256) and seed fixture.

## Gmail draft synchronization and native review

Revision 7 introduced schema 13. Mail imports Gmail drafts into the editable Drafts
folder and enrolls local Gmail drafts for background saves. It retains the
original downloaded MIME until a local edit and uses Gmail's stable draft ID
while the contained message ID changes, as described in
[Google's draft guide](https://developers.google.com/workspace/gmail/api/guides/drafts).
Ordinary receive no longer adds read-only duplicate draft messages to threads.
Paginated discovery imports unknown drafts; absence from a list is never proof
of deletion. A direct lookup must confirm a missing draft.

The composer saves locally every two seconds. After a pause in edits, the main
window runs a draft check without interrupting other account work. The status
reports local saves, pending work, Gmail confirmation or review. Both
**File → Draft synchronization…** and **Review saved versions…** open the native
review window. **Check Gmail** requests reconciliation; conflicts can keep both
versions, retain local edits after a remote deletion, or restore a changed Gmail
copy after local discard. Review rows and version details render emoji, and the
details include recipients and attachment counts.

Editor leases protect unsaved text from background replacement. Clean, closed
drafts adopt remote changes; locally edited, open or queued drafts retain both
versions for review. Choosing Keep both makes a separate local draft with a new
Message-ID and preserves the Gmail version. Outbox delivery waits for unresolved
saves and conflicts. The composer also detects header/body changes synchronously
when closing or previewing, so a queued change notification cannot lose the last
edit. Opening and closing an untouched imported draft keeps its revision and
original MIME.

Interrupted creates search by Message-ID and require a unique exact MIME match;
updates can confirm the persisted attempt against the known draft. No uncertain
write is blindly replayed. Deleted drafts and discarded local drafts are checked
against saved identities before cleanup. Cancellation, account mismatch,
authentication failures and bounded pagination remain explicit errors.

All 19 [host suites](evidence/draft-sync-host.log) pass with ASAN/UBSAN. The new
[native synchronization suite](evidence/draft-sync-native-core.log) passes 31
checks covering imports, updates, deletions, editor protection, conflicts,
discard, interrupted writes, identity checks and cancellation. The
[native UI fixture](evidence/draft-sync-native-ui.log) passes 14 checks through
the actual composer and review windows, including background upload, unchanged
open/close, immediate edit/close and Keep both. The
[review capture](evidence/native-drafts.png) was visually inspected; an initial
missing emoji glyph in the list row was fixed and rechecked. Both debug and
optimized native builds compile cleanly.

The [native regression suite](evidence/draft-sync-native-regression.log) passes
425 core assertions plus storage, composition, reader, clipboard, grapheme,
emoji, typography and inline-image checks. Its
[first run](evidence/draft-sync-native-regression-initial.log) exposed a new
composer's unsaved-draft path after editor leases were added; the constructor
now persists that draft before claiming it, and the full rerun passes. The
[Google UI fixture](evidence/draft-sync-native-google.log) passes sign-in,
key-store refresh, cancellation, identity checks, receive, reply/send and
revoke/reconnect. The [rendered preview test](evidence/draft-sync-native-preview.log)
passes 49 native checks, including composer refresh and shutdown, with zero
requests to the independent remote-content probe.

Revision 7, `artifacts/airos_mail-0.1.0~dev1-7-x86_64.hpkg` (3,512,629 bytes),
packages these changes and the updated Google setup instructions. The
[package lifecycle](evidence/package-v7-lifecycle.log) passes installation of
revision 6, upgrade to revision 7, native launch/edit/preview, reopening and
removal. The older schema-11 fixture seeds the profile before the new build
migrates it to schema 13. The saved draft, formatting, positioned image and
legacy Mail survive; removal preserves the database. The
[upgraded preview](evidence/package-v7-upgraded.png) was visually inspected.
The [revision-7 build manifest](evidence/release-package7.sha256) and
`.cache/mail-native-package-schema13-v7` fixture are retained. Older packages
cannot reopen a profile migrated to schema 13.

Revision 7 held uncertain creates whose results were missing, ambiguous or
rewritten by the provider; the recovery controls below address those decisions.
A remote
edit can also race between Mail's separate GET and update requests; no atomic
cross-client compare-and-swap has been qualified. Large draft lists, attachment
responsiveness, broader review interactions and live Google behavior still need
qualification. IMAP server drafts remain unimplemented.

## Interrupted Gmail draft recovery

Current source builds use schema 14. A completed recovery search retains the
possible Gmail copies, their identities and original MIME for review after a
restart. Search remains bounded to 100 copies and 64 MiB. Mail searches the
attempt's Message-ID using the documented
[draft list query](https://developers.google.com/workspace/gmail/api/reference/rest/v1/users.drafts/list).
It clears old choices when a new check starts and stores each new result set
atomically, so a partial result cannot be presented as a completed search.

**Use Gmail copy…** links the selected snapshot to the interrupted local save.
Transport-header rewrites preserve the downloaded MIME without uploading or
creating another copy. Newer local edits remain pending against their own
revision. If the selected content differs, the local work becomes a separate
draft and the chosen Gmail content is retained. Other server copies are left
intact and can appear separately on a later check.

**Retry save…** creates a fresh Message-ID, retains the latest local text and
attachments, and retires the previous uncertain attempt. The confirmation
explains that the earlier upload may appear later and leave a duplicate. A late
old copy cannot take over the retried journal. An already discarded local draft
instead offers **Stop checking…**, which retires the unresolved cleanup without
deleting server copies. Both recovery decisions and copy selection default to
Cancel. They reject open or queued drafts, active workers and stale
confirmations. None of these actions sends mail to recipients.

All 19 [host suites](evidence/draft-recovery-host.log) pass with ASAN/UBSAN.
[Storage checks](evidence/draft-recovery-store-host.log) now cover 125 assertions,
including persisted candidates, active/editor/outbox guards, stale callbacks,
identity reuse, rollback and migration of an unresolved schema-13 upload.
The expanded [host](evidence/draft-recovery-sync-host.log) and
[native](evidence/draft-recovery-native-sync.log) HTTPS fixtures pass 40 checks.
They verify rewritten and multiple exact copies, preserving divergent edits,
explicit retry, a late earlier copy, and recovery after local discard, with
independent mutation counts.

The [native regression suite](evidence/draft-recovery-native-regression.log)
passes 433 core assertions plus storage, composition, reader, typography,
inline-image, emoji and grapheme checks. Debug and optimized builds compile
cleanly. The [native UI fixture](evidence/draft-recovery-native-ui.log) passes
42 checks through import, autosave, conflict handling, candidate selection,
retry and stopping a discarded save. Each confirmation is checked with Cancel
and acceptance, and server mutation counts verify the result. The
[review capture](evidence/native-draft-recovery.png) was visually inspected;
the layout was adjusted so local and selected server text are visible together.
The [first](evidence/draft-recovery-native-ui-initial.log) and
[second](evidence/draft-recovery-native-ui-refresh-timeout.log) test attempts
timed out after background work made the harness's cached selection stale.
Mail rejected those actions without another upload. The harness now waits for
the requested check to complete and refreshes the reviewed snapshot before
invoking the action; the complete rerun passes.

Revision 8, `artifacts/airos_mail-0.1.0~dev1-8-x86_64.hpkg` (3,516,035 bytes),
includes the recovery controls and updated setup instructions. Its
[package lifecycle](evidence/package-v8-lifecycle.log) passes installation of
revision 7, upgrade to revision 8, native launch/edit/preview, reopening and
removal. The [retained profile](evidence/package-v8-migration.log) confirms
schema 14, the saved draft and no lingering editor leases after removal.
The [upgraded preview](evidence/package-v8-upgraded.png) was visually inspected.
Revision 7's schema-13 seed and manifest remain intact; the
[revision-8 manifest](evidence/release-package8.sha256) and
`.cache/mail-native-package-schema14-v8` seed are retained for later upgrades.
Packages through revision 7 cannot open a schema-14 profile.

Remaining limits include concurrent remote edits between separate GET and update
requests, results whose Message-ID was itself rewritten, large attachments and
mailboxes, and live Google qualification. A missing match never proves the
earlier save failed; retry still needs the user's duplicate-risk decision.
IMAP server draft synchronization remains a follow-up enhancement. The transport
foundation below is tested but is not connected to background draft saves.

## Requested feature coverage and follow-up work

| Requirement | Current state | Known limits and future work |
| --- | --- | --- |
| Native air/OS / Haiku application named Mail | Native reader, organizer and composer; x86_64 package install, upgrade, signature/Deskbar launch and removal validated | Broader interaction/font/DPI/theme/keyboard review; other architectures and complete OS release qualification |
| Modern client informed by Airmail and Mimestream | References reviewed; native three-pane interface and isolated formatted conversation reader; receive/read/compose/reply/reply-all/forward/send/Trash/search workflows and visual review pass | Broader real-message and accessibility review |
| Multiple accounts and unified inbox | Native add/edit/remove, BKeyStore passwords, sequential background sync, scoped queries, composer account selection; two sending identities and a separate BCC identity checked through real Bridge | Concurrent provider scheduling, richer per-account status, live accounts |
| IMAP accounts | TLS/STARTTLS, folders, UIDVALIDITY, PEEK receive, flags, durable MOVE/COPY recovery with targeted removal, native move review/retry and recoverable Sent uploads; host/native fixtures pass | Large-mailbox incremental sync/IDLE, folder mapping and other capability cases, live-server tests |
| POP3 accounts | TLS/STARTTLS, UIDL deduplication, RETR, retained server messages and local organization; host/native fixtures pass | Large mailboxes, disappearance/reconnect coverage and live-server tests |
| Gmail API and Google login | Native sign-in, key-store tokens/refresh, receive/send, revoke/reconnect, Sent reconciliation, background drafts and explicit ambiguous-save recovery pass disposable host/native fixtures | Registered Desktop client and live browser/account validation, rewritten Message-ID cases, cross-client races, quota/backoff and large mailboxes |
| Proton Mail Bridge | Local-only preset, separate credentials, verified certificate trust, native real-engine receive/send/reply/moves/Sent and restart fixture with three identities | Live Proton account, installed Bridge GUI/key-store workflow and certificate export interaction |
| Haiku icon standards | Original vector icon, source generator, native 16/32/64 rendering accepted; installed Tracker and Deskbar icons visually reviewed | Broader DPI/theme review |
| Images and attachment drag and drop | Positioned body drops, native inline bitmap layout, tracked delayed transfers, Mail clipboard image transfer, native Tracker export, image preview, attachment controls and CID delivery; native checks pass | Image resizing/cropping, multiple-file export, broader external-app negotiation and large-file import responsiveness |
| Emoji | Monochrome sequence shaping, designed regular/bold weights and grapheme editing; native and actual packaged-preview checks pass; font-cache fix integrated | Richer picker, color/coverage, broader font/DPI review and system window-title fallback |
| Rich text editing | Native bold/italic/underline, links, inline bitmap layout, native/Mail/HTML clipboard transfer with embedded images, bounded multilevel rich undo/redo, draft restoration, rendered message preview and formatted SMTP send | Richer CSS and list/table layout, colors, image resizing/cropping and large-image editing performance |
| Conversation / threaded views | Reference/provider grouping, plain and formatted conversation views, native message selection and reply/reply-all/forward metadata and interactions validated | Live provider threads and large-thread performance |
| Automatic filtering | Persistent ordered rules, native create/edit/reorder/disable/remove, supported IMAP actions and end-to-end Gmail receive actions | Unicode case folding, richer predicates, rule preview and bulk application to existing mail |
| Snoozed items | Custom local date/time and presets, explicit repeated-hour choice, durable reminder list/count, notifications, early wake and native restart coverage | Separate background service, provider synchronization, large reminder lists and broader calendar/DPI/keyboard review |
| Smart folders | Persistent queries, native create/edit/remove with full current criteria, stable selected view and draft filtering | Large-cache responsiveness, broader combined-query and keyboard/DPI review |
| Tags | Scoped rename/remove, message add/remove, Gmail label journal and host/native API/UI fixtures | Live Gmail behavior, label conflict recovery and broader keyboard/DPI review |
| Standard reliable email behavior | Raw cache, local/Gmail drafts, durable mutations, SMTP/Gmail outbox, persistent delivery attention, native recovery controls, IMAP/Gmail Sent reconciliation and interruption recovery tests | IMAP server drafts, server-specific Message-ID rewriting/delayed-copy behavior, notification, contacts and MIME edge cases |

## Final development-release qualification

Revision 9 retains schema 14 and includes both the Google and Bridge setup
guides. [Acceptance](ACCEPTANCE.md) maps the original requested features to
their implementations and evidence; the future work above does not imply that
every possible feature of another mail client is an acceptance condition.

The full host run passes all 20 CTest suites with ASan/UBSan. Native Release
regression passes core/storage/composition, reader, clipboard, typography,
grapheme/emoji and inline-image checks. The expanded native main-window fixture
imports a two-message thread and exercises archive, search, Reply-To, reply-all
with self-recipient exclusion, forward with binary attachments, draft save/close
and Trash for the whole conversation. Its screenshot was visually inspected.
See `mail-final-{host,native}.log` and `mail-final-inbox.png` in `docs/evidence/`.

Shared IMAP transport changes pass the existing native protocol (37), Sent-copy
(96) and move-recovery (317) checks, with independent server assertions. The
new draft-only transport exposes `\Draft`/`\Deleted`, an optional UIDNEXT,
APPEND with `\Draft`, exact raw retrieval, and targeted deletion. All 95 checks
pass on host and native air/OS. Lost receipts remain uncertain; missing flags
or body are errors, and changed UIDVALIDITY or content prevents removal. A
legacy server leaves only the target marked Deleted; UIDPLUS/rev2 uses targeted
UID EXPUNGE and preserves unrelated messages already marked Deleted. This follows
[IMAP4rev2](https://www.rfc-editor.org/rfc/rfc9051.html),
[UIDPLUS](https://www.rfc-editor.org/rfc/rfc4315.html), and
[special-use mailboxes](https://www.rfc-editor.org/rfc/rfc6154.html).
No IMAP draft store journal or background synchronization is claimed.

The first host draft fixture failed because its synthetic account omitted the
stored account ID required by the composer. Giving the fixture its account ID
fixed the test without relaxing production validation; initial and corrected
logs are retained. Evidence is `imap-draft-{host,native}.log` and
`mail-final-native-{protocol,sent,moves}.log`.

Package revision 8 → 9 installation, launch/edit/preview, upgrade, reopening and
removal are checked in the disposable VM. The profile and legacy Mail app are
retained. See `package-v9-{build,lifecycle}.log` and the release manifest.

## Permanent deletion from Trash

2026-11-11 (airos-works/haiku#73: "Mail cannot delete emails in trash"). Delete
on a message already in Trash queued another move to Trash, which did nothing;
no command removed mail for good. Now, in a Trash view, Delete asks (default
Cancel) and deletes the selected messages permanently, and **Message → Empty
Trash…** does the same for the whole Trash. Only messages whose folder is
Trash are affected; other messages of the same conversation stay.

Storage (schema 16, `deleted_messages`, `src/core/DeleteStore.cpp`): the row
hides its message from lists, conversations, search and counts in the same
transaction. Mail without a server copy is removed at once. Otherwise the
cached row stays as a record until the server copy is gone, so that a later
check cannot download the message again:

- IMAP and Proton Mail Bridge (`pending`): the next check runs `UID STORE
  +FLAGS.SILENT (\Deleted)` and `UID EXPUNGE <uid>` for that UID, then removes
  the cached message. A queued, not yet started move to Trash is dropped and
  the message is removed where the server has it; a move that already started
  finishes first and the removal then targets the destination. Without UIDPLUS
  (or IMAP4rev2) the message stays marked `\Deleted` and hidden (`flagged`):
  Mail does not send a plain EXPUNGE, as for moves and drafts, because it
  would remove other clients' deleted mail. The record goes once a complete
  mailbox listing no longer has the message. A dropped connection leaves the
  journal row and its error for the next check; a tagged NO/BAD shows the
  message again and reports why. A changed UIDVALIDITY cannot be repaired: the
  stale record goes with the next listing and the server's message is
  downloaded again under its new identity.
- Gmail (`local`): the message is hidden here and stays in Gmail's Trash,
  which Gmail empties after 30 days; the confirmation says so. The queued move
  to Trash still reaches Gmail. `users.messages.delete` needs the restricted
  `https://mail.google.com/` scope instead of `gmail.modify`; asking for it
  would make every user sign in again and widens what the OAuth client must
  be verified for, so the scope is unchanged. A message another client takes
  out of Trash is shown again; once Gmail reports it gone the record is removed.
- POP3 (`local`): the server always keeps its copy. The cached content is
  blanked and the record kept until the server's UIDL list drops the message.

Evidence: `ctest --test-dir build-host` passes all 22 suites with the address
and undefined-behavior sanitizers ([log](evidence/delete-host.log));
`mail_deletes` has 107 checks covering the store rules for local, Gmail, POP3
and IMAP accounts, the schema-15 migration, and five IMAP scenarios over TLS
against the stateful move fixture (through Trash, deleted before the queued
move ran, no UIDPLUS, connection lost after STORE, connection lost after
EXPUNGE), each reopening the database and reconnecting. The fixture asserts
that only UID 1 was ever stored or expunged, no copy of deleted mail was made,
and the unrelated message another client marked `\Deleted` survived. The same
binary passes natively in a disposable air/OS VM (hrev60207+2447 image,
[log](evidence/delete-native.log)); `mail_native_smoke` now ends by opening
Trash, cancelling the confirmation, confirming it and finding Trash empty
([log](evidence/delete-native-ui.log), run with `MAIL_PRIVATE_EMOJI=1` since
its glyph check predates system color emoji). Its first step also waits for
the reader instead of failing when the import's last refresh rebuilds the
list. Package `airos_mail-0.1.0~dev1-10` builds from this tree. Not tested:
live Gmail, Proton Bridge or third-party IMAP accounts, and deletion of very
large Trash folders (one transaction, one alert).

## Follow-up enhancements and live validation

Large-mailbox performance pass, 2026-10-27: profiling with a synthetic
100,000-message mailbox (`mail_perf_tests`, built but kept out of CTest) showed
three dominant costs: one HTTPS request with a fresh TLS handshake per
message, one store transaction (and `synchronous=FULL` commit) per ingested
message, and multi-second conversation-list queries. Gmail checks now fetch
message contents on up to eight worker threads through one shared `GmailApi`
(its credential callback was already serialized), while the calling thread
ingests results in batches of up to 400 messages per transaction under relaxed
durability; each thread reuses its HTTPS connection, and "already cached" is
decided from one snapshot of cached IDs instead of a store query per listed
message, so the store lock stays available to the UI during long checks.
Conversation lists rank and aggregate a lean column set and join the winning
message back for its preview; unread counts skip the window function.
Measured on Linux/Release at 100k messages: inbox list 1.71 s → 0.49 s,
unread counts 0.39 s → 0.06 s, full-text list 2.36 s → 0.79 s, ingestion
1,330 → 1,750 messages/s, with bulk commits also removing up to one fsync per
message on slower disks. All 21 host suites and the independent Gmail HTTPS
fixture still pass. IMAP/POP3 receive still ingests serially per message;
batching it and a prepared-statement cache remain future work.

Installation follow-up, 2026-10-22: the user's x86_64 `hrev60207+1733` build
predates the font-cache fix and rejects Mail revision 9's capability dependency.
The exact error was reproduced with the matching original OS package. The
matching revision-2 OS update is now in `artifacts/`, with
[installation instructions](FONT_CACHE_UPDATE.md), checksum and input/change
manifest. Only app_server and package metadata change; all 6,196 other entries
retain their types, modes, symlink targets and file contents. The patched VM
reboots normally, runs the packaged server, passes the full native Mail suite
and ten font-lifetime pairs without a server restart, and passes Mail's actual
installation/upgrade/launch/edit/preview/removal fixture. Evidence uses the
`font-cache-1733-` prefix. The previous `1145` VM package is not the update for
this newer build. The user's running OS was not modified.

1. Configure a Google Desktop client using [Google setup](GOOGLE_SETUP.md), then
   validate live consent and a small mailbox on air/OS when credentials are
   available. The user requested setup instructions rather than supplying a
   client. The requested setup guide is complete; no live account has been
   contacted or mail sent to external recipients.
2. Extend the tested IMAP draft transport into server draft synchronization. Continue
   richer clipboard interoperability and broader composer keyboard/performance coverage.
3. Improve folder mappings/capability fallbacks, incremental
   mailbox updates, and large-message handling. Validate a live Proton account
   through the installed Bridge GUI when available.
4. Add an optional background reminder service and review organization at other
   DPI settings and with keyboard-only use. These extend the delivered feature set;
   production service, other-architecture and public-release qualification remain separate.

## Current connectivity limits

Each check downloads uncached mail and refreshes full folder UID/flag metadata.
Accounts are checked sequentially, with cancellation and bounded network waits;
there is no IMAP IDLE or background service. Downloaded bodies are kept offline.
Read/star and folder changes are journaled and sent during the next check. A
pending local change wins over an older server snapshot. POP3 never issues DELE.

IMAP moves require UIDPLUS (or IMAP4rev2) and an advertised target folder. MOVE
is used when available; otherwise the journal coordinates COPY, STORE and targeted
UID EXPUNGE. Lost or missing receipts can recover through a unique exact content
match among new destination UIDs. Recovery scans at most 100 candidates and stops
on ambiguity or changed mailbox identifiers. Unresolved moves retain the cache
and report an error without another uncertain COPY; their source and destination
folder snapshots wait for resolution. The Folder moves window supports review,
explicit retry, keep-local and queued cancellation. Other folders continue to sync.
An unsupported/malformed MIME message currently stops that folder's download
for the check and reports the error; other folders continue. There is no retry
backoff, provider quota policy, or user-facing cache recovery/export workflow.

SMTP sending is connected to the composer/outbox. A failure after upload
begins reports uncertain delivery and is never automatically resent; TLS,
STARTTLS and ambiguous delivery have dedicated fixtures. All protocol tests
use temporary local servers. The native Bridge integration additionally runs
the actual Bridge engine against its disposable Proton API server; no production
Gmail, IMAP, POP3 or Proton account has been connected.

Account settings and random key identifiers live in SQLite; passwords live in
BKeyStore. Haiku has no atomic key-replacement API: the current implementation
restores the old key on an ordinary add failure, but a process/system crash
between remove/add can require re-entering the password or signing in again.
Incoming host, port,
provider and username cannot be changed after remote mail is cached; add a new
account for a different identity. Gmail email identity is also fixed after cache
creation and is supplied by the verified profile in the account form.

Google OAuth, Gmail cache synchronization and Gmail outbox delivery are wired
to native Accounts and receive/send workers. A shared credential mutex
coordinates key-store load/refresh/save and revocation. Refreshed credentials
are persisted before use; a worker reuses another worker's replacement after a
401. These paths pass on target using disposable fixture credentials.
Gmail sync stops without advancing its cursor if a message
cannot be decoded; malformed-message recovery still needs work. Each account's
sync must remain serialized. A full refresh is bounded to five million IDs and
10,000 pages. Unsupported moves remain pending with a reported error.
The fixture simulates the browser using HTTP requests; it does not prove that
the target system browser can complete Google's live consent screens. Gmail's
`gmail.modify` scope covers the planned mailbox features without immediate
permanent deletion. Public release requires the applicable Google verification
for this restricted scope, as described in the
[scope documentation](https://developers.google.com/workspace/gmail/api/auth/scopes).
There is no production client registration or real Google authorization yet.

## Composer limits still to resolve

Drafts autosave locally every two seconds and on close. Send commits immutable
MIME and envelope recipients before network activity. BCC is omitted from local
message headers and the SMTP wire copy; Gmail gets a temporary BCC header because
its API derives recipients from the message. Successful delivery atomically creates local Sent and removes the
draft. Failed attempts require explicit retry or Return to draft; an interrupted
in-flight attempt becomes uncertain at startup. A local save failure after SMTP
success is also uncertain. No test sends to a real person or external account.

Drafts are not uploaded to IMAP yet. New SMTP Sent copies upload or reconcile
according to the account setting; historical local Sent mail from schema 5 and
earlier stays local. Services that rewrite Message-ID cannot be reliably matched.
A service that saves a delayed Sent copy after Mail's search/upload can create a
duplicate; select Server saves Sent for services that perform that operation.
There is no general duplicate-removal operation. A missing server-managed copy
remains visible until found or kept local. A real native Bridge engine fixture
now exercises this policy, but live Proton and other IMAP accounts still need
verification.
Local Sent display does not yet show BCC roles (envelope recipients remain in
the delivery record). Rich editor styles currently persist bold, italic,
underline and links; pasted font sizes/colors are not retained. Mail clipboard
fragments preserve links and image positions. External HTML clipboard content
now imports supported inline styles, links and embedded base64 images; lists
and tables become readable text rather than editable layout objects. Remote,
file and unresolved CID images are not fetched. Stylesheets, font sizes/colours,
bitmap-only clipboard formats and full browser layout remain unsupported.
HTML clipboard input/output is bounded to 8 MiB; plain/native representations
remain available when HTML is oversized or invalid. The body has independent
Undo and Redo commands (Command-Z and
Command-Shift-Z), with consecutive typing grouped. History retains at most 100
steps across both directions and 64 MiB of additional snapshot data. Shared
attachments already required by the current draft do not consume that history
budget; removed attachment bytes count once. History is discarded on closing
the composer; recipient and subject fields retain Haiku's native single-step
history. Supported raster images now appear as native inline bitmaps, with named
fallback markers for unsupported or damaged images. Bounded previews preserve
original bytes and delivery positions; resizing/cropping and animation remain
unfinished. **Preview message** opens a separate rendered whole-message window
with refresh and zoom controls; it is not an editable canvas. The older
single-attachment Translation Kit preview shows one decoded frame; its zoom
controls, animation and broad translator-format coverage remain unfinished.
Output limits do not isolate the installed translators,
which still run in process. Attachment imports/autosave run on the composer
thread and need large-file responsiveness work. Preview decoding and negotiated
file exports now run off that thread. Limits are 2 MiB body text,
22 MiB text-plus-attachment bytes, and 32 MiB encoded MIME, with 100 recipients
and 100 attachments. Recipient input supports bare addresses and display names;
address groups, quoted local parts and SMTPUTF8 addresses are not supported.
Delivery errors remain visible in the persistent Outbox count while receiving
mail. Native retry/return-to-draft controls and uncertainty confirmations pass
interaction tests; unavailable actions are disabled and active sends remain
protected if the delivery state changes before a click is handled.

## Known limits of the initial reader

Emoji rendering uses monochrome outlines. Supported joined professions/families,
flags, keycaps and skin-tone sequences shape to single native glyphs; unsupported
sequences retain scalar fallback. Regular and bold use designed static instances
of the bundled variable font; italic still uses native shear. This build requires the app_server font-cache
fix documented above. Cursor movement, selection and deletion follow ICU
grapheme boundaries in Mail's body and header text views.
The OS window decorator cannot use Mail's private font, so an emoji in a composer
window title can still show a box even when the subject field renders it.
The picker remains a short menu. Color emoji, broader Unicode coverage, a
searchable picker and font/DPI review still need work.

MIME import is bounded to 32 MiB per message, 64 KiB headers, 512 parts and 20
nested levels. Unsupported charsets/encodings and incomplete multipart messages
report errors. It is not yet a full MIME conformance implementation:
alternative-part selection beyond plain/HTML, richer attachment
types and unusual date/address syntax need more coverage. The main reader uses
plain text; the separate formatted reader supports received HTML and CID images
with scripts and remote resources blocked. Links remain inert, data-URL images
and external stylesheets are unsupported, and layout across a broader sample of
real newsletters still needs review. Each text/HTML body is bounded to 8 MiB,
decoded received snapshots to 32 MiB, and transfer JSON to 40 MiB. Excessive HTML
nesting/complexity falls back to the original plain-text alternative. Received
HTML is never inserted directly into the draft editor. Search uses SQLite FTS5
Unicode word tokenization, not Gmail query syntax or emoji substring search.
Thread actions currently apply to all cached conversation members, including
members outside the selected view. Snooze applies to those existing messages;
later replies are visible normally and do not inherit the earlier deadline.
Snooze is local. The running app checks every 30 seconds, including while
minimized; closed applications catch up at the next launch. There is no separate
background reminder service. The chosen UTC instant remains fixed when the
system time zone changes. Native snooze notifications are available, but new-mail
notifications and their account/privacy preferences still need implementation.

Due reminders remain in Mail until explicitly dismissed or rescheduled, even
after the system notification disappears or is suppressed by OS preferences.
The notification flag records successful queueing to the notification service,
not proof that the user saw it. A crash between queueing and acknowledgement can
repeat a notification; the stable notification identifier replaces an existing
one, and reminder identities protect newer snoozes from stale acknowledgements.
Reminder rows group currently related messages by thread and include accounts
excluded from the unified inbox. The date editor currently uses explicit numeric
date/time fields rather than a calendar picker or localized input syntax.

Rules match sender/subject using case-insensitive ASCII substrings, with all
specified conditions required; full Unicode case folding and richer predicates
remain pending. Rules run on new imported/Inbox messages, not historical remote
Sent/Drafts/Trash/Junk or already-cached mail. They are local Mail rules, not
server-side Gmail filters. Smart folders combine conditions with AND and use
the existing FTS5 search. Local draft search still uses case-sensitive substring
matching; draft tags/read/star/snooze metadata is not supported.

Tag management is scoped to one account. Non-Gmail tags remain local, while
Gmail label changes propagate on the next check. Global rule/view tag names do
not follow a single account's rename or removal. Empty local tag definitions,
tag merges, Gmail label colors/visibility and a dedicated conflict-resolution
workflow are not implemented. A failed label mutation stops that account's check
and leaves the operation and error available for retry in Tags. Each account's
sync must stay serialized; live-provider eventual-consistency behavior is not
covered by the disposable fixtures.

## Reproducing the native checks

`bash tools/cross-build.sh` builds the app and tests. With the disposable VM
running on local SSH port 2258, `bash tools/test-native.sh` copies them into
`/boot/home/mail-check`, runs core/storage/composition/reader/typography suites,
and retrieves `.cache/vm/smoke.png` and `.cache/vm/typography.png`.
The script uses atomic executable renames; do not truncate an executable that
may still be mapped by a process. It does not install system packages.

The preview requires the modern `summit_webkit` runtime and headers; see the
[build instructions](../README.md#build-and-test) for the explicit cross-link
library paths. `python3 tools/test-native-preview.py` copies the companion,
native preview test, core preview test and PNG fixture into the existing VM.
It runs the rendering and actual companion lifecycle checks against a local
HTTP probe and retrieves `.cache/vm/message-preview.png`. No real account,
external HTTP service or mail recipient is used.

For real Bridge engine coverage, prepare the adjacent Bridge checkout's vendor
tree and Haiku Go toolchain, then run `bash tools/build-bridge-fixture.sh` and
`python3 tools/test-native-bridge.py`. The first script writes only Mail's
`.cache/bridge` output and uses a Go source overlay. It records Bridge's source
revision and worktree status. `MAIL_BRIDGE_SOURCE` and `MAIL_BRIDGE_BUILD` override
these paths; normal Mail VM connection overrides also apply. The second script
copies `mail_bridge_tests` and `bridge-mail.test` into the existing VM using
atomic executable renames. It runs only `TestMailInterop`, prohibits a live
Proton API endpoint, and retrieves `.cache/vm/bridge-mail.log`. The fixture
generates all identities, credentials and certificates in private temporary
storage and closes both listeners after each engine instance.

`python3 tests/protocol_fixture.py tools/run-native-protocol.sh` runs native
protocol checks against this host's ephemeral TLS servers via QEMU's 10.0.2.2
gateway. `MAIL_NATIVE_TEST=mail_native_accounts python3 tests/protocol_fixture.py
--accounts tools/run-native-protocol.sh` runs account setup and sync through the
actual Interface Kit windows. On first use, allow the test app's get/add/remove
key-ring prompts in the disposable VM. Logs contain no passwords. The account
adapter redirects its log because a newly launched key server can otherwise
inherit SSH's output descriptors and keep the session open after the test exits.

`MAIL_NATIVE_TEST=mail_native_outbox python3 tests/protocol_fixture.py --outbox
tools/run-native-protocol.sh` drives delivery recovery against controlled SMTP
rejection and lost acknowledgements. Allow its test-app key-ring prompts in the
disposable VM. The adapter retrieves `.cache/vm/outbox.png`; the fixture checks
the exact SMTP submissions after the native test exits.

`python3 tools/test-native-attachments.py` copies the native attachment test and
its generated PNG into the running disposable VM. It checks image decoding,
file export and native cross-window drops, obtains measured widget coordinates,
and uses that VM's QMP socket to perform one physical drag onto Tracker. The
native test verifies and removes the resulting desktop file. The script saves
`.cache/vm/attachments.png`. `MAIL_VM_QMP` can override the QMP socket alongside
the usual SSH/build settings. A running app or another VM must not be restarted
merely because a test observation is slow.

`MAIL_NATIVE_TEST=mail_sent_tests python3 tests/sent_fixture.py
tools/run-native-protocol.sh` runs the stateful Sent-copy suite on air/OS.
`MAIL_NATIVE_TEST=mail_native_sent python3 tests/sent_fixture.py
tools/run-native-protocol.sh` drives native copy recovery and retrieves
`.cache/vm/sent.png`. Allow that disposable test app's key-store prompts.
These fixtures use a gateway IMAP account with Server saves Sent to model the
Bridge policy; the real Bridge preset still rejects non-loopback endpoints.

`python3 tests/google_fixture.py tools/run-native-google.sh` runs the same
OAuth/Gmail suite on air/OS. Its simulated browser and callback listener both
run inside the guest; only the temporary HTTPS provider fixture is on the host.
The fixture creates a one-day CA, verifies every request, and removes it at
exit. It never uses external accounts or sends real mail.

`MAIL_NATIVE_TEST=mail_gmail_sync_tests python3 tests/gmail_sync_fixture.py
tools/run-native-google.sh` verifies native Gmail cache and delivery integration
against a stateful HTTPS server. `tools/test-native.sh` also runs the standalone
Gmail storage suite, in addition to core, composition and reader smoke tests.

`MAIL_NATIVE_TEST=mail_native_google python3 tests/native_google_fixture.py
tools/run-native-google.sh` runs the Google Accounts/composer interaction test.
Allow the disposable test app's key-ring get/add/remove prompts on first use.
The adapter retrieves `.cache/vm/native-google.png`. No client registration,
live Google account or real recipient is used by this test.

`MAIL_NATIVE_TEST=mail_organization_sync_tests python3 tests/organization_fixture.py
tools/run-native-google.sh` runs native Gmail label journal/API checks.
`MAIL_NATIVE_TEST=mail_native_organization python3 tests/organization_fixture.py
tools/run-native-google.sh` drives rule, smart-folder and tag management and
retrieves `.cache/vm/organization-{rules,smart,tags}.png`. Allow that test app's
get/add/remove key-store prompts on first use. Run native HTTPS fixtures in
sequence because the adapter copies each temporary CA to the same guest path.

`python3 tools/test-native-snooze.py` runs the native snooze interaction test.
One process writes future snoozes and exits; another opens the database after
their deadlines. The script clicks measured coordinates in the real system
notification window, and the app verifies the resulting launch arguments and
reminder window. It also drives custom dates, DST selection, Cancel, early wake,
rescheduling and dismissal, then waits for the ordinary 30-second timer while
Mail is minimized. Captures are `.cache/vm/snooze-{notification,reminders,custom,live}.png`.
The test executable embeds its own app signature/launch resources. Only its
process uses the Hobart time zone; the guest's system zone is unchanged.

Earlier tests used `.cache/vm/mail.qcow2`, backed by
`../summit/.vm/airos-x86_64/disk.raw`. Font-cache validation uses the separate
`.cache/vm/mail-font.qcow2`, backed by the owned `.cache/vm/mail-font-base.raw`
copy. The latter now has the patched server in its qualification OS package,
using the normal launch path; the earlier user launch override was moved aside.
Its ISO marker was disabled so BFS writes persist instead of using the live RAM overlay.
Details and the unrelated live-overlay boot assertion are recorded in the
[platform notes](../platform/haiku/README.md). The guest
used 2 GiB RAM, two CPUs, KVM, an IDE disk, e1000 networking with port 2258
forwarded to SSH, a USB tablet and standard VGA. Its QMP socket is
`.cache/vm/qmp.sock`; PID and serial output are in the same directory. It was
stopped after validation. The original Summit VM was left untouched.

Current build SHA-256 hashes (execution coverage is recorded above). The
[Release manifest](evidence/release-build.sha256) records all optimized native
executables, including fixtures whose execution coverage is documented separately.
The shipping package's portable checksum and build-input records are alongside
the artifact in `artifacts/`.

```
da63c0d168e636495046ca10e758056cdb895410110c3c053deef2e9d18183d9  build-haiku/mail_reader_tests
b983a16bc9f40531923831f394fd2f73d7ffd2943b996428fe54041a00d21461  build-haiku/mail_native_reader
840be742c4c23933cc010b9b2e037267e8796b48083544810e98f32f493d3476  .cache/bridge/bridge-mail.test
0b30f079f3211dc05604f69e13384b636796b1484307f8d36df893788f3a68f5  build-haiku/Mail
750cbdb170461c13421d22ac36d0a11da3ee0bec96c4271d55461e04324f0c82  build-haiku/MailPreview
a35621cd2da3193c5c8dc095c573a2b91fcb3bc9ae9762bac896f37179619529  build-haiku/mail_bridge_tests
51a847c2d311db8a7bf211da44ebbbc7f48a623bd3e9960fb02b4b3e538b3a67  build-haiku/mail_compose_tests
1e4ec30b0f483a37dd15e71518bdfa10217dc55e629d5e29b27b4beb051d950a  build-haiku/mail_core_tests
c4402cda6209a93e8cfb05e6aad515eb3e5661102cc30386f7353eef880e2459  build-haiku/mail_gmail_store_tests
cd2620d178c1c2542f6219f336ea08216873ee0037adcfb22872d5136152b9d3  build-haiku/mail_gmail_sync_tests
bafa26a79e9d49f3050f6a5fa2cb549207941f678a949505be44757b0932a14c  build-haiku/mail_google_tests
c489a9a1ce0ad221796729775b76865b732fa20b3934fcba4b7ca83d48c28b3e  build-haiku/mail_native_accounts
25ae1fd4d602b4bc15c5c7da8a01ed7bb9e99bdf426be6f39e7bd4ad175065bb  build-haiku/mail_native_attachments
2fd7fc7266d1899e91af1b47ced87a35feeff77938d6a7ea2364690a402cd6f6  build-haiku/mail_native_composer
4a0b1f13f70b2f8253525acd1f5a5dc0142d70b20d82052a8e937ce076110af3  build-haiku/mail_native_google
929a76f555ca530e7af95a91f2763395d6efe98741c7cd881c2156a547db2ec8  build-haiku/mail_native_organization
d98ef21d8ae4fb80746a75e60df8557afda201383ae5bb8a91bcfaaa07142c97  build-haiku/mail_native_outbox
33438281576b625f72e2ffd313cd005c22d7469d3dbcb93a9a2702cc054d7813  build-haiku/mail_native_preview
36221b6d1cb6923be6fadb41d1243882707a7c626b80e2aba6268f9b5b415c12  build-haiku/mail_native_sent
8ddd0db6afa4ef19cbe458d981cf8ee0a869d3c6689df6bb119862d1b65c7017  build-haiku/mail_native_smoke
992a4217df9c522aeb730620d87c0c08e4bfd4153a96f83330044107001ca6d6  build-haiku/mail_native_snooze
e2e28d06d5a9fcd06407ef825eea1aaeba963c593b455002bcc5140a6c2bd9e4  build-haiku/mail_native_typography
40678ed8a9e740970f6f6260ae5d4420fd5b9471b38ba25df2feaa481d2259fe  build-haiku/mail_organization_sync_tests
9d219a8ad1166d932eab2e7ef06e6a4caca97f8c7e5e44026945f025387b0e33  build-haiku/mail_organization_tests
87bc57dc3540b3524aec701acbb83ef6c71227f693b17444943519b62725f01a  build-haiku/mail_preview_tests
11489ab5c02e9c36372d3fd969253f5fab9170f56898c2c0c6fe0688950136f6  build-haiku/mail_protocol_tests
bd250a19bccae0e2bb0cdc6cb6b2a593b550a06171cc04f630b74da62465b657  build-haiku/mail_sent_tests
7b67565310d9607ae4c14b7b32ba25350082e786d95c6d404a6cb908dc35ff4a  build-haiku/mail_snooze_tests
a09ba46c96bafa7ac8493b09e6008d51158ac2295720b65d55d8ea307ee900d3  resources/Mail.hvif
de6c18832938afc99caf132b39d6a30a19bac7f2e812e28db2535b4608d27551  third_party/noto-emoji/NotoEmoji.ttf
0f5df1bc5080543549645011780d26d33f9c7324d9c830f8a620f56238ed7d6d  build-haiku/mail_clipboard_tests
d2743557edd8e2831dee98ebabcb075e1fecc71d5896a6442d96f1eb9ee4f989  build-haiku/mail_native_inline_images
932f76303429906b9ef6a0ccca94849203cf3c5884aaac9c0a658f423a08c4fa  resources/InlineObject.ttf
604741909b635e0c269cc5e896e8a0dec12f01be22967ff9ad744b474ad71958  build-haiku/mail_text_clusters_tests
6788fe1019fcb6e591f2be713bbf05cfa56c9e2b787850896229bdf49fae45da  build-haiku/mail_native_graphemes
b157f275217ce2dcbad957e9bbc313360791cda449e5e1267d63db4c7fa6bef7  build-haiku/mail_native_emoji_sequences
1e3d975fe752bd04b842c6253cb3c4b0481a4a47774582b48cf0232a5e9350d5  platform/haiku/app-server-font-cache.patch
6ca524b957548383c62c15293587e2fa6b323af30e3fd5b579d421e6640eca63  .cache/haiku-font-build/objects/haiku/x86_64/release/servers/app/app_server
39566ce1c0c782dcdadf0b63fe1cc526bed2e8f92064a6f8fb963ff4a032cec0  build-haiku/mail_native_package
1e5fdb6e2ea722623a2aed637d1bfa17031a94924746927c214a0cd254ef59d4  build-haiku-release/Mail
ce7e257335c575bb789503e5463e4f60d155ba0a6479ae29f7605d77c289dfa9  build-haiku-release/MailPreview
06be7c13b92b0fccc2c64cdf105b2d32f4ab74d89a65348daa7a383cacf49923  build-haiku-release/mail_native_package
27e39716947b1bba7fb96f2b67002fd6a4ca3b901c07e8006676bf2c0e982381  docs/evidence/release-build.sha256
e164a316a5cf4dd4fe250756d60f0d2de6e24332f2b31a20e82fd4e9cf78b586  artifacts/airos_mail-0.1.0~dev1-1-x86_64.hpkg
768214d2036856e664db15e73f9a04227cd29f7326ce176f9eaf7184b855257a  artifacts/airos_mail-0.1.0~dev1-2-x86_64.hpkg
719313fa770748ca38dc70563537f43d2281c665adb667f5425c90c2d8acd859  .cache/vm/haiku-r1~beta6_hrev60206_1145-2-x86_64.hpkg
cb7b55a1809a35a721c7ef2c0391921efa6f20ab26430078aed7a278c35547f8  build-haiku/mail_move_tests
c9aa2f50de94cb74774c4388fe7d9cf7817d32ed249f358034dd40f942430db3  build-haiku-release/mail_move_tests
a522bc9407d99834bcf352ab72b1e753dadd9ec8ea7f32c2f79faa4653f1808a  artifacts/airos_mail-0.1.0~dev1-3-x86_64.hpkg
c24cee9d6e0c1762d467a3c2bc74bc8deed3dee898191d32e7c553848fc23eb4  .cache/mail-native-package-schema9
855f84a0aad2480a1fcd216e8f2c3862bc2235e46c49f982f1fbf6c492150155  artifacts/airos_mail-0.1.0~dev1-4-x86_64.hpkg
f17e4aff79b470b377163af1eb459e9f503d4249df32b6f06f341671aa3aaa2c  .cache/mail-native-package-schema10-v3
8d4a7f539191b6bfa5a36c2eda8bd505d9bfc34c16cf585a04be80983c547a72  build-haiku/mail_native_moves
63608cc6b1a20c5ca81e4837f88a0c56afc23690a7336f09b4f964303d57f08a  build-haiku-release/mail_native_moves
f17e4aff79b470b377163af1eb459e9f503d4249df32b6f06f341671aa3aaa2c  .cache/mail-native-package-schema10-v4
f56a6a38d84d7583a712ff4158d7bbfde4321ce0d0e1f14f5fee936784f64891  artifacts/airos_mail-0.1.0~dev1-5-x86_64.hpkg
aa51cac73bf9405474a6561884f05f7ff2edb4a791e9d5805716c2a32f9e1e64  build-haiku/mail_gmail_draft_tests
676f2bcff1e25f3aff3e1f8e1ef7cb5faceb4714a332b6e55ac915952c2d0295  build-haiku-release/mail_gmail_draft_tests
778eefc73f15699412bd5c266273273ca5ccce73a032b43432aaffd865bc5d92  .cache/mail-native-package-schema11-v5
6ac43499ee5ab147bf2932ff807da0d894ca732c6be74fc7f958f3ded1bf234e  artifacts/airos_mail-0.1.0~dev1-6-x86_64.hpkg
a9b1dd1188bc113d0dc0f64c6358ec258aa230cb67c52f51a054290502026b60  build-haiku/mail_draft_store_tests
1cf9f27ca11ca0e1f5f43fe1804d5aa55ad05a6280a11762cf33cfa9d75c9162  build-haiku-release/mail_draft_store_tests
fe4fab1c0e57d2faa87fb37e5ad5da641898ca6f38bffdb1709e5243fe196e5d  .cache/mail-native-package-schema11-v6
480a9659a39199de590b105171260cce1a615264d722115c6f3e33db1d5fd0b1  docs/evidence/release-package6.sha256
818c6595f302fb42af797d41fae990b422f426557fcbd5de55d936bbef4f20ac  build-haiku/mail_gmail_draft_sync_tests
26bb4a96212dce53c563469941cdff3455439a3e3fcc12fe5513612219740778  build-haiku-release/mail_gmail_draft_sync_tests
be906f4abb250fef5bacc142883cdcb55c40cd98922668e1e980e7dfef5bf9e0  build-haiku/mail_native_drafts
3aef3bdc547e4d2182ceb46cdff593f7e9d511fbd801c911996ca81c6e7ed769  build-haiku-release/mail_native_drafts
0a2b601b1d7a3d454908650e007520660928fc41786aa1fe5aaedfd1d9d6105e  .cache/mail-native-package-schema12
89a62b1e7a86b28519412b6450e9bfe055ab0dd49f7ef06dce375679f5b55616  docs/evidence/release-draft-journal.sha256
7580b9f3f1ae8c0cc2a38d4f3bf2b51b20a70bd00b6efcf885fd83c2cb4aaadb  .cache/mail-native-package-schema13-v7
b5134a968b811cd8c142c85a5bc96f86202d1cace8e49ecadf757e7c6399ef6e  artifacts/airos_mail-0.1.0~dev1-7-x86_64.hpkg
ef3ee008c2c0fd07a8c42716a26659ef35a99ebd4514889a5dbc8dfbce970a82  docs/evidence/release-package7.sha256
e69f600353683b78b8c8d2f888e816ea5536c70f002b16d761fe3b30d372842e  .cache/mail-native-package-schema14-v8
03066e56e915f9f56971b9378b0de5daee4fb947d6fa22a532d0552e28492b7a  artifacts/airos_mail-0.1.0~dev1-8-x86_64.hpkg
3dd4e550d1e4add9e0fd020625fd4433931155af60f09f99f242a57da790ab77  docs/evidence/release-package8.sha256
b0e4a7aef992748b8af5fe8ce61dbf2d05c74e9586f581ae0ebbc6ec00631afd  build-haiku/mail_imap_draft_tests
b230bf1a7b35b95a3b0f9c00c35afa9dba6919c88ee7828db8988490063fb529  build-haiku-release/mail_imap_draft_tests
06be7c13b92b0fccc2c64cdf105b2d32f4ab74d89a65348daa7a383cacf49923  .cache/mail-native-package-schema14-v9
27e39716947b1bba7fb96f2b67002fd6a4ca3b901c07e8006676bf2c0e982381  docs/evidence/release-package9.sha256
3daea5ef29b10014fc1a654d382397c56e84d002f0d8deeb0fc15809c2df861a  artifacts/airos_mail-0.1.0~dev1-9-x86_64.hpkg
```
