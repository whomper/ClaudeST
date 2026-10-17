# ClaudeST line protocol

ClaudeST (on the Atari) and `claude_bridge.py` (on a modern computer)
exchange plain lines over the serial link: fields separated by TAB
(`0x09`), lines ended by LF (`0x0A`). Text is in the Atari ST character
set; the bridge converts to and from Unicode. Text fields never contain
TAB or LF. Lines from the bridge are at most ~200 bytes.

The link is either TCP (ClaudeST connects through STinG to the bridge's
`--tcp` port, 2323 by default) or the serial port (19200 baud, 8N1, no
flow control). The protocol is the same on both.

## Atari → bridge

| Line | Meaning |
|------|---------|
| `HELLO` `1` `<version>` | Atari is up (sent at start, on ^R, and every ~5 s until the bridge answers). Bridge replies with the chat list and a status, and `C` with the chat it still has open. Since 1.14 the Atari ignores that `C` until something is opened, and sends `NEW QUIET` before its first `SEND`, so a fresh start shows no chat selected. |
| `LIST` `CHATS`\|`CODE`\|`PROJECTS`\|`ARTIFACTS` | Fill the sidebar with that list (`CODE`: Claude Code sessions). |
| `OPEN` `CHAT`\|`CODE`\|`PROJECT`\|`ARTIFACT` `<id>` | Show a conversation or Claude Code session, list a project's chats, or show an artifact. After `OPEN CODE`, `SEND` goes to that session until `NEW` or another `OPEN`. |
| `NEW` [`QUIET`] | Start a new chat (created on the first `SEND`; inside the open project, if any). `QUIET`: the Atari switched area and keeps its own title. |
| `SEND` `<text>` | Send a message to the current chat and stream the reply, with the files attached since the last `SEND` (since 1.39; the text may then be empty). |
| `ATTACH` `<name>` `<size>` | A file attached to the next message follows (since 1.39): `<size>` bytes in `ADATA` lines, then `AEND`. |
| `ADATA` `<hex>` | A piece of it, in hex (up to 480 bytes a line). |
| `AEND` | The file is complete. The bridge keeps it for the next `SEND`, or answers `a` and `A` if it arrived cut short. Any kind of file goes. |
| `DETACH` `<name>` | Drop the last attached file of that name (since 1.43: Backspace on the empty reply line). The bridge answers `a` `<name>`, which takes it off the Atari's list. |
| `UNATTACH` | Drop the files attached so far (`HELLO` drops them too). |
| `FIND` `<query>` | Search chat titles. |
| `RENAME` `CHAT`\|`CODE`\|`PROJECT` `<id>` `<name>` | Rename a chat, Claude Code session or project. |
| `PIN` `CHAT`\|`PROJECT` `<id>` `1`\|`0` | Pin (star) or unpin. |
| `DELETE` `CHAT`\|`PROJECT` `<id>` | Delete (the Atari has already asked for confirmation). |
| `ARCHIVE` `PROJECT`\|`CODE` `<id>` | Archive a project or a Claude Code session. |
| `PICKPROJ` `<chat id>` | The Atari wants a project list for "Move to project"; the bridge answers with `Q`/`J`/`W`. |
| `MOVE` `<chat id>` `<project id>` | Move a chat into a project. |
| `NEWPROJ` `<chat id>` `<name>` | Create a project with this name and move the chat into it ("New project…" in the Move menu). |
| `SAVE` `ARTIFACT` `<id>` | The user wants to save an artifact; the bridge answers `F`. |
| `FETCH` `ARTIFACT` `<id>` | Send the artifact's data (`D` lines, then `G`). |
| `CHOOSE` `<model id>` `<effort>` | Use this model and effort for the open chat and for new chats. The bridge answers with the model lists and `K`; an unknown model is ignored and an effort the model doesn't take becomes its default. Sent after connecting if `CLAUDEST.INF` holds a choice. |
| `ACCOUNT` | Show the Account page: the bridge answers `T Account`, `R` and an `X` message with Plan, Usage and Account sections. |
| `PING` | Is the bridge still there? (since 1.42) Sent when nothing has passed either way for 30 s and Claude isn't answering, only to a bridge that has sent `p`; the bridge answers `p`. No line within 20 s and the Atari shows *Gateway silent* and connects again (`HELLO`). |
| `BYE` | ClaudeST is quitting. |

## Bridge → Atari

All commands are one letter.

| Line | Meaning |
|------|---------|
| `S` `<text>` | Status line at the bottom of the sidebar: only the connection, e.g. `Online`, or `Online: demo` for another backend. |
| `u` [`<percent>` `<reset>`] | Session usage for the ring in the status area: percent of the current session limit used (0-100), and the time until it resets, such as `2h 10m`. A bare `u` hides the ring (no such limit). Sent after `S` when due: after each reply, otherwise at most every 2 minutes. |
| `N` `<text>` | A passing notice ("Loading chat...", "Scanning chat 3/40", "Renamed"), shown in the title bar, or in an empty list while it loads, for up to ~5 s. An empty `N` clears it; the bridge sends one when a command is done. |
| `L` `<kind>` `<title>` | Start a sidebar list (`CHATS`, `CODE`, `PROJECTS`, `PROJECT`, `ARTIFACTS`, `SEARCH`). |
| `I` `<id>` `<label>` [`<flags>`] | List item. Flags: `P` pinned, `U` unread (updated since last read; since 1.17); for example `PU`. Code sessions also (since 1.38): `W` Claude is working on it, `A` it waits for you (a question, a permission), as claude.ai's dots. The id `..` means "back to all projects". Ids are at most 39 characters; the bridge sends longer ones (such as artifact file paths) as a `~` stand-in and maps them back. |
| `p` | The bridge answers `PING` (since 1.42). It also sends one first thing after `HELLO`, which tells the Atari that this bridge answers pings. While idle the bridge also checks the session usage and claude.ai every 5 minutes, sending `u`, and `S` when the status changed (*claude.ai silent*, *Not signed in to claude.ai*, *Online*). |
| `a` `<name>` | The bridge turned this attachment down (since 1.39); an `A` alert says why. |
| `d` `<id>` `<flags>` | An item's flags changed (since 1.38): the bridge sends it unasked while the Code list shows a session at work, about every 20 seconds, so its dot follows without the list being sent again. |
| `X` `<label>` | A heading in the list ("Pinned", "Today", "Yesterday", "Oct 4", "Older"); the items after it belong to it. Headings can't be selected. |
| `E` | End of list. |
| `Q` | Start a picker list (for "Move to project"). |
| `J` `<id>` `<label>` | Picker entry. |
| `W` | Show the picker as a popup. |
| `C` `<id>` | Id of the open chat (highlighted in the sidebar). |
| `T` `<title>` | Conversation title. |
| `R` | Clear the conversation pane. |
| `M` `<role>` | Begin a message: `U` you, `A` Claude, `K` artifact, `I` info, `E` error, `X` a page with no label (the Account page). |
| `P` `<text>` | Append text to the current paragraph (streamed replies arrive as many `P`s). |
| `B` | Line break / new paragraph. |
| `H` `<text>` | A bold heading line. |
| `c` `<text>` | A line of a code block (since 1.41), shown in blue; the bridge sends it only to ClaudeST 1.41 and later (by `HELLO`'s version), and `P` lines to older ones. |
| `g` `<text>` | A grey line (since 1.38): a run of tool calls in a Code session, folded into one line as claude.ai shows it ("Ran 2 commands", or the step's own description). |
| `Z` | End of message. |
| `Y` `0`\|`1` | Claude is (not) replying. |
| `A` `<text>` | Show an alert box. |
| `F` `<name>` | Suggested 8.3 file name for a `SAVE`; the Atari shows the file selector, then sends `FETCH` (or nothing if cancelled). |
| `D` `<hex>` | Up to 90 bytes of the file being saved, hex-encoded. Text is already in the Atari character set with CR/LF line ends. |
| `G` `<size>` | The file is complete. |
| `O` | Start the model lists (sent after `HELLO`, `CHOOSE`, opening a chat, and `NEW` when the model changes: each chat keeps its own model). |
| `V` `<id>` `<label>` | A model for the model menu. |
| `U` `<id>` `<label>` | An effort level the current model supports (none for Haiku 4.5). |
| `K` `<model id>` `<effort>` | The model and effort in use; shown on the chip in the reply line. |

Markdown is simplified by the bridge: headings become `H`, `**bold**` and
`*italic*` markers are dropped, `[text](url)` becomes `text <url>`, and
code fences become `[lang]` … `[end]` with the code indented (in `c`
lines for 1.41 on). For 1.41 on, inline code is sent between `0x14`
bytes in `P` text, the only control character the bridge sends there:
the Atari shows it in red (underlined in monochrome) and doesn't
count the marks when wrapping. Older ClaudeST gets the backticks. The
Atari does all word wrapping, so the text reflows when the window is resized.
