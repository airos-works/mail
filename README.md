# Mail for air/OS

A native C++17 / Haiku Interface Kit email client. The development release
receives IMAP, POP3 and Gmail API mail, composes and sends through SMTP or
Gmail, and organizes a persistent local cache. Native Google sign-in and token
refresh pass disposable fixture tests; a registered Desktop OAuth client and
live-account validation are still needed.

The application includes multiple Gmail API, IMAP/SMTP, POP3/SMTP,
and Proton Mail Bridge accounts, a unified inbox, conversations, a rich text
composer with emoji and image/file drag and drop, automatic filters, snooze,
smart folders, and tags. See [acceptance and limits](docs/ACCEPTANCE.md) for the
requested feature coverage and [implementation status](docs/STATUS.md) for the
detailed test history and future enhancements.

## Current build

- Native resizable three-pane inbox, keyboard menu shortcuts, system colors,
  multi-selection, and standard file panels.
- Add/edit/remove IMAP, POP3, and local Proton Mail Bridge account settings.
  Passwords are stored through Haiku's BKeyStore; TLS and STARTTLS connections
  verify server certificates.
- Google account setup through the system browser, verified email identity,
  key-store tokens, coordinated refresh, reauthorization and explicit revocation.
  See [Google setup](docs/GOOGLE_SETUP.md) to configure the required client JSON.
- Background receive at startup, after closing Accounts, every five minutes,
  or with Check mail. Stop cancels the active check. IMAP downloads preserve
  unread state; POP3 uses UIDL and retains messages on the server.
- Server folders and read/star changes, with a durable journal for local
  edits. IMAP moves use MOVE when available, or COPY plus targeted UID EXPUNGE
  on UIDPLUS servers. A persistent journal resumes known copies after reconnects;
  uncertain copies are verified before removing their originals. IMAP4rev2 is
  supported as well. A mapped destination is required.
- **File → Folder moves…** reviews queued and interrupted IMAP moves, including
  their account, source, destination and latest error. Check saved progress,
  explicitly retry an unconfirmed copy, keep a local copy, or cancel a move
  that has not started. Retry and keep-local confirmations default to Cancel.
- Import `.eml` files, read conversations, and save decoded attachments.
- Native composer with sending-account selection, To/Cc/Bcc, reply/reply-all,
  forward, bold/italic/underline, links, an emoji menu, and saved drafts.
- File/image drops and file-panel attachments. Images dropped into the editor
  appear as native bitmaps at the drop position. Click an image to select it;
  cut/copy moves or repeats it, and arrow keys and Delete cross it as one object.
  Decoding runs in the background and images fit the text column. Unsupported
  or damaged images retain named `[Image: filename]` placeholders.
  **Insert in message**, **Keep as attachment**, and **Remove attachment**
  also update the body. Removing a file's last image placement keeps it
  as a regular attachment. Undo/redo restores text, formatting, links, images
  and attachment changes together; consecutive typing is grouped. **Undo**
  (Command-Z) and **Redo** (Command-Shift-Z) retain up to 100 body edits within
  a 64 MiB history budget, sharing attachment bytes. History lasts for the open
  composer; address and subject fields use Haiku's native single-step history. MIME includes
  plain/HTML alternatives, CID images, Unicode filenames and binary data.
- Drag received or draft attachments to Tracker to save a copy, or into another
  Mail composer to attach them. A drag retains the original bytes if the source
  selection changes. Attachment rows and preview filenames render emoji.
- The message body accepts HTML clipboard content from other apps. Bold,
  italic, underline, supported links and embedded base64 images become editable
  draft content. Paragraphs, lists and table cells retain readable text structure.
  Copy offers self-contained HTML alongside native text/font runs and Mail's
  exact image fragment. An entire rich paste remains one Undo step.
- Preview images from the reader or composer using installed Haiku translators.
  Decoding runs off the window thread, preserves transparency, and fits large
  images into a resizable preview window. Closing a loading preview cancels it.
- **Preview message** in the composer (also **Message → Preview message**, Cmd-P)
  renders the saved draft's formatting and positioned images in a separate
  native WebKit window. It shows recipients and attachments, fits wide images,
  and provides zoom, Refresh and Edit message controls. Refresh saves current
  composer edits and updates the same preview. Links are inactive; images and
  the emoji font come from the draft snapshot and bundled resources.
- Where the system draws emoji in color (air/OS since October 26, 2026),
  Mail leaves them to it: color glyphs, sequences and flags come from the
  system's fonts, and the message preview prefers Noto Color Emoji. Mail
  finds out by drawing an emoji at start; `MAIL_PRIVATE_EMOJI=1` keeps the
  embedded font. Elsewhere:
- An embedded Noto Emoji font supplies monochrome glyphs in the reader,
  conversation list, subject, composer and emoji menu. Formatting, clipboard
  text and draft MIME retain the original Unicode. Caret movement, selection,
  deletion and clipboard operations keep joined emoji, flags, skin tones and
  combining characters intact. HarfBuzz shapes supported joined emoji, flags,
  skin-tone sequences and keycaps into single native glyphs. Native emoji use
  Noto's designed regular and bold weights, preserving small-label detail without
  a fixed synthetic outline. This build requires
  the [app_server font-lifetime fix](platform/haiku/README.md). Unsupported
  sequences retain scalar fallback; OS window-title font fallback remains incomplete.
- Draft autosave and an outbox with queued/sending/failed/uncertain states.
  Successful SMTP delivery creates a local Sent copy. Interrupted sends never
  retry automatically. A persistent Outbox button shows the message count and
  delivery problems even while receiving mail. Retry and Return to draft follow
  the current state; uncertain deliveries require an explicit confirmation.
- Gmail draft import and background saves, with original MIME retained until
  an edit. Active composers keep their local text when another device changes
  the draft. **File → Draft synchronization…** shows progress and lets you keep
  both conflicting versions, or keep local edits after a remote deletion.
- IMAP Sent uploads with a separate durable copy journal. Server-created copies
  are reconciled by Message-ID while preserving local organization. Bridge
  defaults to server-managed Sent. Missing receipts trigger checks, with explicit
  copy retry and keep-local actions in Outbox; neither resends to recipients.
- Durable SQLite mailbox with account separation and full-text search.
- Threading by Message-ID/References/In-Reply-To; provider thread IDs when
  supplied. Out-of-order ancestors merge existing conversations.
- Archive/trash/restore, read/unread, stars, tags, custom snooze times, saved search
  views, and rules for newly imported or received messages.
- Permanent deletion from Trash. In a Trash view, **Delete** (the key, the
  toolbar button, or **Message → Delete permanently…**) removes the selected
  messages for good after a confirmation that defaults to Cancel;
  **Message → Empty Trash…** removes everything in the Trash being shown, or
  in every account's Trash from any other view. Messages disappear at once.
  IMAP and Proton Mail Bridge messages are removed from the server by the next
  mail check, with STORE `\Deleted` and a UID EXPUNGE of that one message; a
  server without UIDPLUS keeps the message marked `\Deleted` (Mail never sends
  a plain EXPUNGE, which would also remove other clients' deleted mail). Gmail
  messages stay in Gmail's own Trash, which Gmail empties after 30 days:
  deleting through the Gmail API needs the full `https://mail.google.com/`
  access, more than the `gmail.modify` access Mail asks for. POP3 mail is
  removed on this computer only. An interrupted removal is journaled and
  finishes on a later check; one the server refuses puts the message back.
- Snooze presets, validated local dates/times and an explicit choice for repeated
  daylight-saving hours. Durable reminders survive reopening Mail; a persistent
  count and native notifications lead to open, snooze again and dismiss actions.
- Native rule and smart-folder editors with account scope, rule ordering,
  enable/disable, and removal. Account-scoped tag management includes durable
  Gmail label renaming and removal, with pending changes and failures visible.
- MIME multipart, quoted-printable/base64, encoded headers, character set
  conversion, inline-image metadata, and binary attachment storage.
- Haiku's own Mail icon, since this Mail replaces Haiku's.

The main window shows the selected conversation formatted, in place, using the
system web engine: each message appears as a card (older read messages are
collapsed; click a header to expand it), with HTML, inline images, quoted text
and attachment names. **Plain text** (Shift-Command-T) switches to the text
alternative, and **Open in window** (Command-P, or double-click) still opens the
separate preview window. Without the web engine, the reader falls back to plain
text. A conversation is marked read after it has been shown for 1.5 seconds.
Scripts, forms and embedded documents are removed and remote content is blocked.
Clicking an http(s) link opens it in the preferred browser; mailto links open a
new message. Missing and remote images leave readable placeholders. The raw
message and HTML are retained. `MAIL_READER_TRACE=1` logs the reader's
requests and page loads to standard error.

The sidebar is an outline: unified mailboxes, then each account (its row is its
inbox) with its server folders, smart folders and tags. Icons are Haiku's own
vector artwork (see `resources/icons` and `tools/extract-haiku-icons.py`).
Unread counts appear beside mailboxes. Mail checks and imports show a progress
bar with the account, folder and message count; the toolbar's Check mail button
becomes Stop while a check runs. Window size, pane widths, collapsed sections and
the last mailbox are remembered beside the database (`mail.db.window`).

**File → Make Mail the default email application** makes Mail open email files
and mailto links instead of Haiku's previous Mail. `Mail message.eml` imports
messages and `Mail mailto:someone@example.com` starts a new message.

Import currently uses the resolved source path
as its stable identifier; importing the same file again updates its contents
without undoing local organization. Moving the source file and importing it
again can create a second copy.

Open **File → Accounts** to configure both incoming and outgoing servers. Use
the provider's account or app password. For Proton Mail Bridge, use the username
and password supplied by Bridge and its local ports. For its certificate, use
**Get from server…** beside **Trusted certificate** (Mail shows the certificate
and its fingerprint and saves it under `~/config/settings/Mail/certificates`
when you trust it), or **Choose…** an exported PEM/DER file. Pasted PEM text is
also accepted and saved to a file; verification stays
enabled. See [Bridge setup](docs/BRIDGE_SETUP.md) for the export and account
settings. The native Mail transport is checked against the real air/OS Bridge
engine with a disposable local Proton API; a live Proton account and the
installed Bridge GUI still need validation.

For Gmail, follow [Google setup](docs/GOOGLE_SETUP.md), then choose
**Google (Gmail) → Sign in with Google** in Accounts. Close Accounts to receive
mail. The native receive/send workers obtain credentials from the system key
store and refresh them when needed. Gmail labels appear as tags; read, star,
tag, archive and trash changes are sent on the next check.

Use **New message** to compose, or the Message menu to reply or forward. Closing
the composer saves the draft locally; double-click it in Drafts to continue.
The **Outbox** button (also **File → Outbox**) shows delivery failures, retry
controls, and Return to draft. Failed and uncertain messages remain paused until
you choose an action. Check Sent and the server before retrying an uncertain
delivery; returning it to Drafts does not recall an already delivered message.
Queued messages resume when Mail starts. Gmail drafts synchronize after a pause
in editing and during mail checks; other providers' drafts remain local. Gmail
records the API send receipt and reconciles it with the server's Sent message.

In **Accounts → Sent copies**, **Automatic** uploads new SMTP Sent messages for
IMAP accounts and lets Proton Bridge save its own copies. POP3 keeps local Sent
mail. Choose **Server saves Sent** when another IMAP service also saves sent
messages; this avoids a competing client upload. The optional **Sent folder**
field accepts a listed server mailbox name. Leaving it blank uses one advertised
Sent folder; missing or ambiguous mappings remain visible in Outbox.

Outbox lists Sent-copy work separately from delivery problems. **Check server**
looks for an existing copy. **Retry saving copy…** requires confirmation after an
interrupted upload and can create another server copy, but does not send to
recipients again. **Keep local copy** stops checking that entry without deleting
local or server mail. Messages sent by versions before schema 6 are not uploaded
retroactively. Servers that rewrite Message-ID, or create a delayed copy after
Mail has checked and uploaded, can still produce duplicates.

Tags on non-Gmail accounts, snooze, and POP3 organization are local. IMAP changes
are sent on the next mail check. Missing destination folders
or unsupported server capabilities leave moves pending and display an error.

Use **Organize → Rules…** to create, edit, reorder, disable or remove rules.
From and Subject conditions both match when specified; matching currently uses
case-insensitive ASCII substrings. Rules run in order on newly imported messages
and newly received Inbox mail. They can add a tag, move mail, mark it read, star
it, or stop later rules. Changing a rule does not reorganize cached messages.
These rules are stored in Mail and run while it receives mail.

**Organize → Smart folders…** edits saved views by account, folder, search words,
tag, unread/starred state, attachments and snooze. All selected conditions must
match. “Accounts in All inboxes” respects each account's unified-inbox setting.
Editing a selected view updates its results immediately; removing it keeps mail.
**Save view as smart folder…** starts an editor with the current view's criteria.

**Message → Add tag… / Remove tag…** changes selected conversations.
**Organize → Tags…** renames or removes a tag throughout the chosen account,
including unused Gmail labels. Gmail changes are queued until the next check;
the editor shows pending work and errors. Removing a Gmail label also removes
it from Google and other devices, while retaining messages. Rules and smart
folders scoped to that account follow renames; all-account criteria retain their
original tag names. Removing a tag clears scoped rule actions but keeps saved
view criteria, so a view cannot unexpectedly expand to every message.

Choose **Snooze…** to return selected conversations in one hour, tomorrow at
09:00, next Monday at 09:00, or a custom local date and time. A skipped clock
time is rejected; when clocks repeat, choose the first or second occurrence and
check the displayed UTC offset. Saved deadlines preserve their instant if the
system time zone later changes. Messages return to their existing folders and
retain their read/star state. **Message → End snooze** brings them back early.
Each account has its own **Snoozed** view, including accounts excluded from All
inboxes. Snoozing is local to this computer and does not synchronize to providers.

The **Reminders** count persists until you dismiss or reschedule due reminders.
Open it, or **File → Reminders…**, to review conversations from any account.
Clicking a system notification opens that list. Mail checks deadlines every
30 seconds while running, including when minimized. Keep it running for timely
notifications; when closed, it catches up on overdue reminders at the next
launch. No separate background service is installed. A reminder can remain in
Mail even when system notification preferences suppress its notification.

## Build and test

On Haiku, install the development packages for SQLite (with FTS5), libiconv,
libcurl with TLS support, OpenSSL (libcrypto), libxml2 and ICU 74 or later
(uc/i18n), plus CMake 3.20 or later and Ninja. The
message preview also needs air/OS's modern `summit_webkit` runtime and its
`WebKit/WebKitView.h` development headers (C++23). The main application remains
C++17. Keep the built `MailPreview` executable beside `Mail`. Then run:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/Mail
```

The normal database is `~/config/settings/Mail/mail.db`. To use disposable data:

```sh
./build/Mail --database /boot/home/mail-test/mail.db
```

The parent directory must already exist. The app uses a private umask and sets
its normal settings directory to mode 0700. SQLite stores mail content, not
passwords or tokens. Account passwords and Google tokens use Haiku's key store. The OS asks for
key-ring access when it is first needed.

When SQLite reports damage in the database, Mail leaves `mail.db.needs-repair`
beside it and repairs it at the next start: the `sqlite3` command's `.recover`
copies every readable page into a new database, which must pass SQLite's
integrity and foreign-key checks before it replaces the damaged one. The
damaged files are kept as `mail.db.damaged-<date>-<time>`. Messages that could
not be recovered download again: Gmail restarts with a full check, and IMAP
compares every folder anyway. If nothing usable can be recovered, Mail offers
to set the damaged database aside and start empty.

The core builds and runs on Linux too; the Interface Kit application is built
only for Haiku:

```sh
cmake -S . -B build-host -G Ninja -DMAIL_SANITIZERS=ON
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

The workspace cross build is `bash tools/cross-build.sh`. `MAIL_CXX`,
`MAIL_SYSROOT`, `MAIL_TOOLS`, `MAIL_BUILD`, and `MAIL_BUILD_TYPE` override its local defaults.
It produces `build-haiku/Mail`, `build-haiku/MailPreview` and the native tests,
and embeds the icon as a `BEOS:ICON` vector resource. If the SDK lacks the modern
WebKit link library, set `MAIL_WEBKIT_LIBRARY` to the matching runtime's
`libWebKit.so.1` and `MAIL_WEBKIT_INCLUDE_DIR` to the directory containing its
`WebKit` headers. These options also work as CMake `-D` arguments. The workspace
validation uses `.cache/webkit/libWebKit.so.1`, copied read-only from the test
VM's installed `summit_webkit-1.10.1~git20261011.1233-1-x86_64` package, with the
SDK's `boot/system/develop/headers/summit-webkit` headers. Shared SDK files and
system packages are not modified. The preview loads the installed engine from
`/boot/system/lib/summit-webkit/lib`; the legacy Haiku WebKit library is not a
substitute.

Native builds also require HarfBuzz and its subset library (validated with
14.2.0); the raster comparison test uses FreeType (validated with 2.14.3).
It requires an air/OS package providing `haiku_private_font_cache >= 1`.
For x86_64 air/OS `hrev60207+1733`, install the matching
[font-cache OS update](docs/FONT_CACHE_UPDATE.md) and reboot before installing
Mail. This is a capability of the fixed base system, not a separate library.
The [private-font lifetime fix](platform/haiku/README.md) is merged into the
owner's air/OS source; the updated OS package is validated in a separate VM.
The shared OS installation has not been changed.

An x86_64 development package is available under `artifacts/`. See
[installation instructions](docs/INSTALL.md) and [package building](docs/BUILD_PACKAGE.md).
It installs `Mail` as `apps/Mail`, replacing Haiku's Mail, with its preview
helper in `lib/mail`, the **Applications → Mail** Deskbar entry and Haiku's Mail
icon. air/OS x86_64 images build it from the pin in airos-ci's
`apps/mail.lock.json` and leave Haiku's Mail out.
The native package test exercises install, real application/preview launch,
painted image pixels, draft editing, upgrade, signature-based reopen and
uninstall with saved data intact.

The build also includes `mail_native_smoke`, an Interface Kit integration test
for import, reading, layout, archive, search, and native icon rendering, plus
`mail_native_typography` for embedded glyphs, formatted emoji, typing after
emoji, clipboard portability, undo/redo and semantic draft styles.
`mail_native_composer` checks image positioning, multilevel rich undo/redo,
typing styles, history limits, focused native shortcuts, cross-editor
clipboard transfer, external HTML import/export and fallback, legacy drafts,
delayed attachment drops, reopen and queued MIME. `mail_clipboard_tests` checks
the bounded HTML parser, image transfer and malformed input handling.
`mail_native_inline_images` checks actual bitmap pixels and transparency in the
composer, reflow, image selection, keyboard deletion, scroll preservation,
clipboard fonts, Undo/Redo, draft reload and decoder fallback. Run them
from the build directory on Haiku. `mail_native_graphemes` checks Unicode-aware
editing and native subject-field undo. `mail_text_clusters_tests` checks byte
boundaries against the official corpus matching ICU's Unicode version; see
[corpus provenance](tests/data/unicode/README.md).
`mail_native_emoji_sequences` checks joined glyph pixels against the original
font at regular/bold weights and label sizes, shared-family capacity, styles, native caret advances, incremental typing,
label clipping, bounded caching and Unicode MIME.
Set `MAIL_EMOJI_STYLE_GRID` to a PNG path when running it on Haiku to save a
comparison of designed weights and the former synthetic-bold defect.
`bash tools/test-native.sh` runs the tests
on the disposable workspace VM; connection settings can be overridden with
`MAIL_VM_PORT`, `MAIL_VM_KEY`, and `MAIL_VM_KNOWN_HOSTS`.
After that upload, `bash tools/test-native-font-lifetime.sh` runs ten pairs of
new emoji and typography processes and requires app_server to retain its team ID.

`python3 tools/test-native-preview.py` runs the modern WebKit preview in that VM.
It checks actual PNG pixels, SVG scaling, Unicode/font loading, formatting,
inactive links, blocked fetches, snapshot permissions/removal, refresh and the
composer's real companion launch/close lifecycle. A local HTTP probe verifies
that previewing makes no network requests. Its screenshot is saved as
`.cache/vm/message-preview.png`.

`MAIL_NATIVE_TEST=mail_native_reader python3 tools/test-native-preview.py` checks
received HTML in the same helper, with attached-image pixels, stylesheet layout,
script denial, plain-text switching, conversation selection and the actual Mail
reader launch/refresh/close flow. A separate HTTP probe checks that hostile
markup and CSS make no network requests. The capture is
`.cache/vm/received-reader.png`. `mail_reader_tests` checks the bounded HTML
sanitizer, CID matching, snapshot validation and malformed HTML on host and Haiku.

To check actual Bridge interoperability, first prepare the adjacent
`proton-bridge` checkout's vendor tree and Haiku Go toolchain using its build
instructions. Run `bash tools/build-bridge-fixture.sh`, then
`python3 tools/test-native-bridge.py` with the Mail VM running. `MAIL_BRIDGE_SOURCE`
and `MAIL_BRIDGE_BUILD` override the source and fixture-output paths. The build
uses a Go overlay to add Mail's integration test; it leaves Bridge sources,
the shared SDK and the Go runtime unchanged. Both the real Bridge engine and
Mail's production transport run natively inside the VM. The Proton API and
identities are disposable, and neither a personal account nor the installed
Bridge GUI/key store is used. The fixture checks certificate/password rejection,
rich delivery and BCC, two sending identities, replies/threading, read/star,
archive/trash/restore, single Sent copies and reopening across an engine restart.
An independent Go IMAP client verifies server counts, sender identities, flags
and BCC privacy. Logs are saved to `.cache/vm/bridge-mail.log`.

With Python 3 and OpenSSL installed, host CTest also runs real TLS/STARTTLS
IMAP/POP3/SMTP and HTTPS OAuth/Gmail fixture servers. To run those fixtures
against the native binaries:

```sh
python3 tests/protocol_fixture.py tools/run-native-protocol.sh
MAIL_NATIVE_TEST=mail_native_accounts python3 tests/protocol_fixture.py --accounts tools/run-native-protocol.sh
MAIL_NATIVE_TEST=mail_native_outbox python3 tests/protocol_fixture.py --outbox tools/run-native-protocol.sh
python3 tests/google_fixture.py tools/run-native-google.sh
MAIL_NATIVE_TEST=mail_gmail_sync_tests python3 tests/gmail_sync_fixture.py tools/run-native-google.sh
MAIL_NATIVE_TEST=mail_native_google python3 tests/native_google_fixture.py tools/run-native-google.sh
python3 tools/test-native-attachments.py
python3 tools/test-native-snooze.py
MAIL_NATIVE_TEST=mail_sent_tests python3 tests/sent_fixture.py tools/run-native-protocol.sh
MAIL_NATIVE_TEST=mail_native_sent python3 tests/sent_fixture.py tools/run-native-protocol.sh
MAIL_NATIVE_TEST=mail_organization_sync_tests python3 tests/organization_fixture.py tools/run-native-google.sh
MAIL_NATIVE_TEST=mail_native_organization python3 tests/organization_fixture.py tools/run-native-google.sh
```

The account UI test uses the disposable VM's key store; approve its test-app
access prompts in that VM. It verifies account add/edit, password save/update/
remove, background IMAP receive, native formatting and file/image drops, draft
reopening, SMTP Send, and the local Sent copy. No real account is contacted.

The Outbox test drives the persistent indicator, native recovery buttons,
Cancel/Confirm dialogs, rich draft restoration and an action racing an active
send. Its SMTP fixture verifies the exact submissions, including explicit
retries after rejection or a lost acknowledgement, with no automatic resend.

The Sent-copy fixtures cover IMAP APPEND/APPENDUID, missing and malformed
receipts, definite rejection, exact Message-ID search, server-managed copies,
folder selection, durable recovery and local edits before reconciliation. The
native recovery test drives Cancel, explicit copy retry, active-check guards
and Keep local copy. Its server sees one SMTP delivery and two IMAP uploads.

Organization checks cover rule order and stable editor identities, scoped local
tags, persistent Gmail label changes, older responses arriving after newer edits,
rejected/lost responses, and retained mail after label removal. The native test
drives the actual rule, smart-folder and tag forms against a separate HTTPS
fixture. It verifies received rule actions on the server, Cancel/Discard/Remove
dialogs, selected-view updates, local draft criteria and message tag removal.

Snooze checks cover leap days, skipped/repeated clock hours, half-hour daylight
saving changes and a skipped calendar day, plus atomic selection updates,
restart persistence, stale acknowledgements, early wake and dismissal. The
native fixture seeds future snoozes in a separate process, waits for expiry,
then starts the UI and physically clicks the OS notification through QEMU.
It drives the custom-time and reminder forms and checks the ordinary timer while
Mail is minimized. Only the test process's time zone is changed; system settings
and real mail accounts are untouched.

The attachment test verifies decoded PNG pixels and transparency, native image
previews, reader-to-composer transfers, immutable binary export, MIME attributes,
private permissions and destination collisions/symlinks. It uses measured widget
coordinates and QEMU pointer events to drag a received attachment onto Tracker's
desktop, verifies its bytes, and removes the disposable output. Its four-color
PNG is a generated test fixture. No external account or image service is used.

`mail_compose_tests` checks draft/outbox transactions and MIME composition.
Host CTest also parses generated messages with Python's independent email
parser. Schema 9 stores image placement metadata, and tests check HTML order,
reused CID references, Unicode alternative text, binary files, reopen and
delivery recovery. The native composer draws bounded image previews while
preserving original attachment bytes and sizes for save/send. Automatic preview
decoding accepts PNG, JPEG, GIF, BMP, TIFF, WebP and ICO containers through the
installed translators; other formats, including SVG, keep named markers in the
editor. Preview bitmaps fit within 512 × 320 pixels and shrink with the text
column. Image resizing/cropping and animation are not implemented. The separate
whole-message preview renders the message's HTML and original image resources.
An application-private blank font supplies native inline-object metrics; it is
never installed system-wide or exported with copied text. Its original generated
asset can be rebuilt with `python3 tools/make-inline-font.py` using fontTools
(generated with version 4.67.0); fontTools is not a Mail build/runtime dependency.
HTML clipboard import/export is bounded to 8 MiB. Larger exports keep native
text/font data and Mail's binary fragment; invalid or oversized imports fall
back to available plain text. Remote, local-file and unresolved CID image URLs
are not fetched and paste as alternative text. HTML stylesheets, font sizes,
colours and full list/table layout are not retained; supported inline font
properties are converted to Mail's semantic styles. Embedded image bytes are
owned by the resulting draft and are preserved through undo, save and sending.
The preview regenerates the same body HTML used for delivery, with inactive
links and private image URLs. A mode-0600 snapshot in the mode-0700
`~/config/cache/Mail/previews` folder transfers the draft to `MailPreview`; the
helper reads and immediately removes it. The renderer uses a private context
and a restrictive content policy. It does not read the mail database or hold
account credentials. A crash before the helper consumes a snapshot can leave
that private cache file behind. Mail recipients' clients may use different
fonts and layout rules.

Received HTML is parsed with libxml2 and rebuilt from allowed display elements
and attributes. The helper enforces a Content Security Policy and a sandbox
without script permission. It exposes only exact resource URLs for the current
message and the bundled font. Bodies are limited to 8 MiB per text/HTML
alternative, decoded message snapshots to 32 MiB, and transfer files to 40 MiB.
Excessively nested or complex HTML falls back to its plain-text alternative.
External images, external stylesheets, active links, embedded data-URL images
and cross-client layout fidelity remain unsupported.
Protocol tests cover
SMTP TLS/STARTTLS, rejection before DATA, and
connection loss after accepted DATA without automatic resend.

`mail_google_tests` verifies the portable Google transport with a simulated
browser that follows real HTTPS authorization redirects to the app's actual
loopback listener. It covers PKCE, forged callbacks, consent denial, refresh,
revocation, TLS and redirect rejection, labels, history pages, and sending with
an independently parsed MIME envelope. All 94 assertions pass on both the host
and air/OS. No Google account or external recipient is used.

`mail_gmail_draft_tests` covers the draft API and MIME import/export foundation:
stable draft IDs, changed message IDs, paginated searches, BCC, quoted Unicode
names, repeated CID image positions, binary attachments, and uncertain save/send
responses. It also submits journal-linked drafts through the real outbox worker,
including changed server content, lost receipts, rejection and removed drafts.
The HTTPS fixture independently parses MIME and counts mutations. All 117 checks
pass on the host and air/OS.
Run `ctest --test-dir build-host -R mail_gmail_drafts`, or on the existing VM:

```sh
MAIL_BUILD=build-haiku-release MAIL_NATIVE_TEST=mail_gmail_draft_tests \
python3 tests/gmail_draft_fixture.py tools/run-native-google.sh
```

`mail_draft_store_tests` adds 125 checks for draft versions and the persistent
Gmail draft journal: concurrent connections, stale edits/send/discard,
immutable upload snapshots, restart, late receipts, account changes, discard
cleanup, review snapshots, recovery decisions, schema-13 migration and
coordination with queued/uncertain delivery. The native composer
uses draft versions; unchanged saves retain their version and modification time.

`mail_gmail_draft_sync_tests` adds 40 host/native checks for remote import,
background create/update/delete, exact recovery after interrupted writes,
active-editor protection, remote deletion, preserving conflicting versions,
choosing uncertain copies and explicit retry with a new identity.
`mail_native_drafts` exercises the actual composer and review windows against
the disposable HTTPS provider, including automatic upload and immediate Close.
Run the native UI fixture with:

```sh
MAIL_BUILD=build-haiku-release MAIL_NATIVE_TEST=mail_native_drafts \
python3 tests/gmail_draft_sync_fixture.py tools/run-native-google.sh
```

Current source builds and package revisions 8–9 use schema 14. Packages through
revision 7 cannot reopen a migrated profile. Use a closed-app
backup or separate profile if testing those older builds.

The composer shows local/Gmail save status. **Review saved versions…** opens
the same window as **File → Draft synchronization…**. **Check Gmail** reconciles
an interrupted save; **Keep both versions** preserves conflicting local text
and attachments in a separate draft while keeping the server version. Close
the composer and return queued mail to Drafts before resolving a conflict.
Clean, closed drafts adopt remote edits and confirmed deletions automatically.
Draft list omissions alone never delete local work.

Automatic recovery requires a unique exact match for an uncertain create.
Ambiguous or provider-rewritten results remain held for review. Select a listed
copy and choose **Use Gmail copy…** to link it while preserving different local
work. **Retry save…** explicitly authorizes another copy with a fresh Message-ID;
inspect Gmail first because the earlier upload may appear later. Both actions
require a closed, unqueued draft and default to Cancel. **Stop checking…** for
a discarded draft leaves its Gmail copies intact. Recovery snapshots persist
across restart, with a limit of 100 copies and 64 MiB per search.
A different client can also edit between
Mail's check and update, since these are separate API requests. IMAP server
drafts and live Gmail qualification remain open work.

`mail_native_google` drives the Accounts and composer windows on air/OS. It
verifies key-store persistence, concurrent expiry/401 refresh, cancellation on
close, wrong-account rejection, reauthorization, duplicate-account prevention,
background Gmail receive, reply/send, Sent deduplication, revoke and reconnect.
It checks that tokens do not enter SQLite. Its simulated browser follows a
disposable provider's redirect to the real loopback listener; live Google
consent in the target system browser remains unverified.

Mail accepts a downloaded Google **Desktop app** client JSON and ignores any
endpoint URLs in that file. No client registration or live credentials are
bundled. Follow [Google setup](docs/GOOGLE_SETUP.md) for configuration. The JSON
parser is vendored with its license and checksum in [third_party](third_party/README.md).

Gmail cache and delivery integration has 45 storage assertions and 36 HTTPS
sync/outbox assertions passing on both host and air/OS. One cached message can
belong to multiple labels; local edits survive stale snapshots. History cursors
advance only after complete reconciliation, and full-refresh recovery confirms
deletions individually. Filters run on newly cached Inbox mail. Gmail delivery
records the provider's message/thread IDs, so subsequent synchronization reuses
the Sent copy. Interrupted uploads remain uncertain and are never automatically
resent. These checks use disposable fixtures and the same SQLite/outbox core as
the application, including the native Gmail receive/send workers.

Both core and native tests pass on the air/OS test image. See the
[native inbox capture](docs/evidence/native-inbox.png) and
[account setup capture](docs/evidence/native-accounts.png),
[Google account capture](docs/evidence/native-google.png),
[composer capture](docs/evidence/native-composer.png),
[emoji typography capture](docs/evidence/native-typography.png),
[joined-emoji capture](docs/evidence/native-emoji-sequences.png), and
[verification record](docs/STATUS.md).

`tools/extract-haiku-icons.py` copies the application icon (Haiku's Mail
icon) and the mailbox/toolbar icons from a Haiku source tree;
`tools/make-ui-icons.py` draws the two icons Haiku has no match for. The
application uses HVIF, never the PNG previews. `tools/hvif.py` is the MIT-licensed codec shared with AirPins.

## Performance

Large mailboxes (hundreds of thousands of messages) drive a few load-bearing choices:

- Gmail checks fetch message contents on a pool of worker threads (up to 8) while the calling thread ingests results in batches of up to 400 messages per transaction. HTTPS connections and TLS sessions are reused per thread instead of renegotiated per request, and "already cached" is decided from one snapshot of cached IDs, so the store lock stays available to the UI during a long check.
- Bulk ingestion commits use relaxed durability (`synchronous=NORMAL`); an interrupted batch is redownloaded by the next check. Local edits (read/star/move/tag, drafts, outbox) always commit with full durability.
- Conversation lists rank and aggregate a lean column set and join the winning message back for its preview; unread counts skip the window function entirely. A synthetic 100,000-message mailbox lists in ~0.5 s and counts unread conversations in ~0.06 s (Linux, Release build).

`mail_perf_tests` (built by CMake, not part of CTest) seeds a synthetic mailbox and times ingestion and the UI queries:

```sh
cmake --build build-perf --target mail_perf_tests
./build-perf/mail_perf_tests 100000 25
```

`MAIL_PERF_KEEP_DB=1` keeps the seeded database for query analysis; `MAIL_PERF_BATCH_ONLY=1` skips the per-message-ingest baseline.

## Design and protocol references

The interface follows Haiku's native widgets, layout spacing, menu conventions,
system colors, and reversible organization actions. The icon uses the Haiku
perspective, shaded surfaces, edge contrast, and a rightward cast shadow.

- [Haiku Human Interface Guidelines](https://www.haiku-os.org/docs/HIG/index.xml)
- [Haiku icon guidelines](https://www.haiku-os.org/development/icon-guidelines)
- [Airmail](https://airmailapp.com/): unified inbox, snooze, rules, smart views.
- [Mimestream](https://mimestream.com/): native interface and Gmail API integration.
- [Google desktop OAuth](https://developers.google.com/identity/protocols/oauth2/native-app):
  loopback callback, state checking, PKCE and refresh tokens. A Desktop client
  registration is needed to validate live consent in the target system browser.
- [Gmail synchronization](https://developers.google.com/workspace/gmail/api/guides/sync),
  [message sending](https://developers.google.com/workspace/gmail/api/reference/rest/v1/users.messages/send),
  and [scopes](https://developers.google.com/workspace/gmail/api/auth/scopes).
- MIME [RFC 2045](https://www.rfc-editor.org/rfc/rfc2045.html),
  [RFC 2047](https://www.rfc-editor.org/rfc/rfc2047.html), and
  [RFC 2231](https://www.rfc-editor.org/rfc/rfc2231.html).
- Message headers [RFC 5322](https://www.rfc-editor.org/rfc/rfc5322.html) and
  related inline parts [RFC 2387](https://www.rfc-editor.org/rfc/rfc2387.html).
- [libcurl CONNECT_ONLY](https://curl.se/libcurl/c/CURLOPT_CONNECT_ONLY.html),
  [TLS requirements](https://curl.se/libcurl/c/CURLOPT_USE_SSL.html), and
  [IMAP MOVE / RFC 6851](https://www.rfc-editor.org/rfc/rfc6851.html).
- [libxml2 HTML parser](https://gnome.pages.gitlab.gnome.org/libxml2/html/HTMLparser_8h.html)
  and [Content Security Policy](https://www.w3.org/TR/CSP/): bounded received
  HTML parsing and renderer restrictions. The allowlist is implemented in Mail;
  libxml2 itself is a parser, not a sanitizer.
