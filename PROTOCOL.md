# Claude ST line protocol

Claude ST (on the Atari) and `claude_bridge.py` (on a modern computer)
exchange plain lines over the serial link: fields separated by TAB
(`0x09`), lines ended by LF (`0x0A`). Text is in the Atari ST character
set; the bridge converts to and from Unicode. Text fields never contain
TAB or LF. Lines from the bridge are at most ~200 bytes.

The link is either TCP (Claude ST connects through STinG to the bridge's
`--tcp` port, 2323 by default) or the serial port (19200 baud, 8N1, no
flow control). The protocol is the same on both.

## Atari → bridge

| Line | Meaning |
|------|---------|
| `HELLO` `1` `<version>` | Atari is up (sent at start, on ^R, and every ~5 s until the bridge answers). Bridge replies with the chat list and a status. |
| `LIST` `CHATS`\|`PROJECTS`\|`ARTIFACTS` | Fill the sidebar with that list. |
| `OPEN` `CHAT`\|`PROJECT`\|`ARTIFACT` `<id>` | Show a conversation, list a project's chats, or show an artifact. |
| `NEW` [`QUIET`] | Start a new chat (created on the first `SEND`; inside the open project, if any). `QUIET`: the Atari switched area and keeps its own title. |
| `SEND` `<text>` | Send a message to the current chat and stream the reply. |
| `FIND` `<query>` | Search chat titles. |
| `RENAME` `CHAT`\|`PROJECT` `<id>` `<name>` | Rename a chat or project. |
| `PIN` `CHAT`\|`PROJECT` `<id>` `1`\|`0` | Pin (star) or unpin. |
| `DELETE` `CHAT`\|`PROJECT` `<id>` | Delete (the Atari has already asked for confirmation). |
| `ARCHIVE` `PROJECT` `<id>` | Archive a project. |
| `PICKPROJ` `<chat id>` | The Atari wants a project list for "Move to project"; the bridge answers with `Q`/`J`/`W`. |
| `MOVE` `<chat id>` `<project id>` | Move a chat into a project. |
| `SAVE` `ARTIFACT` `<id>` | The user wants to save an artifact; the bridge answers `F`. |
| `FETCH` `ARTIFACT` `<id>` | Send the artifact's data (`D` lines, then `G`). |
| `CHOOSE` `<model id>` `<effort>` | Use this model and effort for the open chat and for new chats. The bridge answers with the model lists and `K`; an unknown model is ignored and an effort the model doesn't take becomes its default. Sent after connecting if `CLAUDE.INF` holds a choice. |
| `ACCOUNT` | Show the Account page: the bridge answers `T Account`, `R` and an info message with Plan, Usage and Account sections. |
| `BYE` | Claude ST is quitting. |

## Bridge → Atari

All commands are one letter.

| Line | Meaning |
|------|---------|
| `S` `<text>` | Status line at the bottom of the sidebar: only the connection, e.g. `Online: claude.ai`. |
| `N` `<text>` | A passing notice ("Loading chat...", "Scanning chat 3/40", "Renamed"), shown in the title bar, or in an empty list while it loads, for up to ~5 s. An empty `N` clears it; the bridge sends one when a command is done. |
| `L` `<kind>` `<title>` | Start a sidebar list (`CHATS`, `PROJECTS`, `PROJECT`, `ARTIFACTS`, `SEARCH`). |
| `I` `<id>` `<label>` [`P`] | List item; `P` marks it pinned. The id `..` means "back to all projects". Ids are at most 39 characters; the bridge sends longer ones (such as artifact file paths) as a `~` stand-in and maps them back. |
| `E` | End of list. |
| `Q` | Start a picker list (for "Move to project"). |
| `J` `<id>` `<label>` | Picker entry. |
| `W` | Show the picker as a popup. |
| `C` `<id>` | Id of the open chat (highlighted in the sidebar). |
| `T` `<title>` | Conversation title. |
| `R` | Clear the conversation pane. |
| `M` `<role>` | Begin a message: `U` you, `A` Claude, `K` artifact, `I` info, `E` error. |
| `P` `<text>` | Append text to the current paragraph (streamed replies arrive as many `P`s). |
| `B` | Line break / new paragraph. |
| `H` `<text>` | A bold heading line. |
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
code fences become `[lang]` … `[end]` with the code indented. The Atari
does all word wrapping, so the text reflows when the window is resized.
