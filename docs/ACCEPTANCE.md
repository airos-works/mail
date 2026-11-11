# Mail development release: acceptance and limits

Mail implements the features requested for the native air/OS email client.
The qualified target is x86_64 air/OS / Haiku with the private-font lifetime fix
and modern Summit WebKit runtime described in [installation](INSTALL.md).
This is a development release, not a claim of live-provider or public-distribution
qualification. Google setup instructions were requested in place of supplying
a Desktop OAuth client; no real mailbox or external recipient was used.

## Requested features

| Request | Delivered behavior | Verification |
| --- | --- | --- |
| Native app named Mail, informed by Airmail and Mimestream | Haiku Interface Kit windows, three-pane inbox, native menus/shortcuts, system colors, file panels and a separate formatted reader | Native workflow and reader tests; screenshots reviewed; references in the [README](../README.md#design-and-protocol-references) |
| Multiple accounts | Separate caches and key-store credentials, unified inbox, account views and sending identity selection | Core account isolation tests, Accounts UI fixture, real Bridge fixture with three identities |
| IMAP and POP3 | TLS/STARTTLS, authentication, receive, UID/UIDL identity, offline cache; IMAP read/star/move and Sent synchronization; SMTP sending | [Native protocol](evidence/mail-final-native-protocol.log), [Sent](evidence/mail-final-native-sent.log), [move](evidence/mail-final-native-moves.log) tests |
| Gmail API and Google login | System-browser PKCE login, loopback callback, verified account identity, key-store refresh tokens, receive, send, labels, drafts, reconnect and revoke | [Native Google fixture](evidence/draft-sync-native-google.log); [draft recovery UI](evidence/draft-recovery-native-ui.log); [setup guide](GOOGLE_SETUP.md) |
| Proton Mail Bridge | Loopback IMAP/SMTP preset, Bridge credentials and explicit certificate trust; server-managed Sent | [Real Bridge engine fixture](evidence/move-native-bridge.log), using a disposable local Proton API; [setup guide](BRIDGE_SETUP.md) |
| Haiku icon | Original HVIF resource and installed Tracker/Deskbar icon | Native 16/32/64 rendering, visual review and package lifecycle checks |
| Images and attachments, including drag and drop | Tracker file drops, positioned inline images, attachment controls, image preview, Mail-to-Mail and Mail-to-Tracker transfers | Native composer/inline-image regression; [actual Tracker drag capture](evidence/native-attachments.png) |
| Emoji and rich text | Unicode/grapheme-safe editing, monochrome sequence shaping, bold/italic/underline, links, inline images, HTML clipboard, multilevel undo/redo and rendered preview | [Native regression](evidence/mail-final-native.log), MIME checks and actual SMTP/Gmail fixture deliveries |
| Conversations and threads | Message-ID/reference and Gmail thread grouping; plain and formatted conversation readers; reply, reply-all and forward | Native workflow covers a two-message thread, Reply-To, self-recipient exclusion, references and exact forwarded attachment bytes |
| Automatic filtering | Persistent ordered rules with native create/edit/reorder/disable/remove and provider mutation handling | Host organization tests, native organization fixture and Gmail receive fixture, recorded in [status](STATUS.md) |
| Snoozed items | Presets/custom time, daylight-saving disambiguation, durable reminders, early wake, native notifications and restart recovery | Native snooze fixture and [notification capture](evidence/native-snooze-notification.png); host/native core checks |
| Smart folders and tags | Saved combined queries; native management; account-scoped tags and synchronized Gmail labels | Organization storage/API/UI fixtures; [smart-folder](evidence/native-organization-smart.png) and [tag](evidence/native-organization-tags.png) captures |
| Standard receive/read/compose/send/organize flow | Local autosave, reply/forward, private BCC, attachments, immutable Outbox, explicit uncertain-send recovery, Sent copies, archive, Trash, permanent deletion from Trash and search | [Native workflow regression](evidence/mail-final-native.log), protocol/Google/Bridge deliveries, Outbox recovery fixture and package reopen checks |

## Final qualification

- All 20 host CTest suites pass with address and undefined-behavior sanitizers:
  [host log](evidence/mail-final-host.log).
- Debug and Release native builds compile with warnings treated as errors.
  The Release regression passes core/storage/composition, reader, clipboard,
  typography, emoji, grapheme and image interaction checks:
  [native log](evidence/mail-final-native.log).
- The expanded main-window test exercises real widgets for import, threading,
  read, archive, search, reply, reply-all, forward, saved drafts and Trash.
  The [conversation screenshot](evidence/mail-final-inbox.png) was inspected.
- The new IMAP draft transport has 95 passing host/native checks covering exact
  MIME, TLS/STARTTLS, missing/lost APPEND receipts, UIDVALIDITY and targeted
  deletion: [host](evidence/imap-draft-host.log), [native](evidence/imap-draft-native.log).
  These are transport foundations only; no IMAP draft background synchronization
  is exposed in this release.
- Revision 9 retains database schema 14. Package install, revision 8 → 9 upgrade,
  installed-app launch/edit/preview, reopening and uninstall are checked in the
  disposable air/OS VM. The draft database and Haiku's older Mail app are retained.
  See [package evidence](evidence/package-v9-lifecycle.log).

## Known limits and follow-up work

Live Google consent in the target browser and live Google/Proton mailboxes still
need user-provided accounts and client registration. The fixtures exercise real
network protocols and native windows, but do not establish live service behavior.
Public Google distribution also requires the applicable restricted-scope process
described in [Google setup](GOOGLE_SETUP.md).

IMAP and POP3 drafts remain local; Gmail drafts synchronize. IMAP checks poll
full folder metadata without IDLE. Accounts synchronize sequentially. Large
mailboxes, provider quotas, delayed Sent copies and messages with rewritten
Message-IDs need further interoperability work. Messages are bounded to 32 MiB.

Snooze reminders run while Mail is open, with catch-up at the next launch.
There is no separate reminder daemon or new-mail notification feature.
Emoji are monochrome, the picker is a short menu, and OS window titles can have
fallback gaps. Rich text supports the styles listed above rather than arbitrary
browser layout. Received HTML blocks remote content and scripts; its links are
inert. Further CSS, contact/autocomplete, IMAP server drafts, color emoji,
background services and other architectures are tracked as follow-up enhancements.

The detailed implementation history and other interoperability limits remain
in [STATUS.md](STATUS.md). The source, repeatable checks, package and provider
setup guides are the handoff for this implementation.
