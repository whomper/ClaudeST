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
| `NEW` | Start a new chat (created on the first `SEND`; inside the open project, if any). |
| `SEND` `<text>` | Send a message to the current chat and stream the reply. |
| `FIND` `<query>` | Search chat titles. |
| `BYE` | Claude ST is quitting. |

## Bridge → Atari

All commands are one letter.

| Line | Meaning |
|------|---------|
| `S` `<text>` | Status line at the bottom of the sidebar. |
| `L` `<kind>` `<title>` | Start a sidebar list (`CHATS`, `PROJECTS`, `PROJECT`, `ARTIFACTS`, `SEARCH`). |
| `I` `<id>` `<label>` | List item. The id `..` means "back to all projects". |
| `E` | End of list. |
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

Markdown is simplified by the bridge: headings become `H`, `**bold**` and
`*italic*` markers are dropped, `[text](url)` becomes `text <url>`, and
code fences become `[lang]` … `[end]` with the code indented. The Atari
does all word wrapping, so the text reflows when the window is resized.
